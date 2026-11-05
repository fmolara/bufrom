// SPDX-License-Identifier: GPL-2.0-or-later
#include "diario/bufr.hpp"
#include "diario/archive.hpp"
#include "diario/observations.hpp"
#include "diario/render.hpp"

#include <wreport/subset.h>
#include <wreport/var.h>
#include <algorithm>
#include <cstdio>
#include <chrono>
#include <regex>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

constexpr const char* help = R"(bufrom 0.2.0-dev — C++ / libwreport

Uso:
  bufrom inspect [--details] FILE.bufr ...
  bufrom export [--output FILE.csv] FILE.bufr ...
  bufrom verify FILE.bufr ...
  bufrom import --database FILE --raw-dir DIRECTORY --origin URL --dataset NAME [--acquired-at UTC] FILE.bufr ...
  bufrom site --database FILE --output DIRECTORY --repository URL [--hours 48] [--asset-prefix PATH]
  bufrom build --output DIRECTORY [--station WMO] [--gnuplot PROGRAMMA] FILE.bufr ...

inspect  Mostra i bollettini e le variabili senza interpretarli come osservazioni.
export   Esporta le variabili e gli attributi, preservando gli indici e i mancanti.
verify   Decodifica, ricodifica e confronta semanticamente ogni bollettino.
build    Estrae temperature da un profilo di superficie limitato e produce HTML/SVG.

--station è ripetibile: nome/ID della fonte oppure chiave rete:latitudine,longitudine.
Le directory di build/site devono essere nuove. Acquisizione e pubblicazione sono gestite da scripts/update.sh.
I tempi delle osservazioni sono UTC; gli indici di messaggio/subset partono da zero.
)";

struct Arguments {
	std::string command;
	std::vector<std::filesystem::path> files;
	std::filesystem::path output;
	std::set<std::string> stations;
	std::string gnuplot = "gnuplot";
	bool details = false;
	std::filesystem::path database, raw_dir;
	std::string origin, dataset, acquired_at, repository, asset_prefix;
	int hours = 48;
	int minimum_stations = 2;
};

