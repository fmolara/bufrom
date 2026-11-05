// SPDX-License-Identifier: GPL-2.0-or-later
#include "diario/bufr.hpp"
#include "diario/observations.hpp"

#include <wreport/subset.h>
#include <wreport/var.h>
#include <wreport/varinfo.h>
#include <wreport/vartable.h>

#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace {

void require(bool condition, const std::string& message)
{
	if (!condition)
		throw std::runtime_error(message);
}

void require_near(double actual, double expected, const std::string& message,
	double tolerance = 0.000001)
{
	require(std::isfinite(actual) && std::abs(actual - expected) < tolerance,
		message + ": atteso " + std::to_string(expected)
		+ ", ottenuto " + std::to_string(actual));
}

// wreport 3.36 usa esplicitamente -273.15001 nella conversione K -> C:
// https://sources.debian.org/src/wreport/3.36-1.1/wreport/conv.cc/
// Lo scarto di 0.00001 non nasce dalla decodifica. La tolleranza seguente
// ammette quel comportamento upstream, rimanendo 1000 volte piu' fine del
// passo 0.1 K del fixture. Non modifica i dati dell'applicazione.
constexpr double temperature_tolerance = 0.0001;

std::string read_bytes(const fs::path& path)
{
	std::ifstream input(path, std::ios::binary);
	require(input.is_open(), "Impossibile leggere " + path.string());
	std::string bytes{std::istreambuf_iterator<char>{input}, {}};
	require(!input.bad(), "Errore di lettura: " + path.string());
	return bytes;
}

void write_bytes(const fs::path& path, const std::string& bytes)
{
	std::ofstream output(path, std::ios::binary);
	require(output.is_open(), "Impossibile scrivere " + path.string());
	output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
	output.close();
	require(!output.fail(), "Errore di scrittura: " + path.string());
}

class TemporaryDirectory
{
public:
	TemporaryDirectory()
	{
		const auto pattern = (fs::temp_directory_path() / "diario-native-XXXXXX").string();
		std::vector<char> buffer(pattern.begin(), pattern.end());
		buffer.push_back('\0');
		const char* created = ::mkdtemp(buffer.data());
		if (!created)
			throw std::runtime_error(std::string{"mkdtemp: "} + std::strerror(errno));
		path = created;
	}

	~TemporaryDirectory()
	{
		std::error_code ignored;
		fs::remove_all(path, ignored);
	}

	TemporaryDirectory(const TemporaryDirectory&) = delete;
	TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;

	fs::path path;
};

class TimezoneRestore
{
public:
	TimezoneRestore()
	{
		if (const char* value = std::getenv("TZ"))
			previous = value;
	}

	~TimezoneRestore()
	{
		if (previous)
			::setenv("TZ", previous->c_str(), 1);
		else
			::unsetenv("TZ");
		::tzset();
	}

	void set(const char* value)
	{
		require(::setenv("TZ", value, 1) == 0, "Impossibile impostare TZ per il test");
		::tzset();
	}

private:
	std::optional<std::string> previous;
};

std::unique_ptr<wreport::BufrBulletin> decode(const std::string& bytes)
{
	auto options = wreport::BufrCodecOptions::create();
	options->decode_adds_undef_attrs = true;
	return wreport::BufrBulletin::decode(bytes, *options, "test fixture");
}

wreport::Var& variable(wreport::Subset& subset, wreport::Varcode code)
{
	for (auto& value : subset)
		if (value.code() == code)
			return value;
	throw std::runtime_error("Variabile mancante nel fixture: " + wreport::varcode_format(code));
}

diario::Observation extract_one(const wreport::BufrBulletin& bulletin)
{
	std::vector<std::string> warnings;
	auto observations = diario::extract_temperatures(bulletin, "test fixture", 0, warnings);
	require(observations.size() == 1, "Il fixture deve produrre una sola osservazione");
	return observations.front();
}

// Parser di CSV usato solo nei test: legge anche virgole, virgolette doppie e
// newline incorporati nei campi. Le attese confrontano dati, non righe serializzate.
using CsvRows = std::vector<std::vector<std::string>>;

