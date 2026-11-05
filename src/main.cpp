// SPDX-License-Identifier: GPL-2.0-or-later
#include "diario/bufr.hpp"
#include "diario/observations.hpp"
#include "diario/render.hpp"

#include <wreport/subset.h>
#include <wreport/var.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

constexpr const char* help = R"(diario-meteo 0.1.0 — C++ / libwreport

Uso:
  diario-meteo inspect [--details] FILE.bufr ...
  diario-meteo export [--output FILE.csv] FILE.bufr ...
  diario-meteo verify FILE.bufr ...
  diario-meteo build --output DIRECTORY [--station WMO] [--gnuplot PROGRAMMA] FILE.bufr ...

inspect  Mostra i bollettini e le variabili senza interpretarli come osservazioni.
export   Esporta le variabili e gli attributi, preservando gli indici e i mancanti.
verify   Decodifica, ricodifica e confronta semanticamente ogni bollettino.
build    Estrae temperature da un profilo di superficie limitato e produce HTML/SVG.

--station e' ripetibile; identificatore WMO a cinque cifre, per esempio 16148.
La directory di build deve essere nuova. Nessun download o deploy automatico.
I tempi delle osservazioni sono UTC; gli indici di messaggio/subset partono da zero.
)";

struct Arguments {
	std::string command;
	std::vector<std::filesystem::path> files;
	std::filesystem::path output;
	std::set<std::string> stations;
	std::string gnuplot = "gnuplot";
	bool details = false;
};

Arguments parse(int argc, char** argv)
{
	Arguments args;
	args.command = argv[1];
	if (args.command != "inspect" && args.command != "export" && args.command != "verify" && args.command != "build")
		throw std::invalid_argument("Comando sconosciuto. Usare --help.");
	bool positional = false;
	for (int index = 2; index < argc; ++index) {
		const std::string value = argv[index];
		if (!positional && value == "--") { positional = true; continue; }
		if (!positional && value == "--details" && args.command == "inspect") { args.details = true; continue; }
		if (!positional && (value == "--output" || value == "--station" || value == "--gnuplot")) {
			if (++index == argc) throw std::invalid_argument("Manca il valore di " + value);
			if (value == "--output") {
				if (args.command != "export" && args.command != "build") throw std::invalid_argument("--output non e' previsto per questo comando.");
				args.output = argv[index];
			} else {
				if (args.command != "build") throw std::invalid_argument(value + " e' previsto soltanto per build.");
				if (value == "--gnuplot") args.gnuplot = argv[index];
				else {
					const std::string station = argv[index];
					if (station.size() != 5 || !std::all_of(station.begin(), station.end(), [](unsigned char c) { return c >= '0' && c <= '9'; }))
						throw std::invalid_argument("--station richiede cinque cifre WMO.");
					args.stations.insert(station);
				}
			}
			continue;
		}
		if (!positional && value.starts_with('-')) throw std::invalid_argument("Opzione non riconosciuta: " + value);
		args.files.emplace_back(value);
	}
	if (args.files.empty()) throw std::invalid_argument("Specificare almeno un file BUFR.");
	if (args.command == "build" && args.output.empty()) throw std::invalid_argument("build richiede --output DIRECTORY.");
	return args;
}

void output(const std::string& text)
{
	if (std::fwrite(text.data(), 1, text.size(), stdout) != text.size()) throw std::runtime_error("Errore scrivendo su stdout.");
}

void report(const diario::InputStats& stats)
{
	std::fprintf(stderr, "Letti: %zu file, %zu messaggi, %zu subset, %zu variabili.\n",
		stats.files, stats.messages, stats.subsets, stats.variables);
}

} // namespace

int main(int argc, char** argv)
{
	try {
		if (argc < 2 || std::string(argv[1]) == "--help" || std::string(argv[1]) == "-h") {
			output(help);
			return argc < 2 ? EXIT_FAILURE : EXIT_SUCCESS;
		}
		if (std::string(argv[1]) == "--version") { output("diario-meteo 0.1.0\n"); return EXIT_SUCCESS; }
		const auto args = parse(argc, argv);
		std::string csv = diario::raw_csv_header();
		std::vector<diario::Observation> observations;
		std::vector<std::string> warnings;
		std::size_t input_index = 0;
		const auto stats = diario::read_bufr(args.files, [&](const wreport::BufrBulletin& bulletin,
			const std::filesystem::path& path, std::size_t message_index) {
			if (message_index == 0) ++input_index;
			// Distinguish equally named inputs without publishing local directory names.
			const auto source = std::format("input{}:{}", input_index, path.filename().string());
			if (args.command == "inspect") {
				output(std::format("\nFile: {} | messaggio: {} | offset: {}\n", path.string(), message_index, static_cast<long long>(bulletin.offset)));
				output(std::format("BUFR {} | categoria: {} | subset: {} | data di riferimento intestazione: {:04}-{:02}-{:02} {:02}:{:02}:{:02}\n",
					bulletin.edition_number, bulletin.data_category, bulletin.subsets.size(),
					bulletin.rep_year, bulletin.rep_month, bulletin.rep_day, bulletin.rep_hour, bulletin.rep_minute, bulletin.rep_second));
				if (args.details) bulletin.print_structured(stdout);
				else bulletin.print(stdout);
			} else if (args.command == "export") {
				csv += diario::raw_csv_rows(bulletin, source, message_index);
			} else if (args.command == "verify") {
				auto options = wreport::BufrCodecOptions::create();
				options->decode_adds_undef_attrs = true;
				auto decoded = wreport::BufrBulletin::decode(bulletin.encode(), *options);
				const auto differences = bulletin.diff(*decoded);
				if (differences) throw std::runtime_error(std::format("Roundtrip: {} differenze semantiche.", differences));
				output(std::format("OK {} messaggio {}: decode/encode/decode.\n", path.filename().string(), message_index));
			} else {
				auto extracted = diario::extract_temperatures(bulletin, source, message_index, warnings);
				for (auto& observation : extracted) {
					if (args.stations.empty() || args.stations.contains(observation.station_id)) observations.push_back(std::move(observation));
				}
			}
		});
		report(stats);
		for (const auto& warning : warnings) std::fprintf(stderr, "Nota: %s\n", warning.c_str());
		if (args.command == "export") {
			if (args.output.empty()) output(csv);
			else {
				for (const auto& input : args.files) {
					if (std::filesystem::exists(args.output) && std::filesystem::equivalent(input, args.output))
						throw std::runtime_error("Il CSV di output coincide con un BUFR di input.");
				}
				std::ofstream file(args.output, std::ios::binary | std::ios::trunc);
				file.exceptions(std::ios::badbit | std::ios::failbit);
				file.write(csv.data(), static_cast<std::streamsize>(csv.size()));
				file.close();
			}
		} else if (args.command == "build") {
			if (observations.empty()) throw std::runtime_error("Nessuna temperatura supportata per le stazioni selezionate. Usare inspect/export per esaminare i dati.");
			diario::render_diary(observations, warnings, args.output, args.gnuplot);
			output(std::format("Creato {}: {} osservazioni.\n", (args.output / "index.html").string(), observations.size()));
		}
		return EXIT_SUCCESS;
	} catch (const std::exception& error) {
		std::fprintf(stderr, "Errore: %s\n", error.what());
		return EXIT_FAILURE;
	}
}