Arguments parse(int argc, char** argv)
{
	Arguments args;
	args.command = argv[1];
	if (args.command != "inspect" && args.command != "export" && args.command != "verify" && args.command != "build" && args.command != "import" && args.command != "site")
		throw std::invalid_argument("Comando sconosciuto. Usare --help.");
	bool positional = false;
	for (int index = 2; index < argc; ++index) {
		const std::string value = argv[index];
		if (!positional && value == "--") { positional = true; continue; }
		if (!positional && value == "--details" && args.command == "inspect") { args.details = true; continue; }
		if (!positional && (value == "--database" || value == "--raw-dir" || value == "--origin"
			|| value == "--dataset" || value == "--acquired-at" || value == "--hours"
			|| value == "--repository" || value == "--asset-prefix" || value == "--minimum-stations")) {
			if (++index == argc) throw std::invalid_argument("Manca il valore di " + value);
			if (args.command != "import" && args.command != "site") throw std::invalid_argument("Opzione prevista solo per import/site: " + value);
			const std::string parameter=argv[index];
			if (value == "--database") args.database=parameter;
			else if (value == "--raw-dir") args.raw_dir=parameter;
			else if (value == "--origin") args.origin=parameter;
			else if (value == "--dataset") args.dataset=parameter;
			else if (value == "--acquired-at") args.acquired_at=parameter;
			else if (value == "--repository") args.repository=parameter;
			else if (value == "--asset-prefix") args.asset_prefix=parameter;
			else {
				if (parameter.empty() || !std::all_of(parameter.begin(),parameter.end(),[](unsigned char c){return c>='0' && c<='9';}))
					throw std::invalid_argument("Numero intero richiesto: "+value);
				const int n=std::stoi(parameter);
				if (value=="--hours") { if(n<1 || n>8760) throw std::invalid_argument("hours: intervallo 1..8760"); args.hours=n; }
				else { if(n<1) throw std::invalid_argument("Almeno una stazione richiesta"); args.minimum_stations=n; }
			}
			continue;
		}
		if (!positional && (value == "--output" || value == "--station" || value == "--gnuplot")) {
			if (++index == argc) throw std::invalid_argument("Manca il valore di " + value);
			if (value == "--output") {
				if (args.command != "export" && args.command != "build" && args.command != "site") throw std::invalid_argument("--output non e' previsto per questo comando.");
				args.output = argv[index];
			} else {
				if (args.command != "build" && args.command != "site" && args.command != "import") throw std::invalid_argument(value + " e' previsto soltanto per build.");
				if (value == "--gnuplot") args.gnuplot = argv[index];
				else {
					const std::string station = argv[index];
					if (station.empty()) throw std::invalid_argument("--station richiede un identificativo.");
					args.stations.insert(station);
				}
			}
			continue;
		}
		if (!positional && value.starts_with('-')) throw std::invalid_argument("Opzione non riconosciuta: " + value);
		args.files.emplace_back(value);
	}
	if (args.files.empty() && args.command != "site") throw std::invalid_argument("Specificare almeno un file BUFR.");
	if ((args.command == "build" || args.command == "site") && args.output.empty()) throw std::invalid_argument("build richiede --output DIRECTORY.");
	if ((args.command=="import" || args.command=="site") && args.database.empty()) throw std::invalid_argument("Specificare --database");
	if (args.command=="import" && (args.raw_dir.empty() || args.origin.empty() || args.dataset.empty()))
		throw std::invalid_argument("import richiede --raw-dir, --origin e --dataset");
	if (!args.acquired_at.empty() && !std::regex_match(args.acquired_at,std::regex(R"(\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}Z)")))
		throw std::invalid_argument("--acquired-at richiede YYYY-MM-DDTHH:MM:SSZ");
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
		if (std::string(argv[1]) == "--version") { output("bufrom 0.2.0-dev\n"); return EXIT_SUCCESS; }
		if (!std::getenv("WREPORT_EXTRA_TABLES")) {
			const auto tables=std::filesystem::canonical("/proc/self/exe").parent_path().parent_path()/"tables";
			if (std::filesystem::is_directory(tables)) ::setenv("WREPORT_EXTRA_TABLES",tables.c_str(),0);
		}
		const auto args = parse(argc, argv);
		const auto selected=[&](const diario::Observation& o) {
			return args.stations.empty() || args.stations.contains(o.station_id) || args.stations.contains(o.station_key);
		};
		if (args.command=="import") {
			std::vector<diario::Batch> batches;
			std::vector<std::string> warnings;
			for (const auto& file:args.files) {
				diario::Batch batch{file,args.origin,args.dataset,
					args.acquired_at.empty()?diario::utc_now():args.acquired_at,{},0};
				const auto hash=diario::sha256_file(file);
				const auto stats=diario::read_bufr({file},[&](const wreport::BufrBulletin& b,const auto&,std::size_t index) {
					for (auto& o:diario::extract_temperatures(b,hash,index,warnings))
						if(selected(o)) batch.observations.push_back(std::move(o));
				});
				if (diario::sha256_file(file)!=hash) throw std::runtime_error("Input modificato durante la lettura");
				batch.excluded=stats.subsets-batch.observations.size(); batches.push_back(std::move(batch));
			}
			for (const auto& w:warnings) std::fprintf(stderr,"Nota: %s\n",w.c_str());
			const auto c=diario::import_batches(args.database,args.raw_dir,batches);
			output(std::format("{{\"new\":{},\"updated\":{},\"duplicate\":{},\"excluded\":{}}}\n",c.added,c.updated,c.duplicate,c.excluded));
			return EXIT_SUCCESS;
		}
		if (args.command=="site") {
			if (!args.repository.starts_with("https://github.com/")) throw std::invalid_argument("Specificare --repository https://github.com/ACCOUNT/REPO");
			const auto now=std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
			auto observations=diario::load_archive(args.database,now-args.hours*3600);
			std::erase_if(observations,[&](const auto& o){return !selected(o) || o.epoch_seconds>now || o.profile!="meteohub-generic-200-14-1-v1" || o.network!="simnbo";});
			std::set<std::string> stations;
			for (const auto& o:observations) if(o.temperature_set) stations.insert(o.station_key);
			if (stations.size()<static_cast<std::size_t>(args.minimum_stations)) throw std::runtime_error("Stazioni con misure insufficienti nella finestra; pubblicazione rifiutata");
			diario::RenderOptions options{true,args.repository,args.asset_prefix,diario::utc_now()};
			diario::render_diary(observations,{},args.output,args.gnuplot,options);
			return EXIT_SUCCESS;
		}
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
					if (selected(observation)) observations.push_back(std::move(observation));
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