CsvRows parse_csv(std::string_view text)
{
	CsvRows rows;
	std::vector<std::string> row;
	std::string field;
	bool quoted = false;
	bool closed_quote = false;
	for (std::size_t index = 0; index < text.size(); ++index)
	{
		const char ch = text[index];
		if (quoted)
		{
			if (ch == '"')
			{
				if (index + 1 < text.size() && text[index + 1] == '"')
				{
					field += '"';
					++index;
				}
				else
				{
					quoted = false;
					closed_quote = true;
				}
			}
			else
				field += ch;
		}
		else if (ch == ',' || ch == '\n')
		{
			row.push_back(std::move(field));
			field.clear();
			closed_quote = false;
			if (ch == '\n')
			{
				rows.push_back(std::move(row));
				row.clear();
			}
		}
		else if (ch == '"')
		{
			require(field.empty() && !closed_quote, "CSV non valido: virgoletta inattesa");
			quoted = true;
		}
		else
		{
			require(!closed_quote, "CSV non valido: testo dopo la virgoletta finale");
			field += ch;
		}
	}
	require(!quoted, "CSV non valido: campo quotato incompleto");
	if (!field.empty() || !row.empty() || closed_quote)
	{
		row.push_back(std::move(field));
		rows.push_back(std::move(row));
	}
	return rows;
}

const std::vector<std::string>& variable_row(const CsvRows& rows, wreport::Varcode code)
{
	for (const auto& row : rows)
	{
		require(row.size() == 12, "Numero di colonne inatteso nel CSV grezzo");
		if (row[4] == "variable" && row[6] == wreport::varcode_format(code))
			return row;
	}
	throw std::runtime_error("Variabile assente dal CSV: " + wreport::varcode_format(code));
}

std::string require_error(const std::function<void()>& operation)
{
	try
	{
		operation();
	}
	catch (const std::exception& error)
	{
		return error.what();
	}
	throw std::runtime_error("L'operazione doveva segnalare un errore");
}

void test_known_fixture(const fs::path& fixture)
{
	std::size_t visits = 0;
	const auto stats = diario::read_bufr({fixture}, [&](const auto& bulletin,
		const auto& source, std::size_t message_index)
	{
		++visits;
		require(message_index == 0 && source == fixture, "Provenienza del messaggio errata");
		require(bulletin.subsets.size() == 1, "Atteso un subset nel fixture di riferimento");
		const auto& subset = bulletin.subsets[0];
		require(subset.size() == 52, "Attese 52 variabili nel fixture di riferimento");
		require(subset[0].code() == WR_VAR(0, 1, 1) && subset[0].enqi() == 60,
			"Blocco WMO del fixture errato");
		require(subset[1].code() == WR_VAR(0, 1, 2) && subset[1].enqi() == 150,
			"Numero stazione WMO del fixture errato");
		require(subset[17].code() == WR_VAR(0, 12, 4), "Descrittore della temperatura errato");
		require_near(subset[17].enqd(), 289.2, "Temperatura fisica decodificata in kelvin");
		for (std::size_t index : {0u, 1u})
		{
			const auto* confidence = subset[index].enqa(WR_VAR(0, 33, 7));
			require(confidence && confidence->isset() && confidence->enqi() == 70,
				"Attributo B33007 del fixture non conservato");
		}
		std::vector<std::string> warnings;
		const auto observations = diario::extract_temperatures(
			bulletin, source.string(), message_index, warnings);
		require(observations.size() == 1, "Attesa una temperatura nel fixture di riferimento");
		const auto& observation = observations[0];
		require(observation.station_id == "60150", "Identificativo stazione atteso: 60150");
		require(observation.timestamp == "2004-11-30T12:00:00Z", "Timestamp osservazione errato");
		require(observation.epoch_seconds == 1101816000, "Epoch UTC del fixture errato");
		require_near(observation.temperature_c, 16.05, "Temperatura del fixture", temperature_tolerance);
		require(observation.sensor_height_m.has_value(), "B12004 deve indicare l'altezza di 2 m");
		require_near(*observation.sensor_height_m, 2.0, "Altezza del sensore");
		require(observation.source == fixture.string() && observation.message_index == 0
			&& observation.subset_index == 0, "Provenienza dell'osservazione errata");
		require(!warnings.empty(), "L'assunzione dei secondi mancanti deve essere segnalata");
	});
	require(visits == 1 && stats.files == 1 && stats.messages == 1
		&& stats.subsets == 1 && stats.variables == 52, "Conteggio del fixture errato");
}

void test_round_trip(const std::string& bytes)
{
	const auto original = decode(bytes);
	const auto restored = decode(original->encode());
	require(original->diff(*restored) == 0,
		"Il roundtrip BUFR ha cambiato i dati o i loro attributi");
}

void test_utc_and_observation_time(const std::string& bytes)
{
	TimezoneRestore timezone;
	auto bulletin = decode(bytes);
	timezone.set("UTC0");
	const auto utc = extract_one(*bulletin);
	timezone.set("HST10");
	const auto other_zone = extract_one(*bulletin);
	require(utc.timestamp == "2004-11-30T12:00:00Z" && utc.epoch_seconds == 1101816000,
		"La data del fixture deve rappresentare l'istante UTC atteso");
	require(other_zone.timestamp == utc.timestamp && other_zone.epoch_seconds == utc.epoch_seconds,
		"Il fuso orario del processo ha alterato la data UTC");
	bulletin->rep_year = 1999;
	bulletin->rep_month = 1;
	bulletin->rep_day = 2;
	bulletin->rep_hour = 3;
	bulletin->rep_minute = 4;
	bulletin->rep_second = 5;
	const auto changed_header = extract_one(*bulletin);
	require(changed_header.timestamp == utc.timestamp
		&& changed_header.epoch_seconds == utc.epoch_seconds,
		"La data dell'header ha sostituito la data dell'osservazione");
}

void test_missing_zero_and_attributes(const std::string& bytes)
{
	auto bulletin = decode(bytes);
	auto& humidity = variable(bulletin->subsets[0], WR_VAR(0, 13, 3));
	require(!humidity.isset(), "Il fixture deve contenere umidita' mancante");
	const std::string source = "fixture, \"citato\"\nseconda riga";
	const auto missing = parse_csv(diario::raw_csv_rows(*bulletin, source, 4));
	const auto& missing_humidity = variable_row(missing, WR_VAR(0, 13, 3));
	require(missing_humidity[7] == "0" && missing_humidity[8].empty(),
		"Il CSV deve distinguere il valore mancante da uno zero");
	bool found_attribute = false;
	for (const auto& row : missing)
	{
		require(row.size() == 12 && row[0] == source && row[1] == "4",
			"CSV: provenienza o escaping non conservati");
		if (row[4] == "attribute" && row[3] == "0"
			&& row[5] == wreport::varcode_format(WR_VAR(0, 1, 1))
			&& row[6] == wreport::varcode_format(WR_VAR(0, 33, 7)))
		{
			require(row[7] == "1" && row[8] == "70", "Attributo di qualita' errato nel CSV");
			found_attribute = true;
		}
	}
	require(found_attribute, "Il CSV ha perso l'attributo B33007 del blocco WMO");
	humidity.seti(0);
	const auto zero = parse_csv(diario::raw_csv_rows(*bulletin, source, 4));
	const auto& zero_humidity = variable_row(zero, WR_VAR(0, 13, 3));
	require(zero_humidity[7] == "1" && zero_humidity[8] == "0",
		"Uno zero definito non deve diventare un valore mancante");
	variable(bulletin->subsets[0], WR_VAR(0, 12, 4)).unset();
	std::vector<std::string> warnings;
	require(diario::extract_temperatures(*bulletin, source, 0, warnings).empty(),
		"Una temperatura mancante non deve produrre un'osservazione");
	require(!warnings.empty(), "La temperatura assente deve produrre una diagnostica");
}

void test_generic_height_and_ambiguity(const std::string& bytes)
{
	auto generic = decode(bytes);
	auto& temperature = variable(generic->subsets[0], WR_VAR(0, 12, 4));
	const auto info = generic->tables.btable->query(WR_VAR(0, 12, 101));
	// In wreport 3.36 l'assegnazione da rvalue conserva il vecchio Varinfo,
	// mentre la copia aggiorna anche descrittore e scala (wreport/var.cc).
	// Qui cambiamo descrittore: usiamo esplicitamente l'assegnazione per copia.
	const wreport::Var generic_temperature(info, 289.2);
	temperature = generic_temperature;
	require(temperature.code() == WR_VAR(0, 12, 101), "Preparazione del descrittore generico fallita");
	require_near(temperature.enqd(), 289.2, "Preparazione della temperatura generica in kelvin");
	const auto observation = extract_one(*generic);
	require_near(observation.temperature_c, 16.05, "Temperatura generica", temperature_tolerance);
	require(!observation.sensor_height_m, "B12101 da solo non dimostra un'altezza di 2 m");

	auto ambiguous = decode(bytes);
	wreport::Var duplicate = variable(ambiguous->subsets[0], WR_VAR(0, 12, 4));
	ambiguous->subsets[0].push_back(std::move(duplicate));
	std::vector<std::string> warnings;
	require(diario::extract_temperatures(*ambiguous, "ambiguous", 0, warnings).empty(),
		"Due temperature nello stesso subset non devono essere selezionate arbitrariamente");
	require(!warnings.empty(), "Un subset ambiguo deve produrre una diagnostica");
}

void test_input_framing(const std::string& bytes)
{
	TemporaryDirectory temporary;
	const auto concatenated = temporary.path / "concatenated.bufr";
	write_bytes(concatenated, bytes + bytes);
	std::vector<std::size_t> indices;
	const auto stats = diario::read_bufr({concatenated}, [&](const auto& bulletin,
		const auto& source, std::size_t index)
	{
		require(source == concatenated && bulletin.subsets.size() == 1,
			"Messaggio concatenato con provenienza o subset errati");
		indices.push_back(index);
	});
	require(stats.files == 1 && stats.messages == 2 && stats.subsets == 2
		&& stats.variables == 104 && indices == std::vector<std::size_t>{0, 1},
		"La lettura deve attraversare entrambi i messaggi concatenati");

	const auto empty = temporary.path / "empty.bufr";
	write_bytes(empty, "");
	const auto ignore = [](const auto&, const auto&, std::size_t) {};
	const auto empty_error = require_error([&] { diario::read_bufr({empty}, ignore); });
	require(empty_error.find(empty.string()) != std::string::npos,
		"L'errore del file vuoto deve identificare il file");

	require(bytes.size() > 16, "Fixture troppo corto per il test di troncamento");
	const auto truncated = temporary.path / "truncated.bufr";
	write_bytes(truncated, bytes.substr(0, bytes.size() - 6));
	const auto truncated_error = require_error([&] { diario::read_bufr({truncated}, ignore); });
	require(truncated_error.find(truncated.string()) != std::string::npos,
		"L'errore di troncamento deve identificare il file");
}

void test_romagna_fixture(const fs::path& fixture)
{
	std::size_t cervia = 0;
	std::size_t rimini = 0;
	const auto stats = diario::read_bufr({fixture}, [&](const auto& bulletin,
		const auto& source, std::size_t index)
	{
		std::vector<std::string> warnings;
		const auto observations = diario::extract_temperatures(bulletin, source.string(), index, warnings);
		for (const auto& observation : observations)
		{
			if (observation.station_id != "16148" && observation.station_id != "16149")
				continue;
			require(observation.timestamp == "2010-08-15T00:00:00Z"
				&& observation.epoch_seconds == 1281830400, "Data del fixture Romagna errata");
			require(observation.sensor_height_m.has_value(), "Altezza esplicita mancante in Romagna");
			require_near(*observation.sensor_height_m, 2.0, "Altezza delle temperature in Romagna");
			if (observation.station_id == "16148")
			{
				++cervia;
				require_near(observation.temperature_c, 21.85, "Temperatura di Cervia", temperature_tolerance);
				require(observation.message_index == 62, "Provenienza di Cervia errata");
			}
			else
			{
				++rimini;
				require_near(observation.temperature_c, 22.45, "Temperatura di Rimini", temperature_tolerance);
				require(observation.message_index == 41, "Provenienza di Rimini errata");
			}
		}
	});
	require(stats.messages == 66 && stats.subsets == 66, "Attesi 66 messaggi nel fixture synop3new");
	require(cervia == 1 && rimini == 1, "Attesa una osservazione per Cervia e una per Rimini");
}

void run_test(const char* name, const std::function<void()>& test)
{
	try
	{
		test();
	}
	catch (const std::exception& error)
	{
		throw std::runtime_error(std::string{name} + ": " + error.what());
	}
	std::printf("[OK] %s\n", name);
}

} // namespace

int main(int argc, char** argv)
{
	if (argc != 2)
	{
		std::fprintf(stderr, "Uso: %s PERCORSO_TESTS_DATA\n", argv[0]);
		return EXIT_FAILURE;
	}
	try
	{
		const fs::path data = argv[1];
		const auto reference = data / "obs0-1.22.bufr";
		const auto bytes = read_bytes(reference);
		run_test("fixture reale: valori, attributi e provenienza", [&] { test_known_fixture(reference); });
		run_test("roundtrip BUFR semantico", [&] { test_round_trip(bytes); });
		run_test("UTC indipendente da TZ e dalla data dell'header", [&] { test_utc_and_observation_time(bytes); });
		run_test("CSV: valori mancanti, zero, attributi ed escaping", [&] { test_missing_zero_and_attributes(bytes); });
		run_test("altezza sconosciuta e temperatura ambigua", [&] { test_generic_height_and_ambiguity(bytes); });
		run_test("input concatenato, vuoto e troncato", [&] { test_input_framing(bytes); });
		run_test("fixture Romagna: Cervia e Rimini", [&] { test_romagna_fixture(data / "synop3new.bufr"); });
		std::puts("Tutti i test nativi sono passati.");
		return EXIT_SUCCESS;
	}
	catch (const std::exception& error)
	{
		std::fprintf(stderr, "[ERRORE] %s\n", error.what());
		return EXIT_FAILURE;
	}
}
