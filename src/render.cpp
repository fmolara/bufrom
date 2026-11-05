// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "diario/render.hpp"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <locale>
#include <map>
#include <spawn.h>
#include <sstream>
#include <stdexcept>
#include <sys/wait.h>
#include <tuple>
#include <unistd.h>

extern char** environ;

namespace diario {
namespace {

namespace fs = std::filesystem;
using Key = std::tuple<std::string, std::string, std::string, std::string, std::optional<double>, std::string, int>;
using Points = std::vector<const Observation*>;

std::string html(const std::string& value)
{
	std::string result;
	for (unsigned char c : value) {
		switch (c) {
		case '&': result += "&amp;"; break;
		case '<': result += "&lt;"; break;
		case '>': result += "&gt;"; break;
		case '"': result += "&quot;"; break;
		case '\'': result += "&#39;"; break;
		default:
			if (c < 32 && c != '\n' && c != '\t') result += "&#xfffd;";
			else result += static_cast<char>(c);
		}
	}
	return result;
}

std::string csv(const std::string& value)
{
	std::string result = "\"";
	for (char c : value) {
		if (c == '"') result += '"';
		result += c;
	}
	return result + '"';
}

std::string number(double value, int precision = 2)
{
	std::ostringstream out;
	out.imbue(std::locale::classic());
	out << std::fixed << std::setprecision(precision) << value;
	return out.str();
}

std::string height(const std::optional<double>& value)
{
	return value ? number(*value) + " m sopra il suolo" : "altezza del sensore non determinata";
}

std::string source_name(const Observation& obs)
{
	return fs::path(obs.source).filename().string();
}

std::ofstream output(const fs::path& path)
{
	std::ofstream stream(path, std::ios::binary);
	if (!stream) throw std::runtime_error("Impossibile scrivere " + path.string());
	stream.exceptions(std::ios::badbit | std::ios::failbit);
	stream.imbue(std::locale::classic());
	return stream;
}

struct Staging {
	fs::path path;
	bool published = false;
	~Staging()
	{
		if (!published) {
			std::error_code ignored;
			fs::remove_all(path, ignored);
		}
	}
};

void run_gnuplot(const std::string& executable, const fs::path& cwd,
	const std::string& script)
{
	// Resolve executable paths before changing the child's working directory.
	const auto command = fs::path(executable).has_parent_path()
		? fs::absolute(executable).string() : executable;
	posix_spawn_file_actions_t actions;
	int error = posix_spawn_file_actions_init(&actions);
	if (error) throw std::runtime_error(std::string("posix_spawn: ") + std::strerror(error));
	error = posix_spawn_file_actions_addchdir_np(&actions, cwd.c_str());
	if (error) {
		posix_spawn_file_actions_destroy(&actions);
		throw std::runtime_error(std::string("Directory di gnuplot: ") + std::strerror(error));
	}
	char defaults[] = "-d";
	char* args[] = {const_cast<char*>(command.c_str()), defaults,
		const_cast<char*>(script.c_str()), nullptr};
	pid_t child = 0;
	error = posix_spawnp(&child, command.c_str(), &actions, nullptr, args, environ);
	posix_spawn_file_actions_destroy(&actions);
	if (error) throw std::runtime_error("Avvio di gnuplot fallito: " + std::string(std::strerror(error)));
	int status = 0;
	for (;;) {
		if (waitpid(child, &status, 0) >= 0) break;
		if (errno != EINTR) throw std::runtime_error(std::string("waitpid: ") + std::strerror(errno));
	}
	if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
		const auto reason = WIFSIGNALED(status)
			? "segnale " + std::to_string(WTERMSIG(status))
			: "stato " + std::to_string(WEXITSTATUS(status));
		throw std::runtime_error("gnuplot ha fallito per " + script + " (" + reason + ")");
	}
}

void plot(const fs::path& directory, const std::string& basename,
	const Points& points, const std::string& executable)
{
	auto data = output(directory / (basename + ".dat"));
	data << "# unix_seconds_utc temperature_c\n" << std::setprecision(17);
	Points defined;
	for (const auto* obs : points) {
		data << obs->epoch_seconds << ' ';
		if (obs->temperature_set) { data << obs->temperature_c; defined.push_back(obs); }
		else data << "NaN";
		data << '\n';
	}
	if (defined.empty()) return;
	data.close();
	double first = static_cast<double>(points.front()->epoch_seconds);
	double last = static_cast<double>(points.back()->epoch_seconds);
	const double padding = first == last ? 1800 : std::max(60.0, (last - first) / 20);
	const auto extremes = std::minmax_element(defined.begin(), defined.end(),
		[](const Observation* a, const Observation* b) { return a->temperature_c < b->temperature_c; });
	const double low = (*extremes.first)->temperature_c;
	const double high = (*extremes.second)->temperature_c;
	const double y_padding = std::max(0.5, (high - low) / 10);
	auto script = output(directory / (basename + ".gp"));
	// All interpolated strings below are generated filenames, never BUFR text.
	// Run this script from its directory, also after staging has been renamed.
	script << "set encoding utf8\nset terminal svg size 1000,360 dynamic noenhanced\n"
		<< "set output '" << basename << ".svg'\n"
		<< "set title \"Temperatura dell'aria\"\nset xlabel 'Data e ora UTC'\n"
		<< "set ylabel 'Temperatura (°C)'\nset xdata time\nset timefmt '%s'\n"
		<< "set format x \"%d/%m\\n%H:%M\"\nset grid\nunset key\n"
		<< std::setprecision(17) << "set xrange [" << first - padding << ':' << last + padding << "]\n"
		<< "set yrange [" << low - y_padding << ':' << high + y_padding << "]\n"
		<< "plot '" << basename << ".dat' using 1:2 with points pointtype 7 pointsize 0.8 linecolor rgb '#147d92'\n";
	script.close();
	run_gnuplot(executable, directory, basename + ".gp");
	if (!fs::is_regular_file(directory / (basename + ".svg"))
		|| fs::file_size(directory / (basename + ".svg")) == 0)
		throw std::runtime_error("gnuplot non ha prodotto " + basename + ".svg");
}

} // namespace

void render_diary(const std::vector<Observation>& observations,
	const std::vector<std::string>& warnings, const fs::path& output_dir,
	const std::string& gnuplot_executable, const RenderOptions& options)
{
	if (output_dir.empty()) throw std::runtime_error("Specificare una directory di output nuova");
	const auto target = fs::absolute(output_dir).lexically_normal();
	if (fs::exists(target) || fs::is_symlink(target))
		throw std::runtime_error("La directory di output deve essere nuova: " + target.string());
	std::map<Key, Points> series;
	Points ordered;
	for (const auto& obs : observations) {
		if (!std::isfinite(obs.temperature_c)
			|| (obs.sensor_height_m && !std::isfinite(*obs.sensor_height_m)))
			throw std::runtime_error("Osservazione con temperatura o altezza non finita");
		const auto identity=obs.station_key.empty()?obs.network+":"+obs.station_id:obs.station_key;
		const auto level=obs.sensor_height_m?obs.level_context:
			obs.level_context+":"+obs.source+":"+std::to_string(obs.message_index)+":"+std::to_string(obs.subset_index);
		series[{identity, obs.variable_code, obs.profile, level+":"+obs.time_context,
			obs.sensor_height_m, obs.native_unit, obs.native_scale}].push_back(&obs);
		ordered.push_back(&obs);
	}
	const auto by_time = [](const Observation* a, const Observation* b) {
		return std::tie(a->epoch_seconds, a->station_id, a->variable_code, a->message_index, a->subset_index)
			< std::tie(b->epoch_seconds, b->station_id, b->variable_code, b->message_index, b->subset_index);
	};
	std::stable_sort(ordered.begin(), ordered.end(), by_time);
	for (auto& [key, points] : series) std::stable_sort(points.begin(), points.end(), by_time);
	fs::create_directories(target.parent_path());
	std::string pattern = (target.parent_path() / ("." + target.filename().string() + ".tmp-XXXXXX")).string();
	std::vector<char> writable(pattern.begin(), pattern.end());
	writable.push_back('\0');
	const char* created = mkdtemp(writable.data());
	if (!created) throw std::runtime_error(std::string("Creazione dello staging: ") + std::strerror(errno));
	Staging staging{created};
	auto values = output(staging.path / "observations.csv");
	values << "station_id,station_name,timestamp_utc,epoch_seconds,temperature_c,sensor_height_m,variable_code,source,message_index,subset_index,network,station_key,native_value,native_unit,native_scale,level_context,time_context,quality\n"
		<< std::setprecision(17);
	for (const auto* obs : ordered) {
		values << csv(obs->station_id) << ',' << csv(obs->station_name) << ',' << csv(obs->timestamp)
			<< ',' << obs->epoch_seconds << ',';
		if (obs->temperature_set) values << obs->temperature_c;
		values << ',';
		if (obs->sensor_height_m) values << *obs->sensor_height_m;
		values << ',' << csv(obs->variable_code) << ',' << csv(source_name(*obs))
			<< ',' << obs->message_index << ',' << obs->subset_index << ',' << csv(obs->network)
			<< ',' << csv(obs->station_key) << ',';
		if (obs->native_value) values << *obs->native_value;
		values << ',' << csv(obs->native_unit) << ',' << obs->native_scale << ',' << csv(obs->level_context)
			<< ',' << csv(obs->time_context) << ',' << csv(obs->quality) << '\n';
	}
	values.close();
	auto page = output(staging.path / "index.html");
	page << R"HTML(<!doctype html>
<html lang="it"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1">
<title>BufRom — Diario meteo della Romagna</title>
<style>
:root{color-scheme:light;font-family:system-ui,sans-serif;color:#18323e;background:#eef3f5}
body{margin:0}header{background:#123a4a;color:#fff;padding:2.5rem max(1.25rem,calc((100vw - 1100px)/2))}
header p{max-width:64ch;color:#daeaee}h1{font-size:clamp(1.8rem,4vw,2.6rem);margin:.2rem 0}
main{max-width:1100px;margin:auto;padding:1.25rem}h2{margin-top:2rem}h3{margin:.3rem 0}
.card{background:#fff;border:1px solid #cbdbe1;border-radius:12px;padding:1.2rem;margin:1rem 0}
.meta,figcaption{color:#48616c;font-size:.95rem}figure{margin:1rem 0 0}img{display:block;width:100%;height:auto}
a{color:#075d78}a:focus-visible{outline:3px solid #b86712;outline-offset:3px}
.summary{display:flex;flex-wrap:wrap;gap:1.2rem 2rem}.summary div{min-width:7rem}dt{font-size:.85rem;color:#48616c}dd{margin:.2rem 0;font-weight:650}
.scroll{overflow-x:auto}table{border-collapse:collapse;width:100%;font-size:.9rem}th,td{text-align:left;padding:.6rem;border-bottom:1px solid #d9e4e8;white-space:nowrap}
th{background:#eef3f5}caption{text-align:left;padding:.5rem 0}.notice{border-left:4px solid #b86712}li{margin:.5rem 0}footer{padding:1rem 0;color:#48616c}
</style></head><body><header><p>Osservazioni meteorologiche</p><h1>BufRom — Diario meteo della Romagna</h1>
<p>Temperature osservate in Romagna. File statici generati da BUFR con libwreport; tutte le date sono in UTC.</p></header><main>
)HTML";
	if (options.operational) {
		page << "<section class=\"card\"><p>Ultimo aggiornamento riuscito del diario: <strong>"
			<< html(options.generated_utc) << "</strong> (UTC). Le date delle misure sono riportate separatamente.</p>"
			<< "<p>Fonte: <a href=\"https://meteohub.agenziaitaliameteo.it/\">MeteoHub — Agenzia ItaliaMeteo</a>. "
			<< "Rete simnbo, Arpae Emilia-Romagna Idro-Meteo-Clima Service, "
			<< "<a href=\"https://creativecommons.org/licenses/by/4.0/\">CC BY 4.0</a>. "
			<< "<a href=\"" << html(options.repository) << "\">Codice sorgente BufRom</a>.</p>"
			<< "<p>Qualità: nessuna certificazione applicata. I flag eventualmente presenti sono conservati nel CSV e dichiarati non interpretati. "
			<< "I mancanti restano vuoti nel CSV. I grafici mostrano solo punti, senza interpolazione né linee sui buchi.</p></section>";
	} else page << "<section class=\"card notice\"><strong>Esempio / importazione manuale: può contenere osservazioni storiche. Non è il diario operativo.</strong></section>";
	page << "<section class=\"card\"><h2>Contenuto del diario</h2><dl class=\"summary\"><div><dt>Osservazioni</dt><dd>"
		<< observations.size() << "</dd></div><div><dt>Serie distinte</dt><dd>" << series.size() << "</dd></div>";
	if (!ordered.empty()) page << "<div><dt>Prima osservazione UTC</dt><dd>" << html(ordered.front()->timestamp)
		<< "</dd></div><div><dt>Ultima osservazione UTC</dt><dd>" << html(ordered.back()->timestamp) << "</dd></div>";
	page << "</dl><p><a href=\"" << html(options.asset_prefix) << "observations.csv\" download>Scarica tutte le osservazioni in CSV</a></p></section>\n";
	if (observations.empty()) page << "<section class=\"card notice\"><h2>Nessuna osservazione compatibile</h2><p>I file non hanno prodotto temperature utilizzabili con il profilo di estrazione del prototipo. Consultare la diagnostica.</p></section>\n";
	std::size_t index = 0;
	for (const auto& [key, points] : series) {
		const auto basename = "temperature-" + std::to_string(++index);
		const bool has_values=std::any_of(points.begin(),points.end(),[](const auto* o){return o->temperature_set;});
		if (has_values) plot(staging.path, basename, points, gnuplot_executable);
		const auto& first = *points.front();
		const auto label = first.station_name.empty() ? first.station_id : first.station_name;
		page << "<section class=\"card\"><h2>" << html(label) << "</h2><p class=\"meta\">Stazione "
			<< html(first.station_id) << " · Rete " << html(first.network) << " · Descrittore " << html(first.variable_code) << " · " << html(height(first.sensor_height_m))
			<< " · " << html(options.operational ? "temperatura istantanea" : first.time_context)
			<< " · Risoluzione nativa: " << number(std::pow(10.0,-first.native_scale),first.native_scale) << " " << html(first.native_unit)
			<< "</p><p class=\"meta\">Identità della fonte (rete e coordinate): " << html(first.station_key)
			<< "</p><p>Ultima osservazione (UTC): <strong>" << html(points.back()->timestamp) << "</strong>. "
			<< html(points.back()->quality) << ".</p>";
		const auto now=std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
		if (now-points.back()->epoch_seconds>10800) page << "<p class=\"notice\">Dati non recenti: l'ultima osservazione risale a oltre tre ore prima della generazione della pagina.</p>";
		if (!has_values) { page << "<p>Nessuna misura disponibile per questa serie.</p></section>"; continue; }
		page << "<figure><img src=\"" << html(options.asset_prefix) << basename << ".svg\" width=\"1000\" height=\"360\" alt=\"Grafico delle temperature di "
			<< html(label) << "\"><figcaption>" << points.size() << " campioni · " << html(first.timestamp) << " — "
			<< html(points.back()->timestamp) << " (UTC). I punti rappresentano le singole misure.</figcaption></figure></section>\n";
	}
	if (!ordered.empty()) {
		page << "<section class=\"card\"><h2>Osservazioni</h2><div class=\"scroll\"><table><caption>Ultime "
			<< std::min<std::size_t>(ordered.size(), 200) << " osservazioni, dalla più recente. L'esportazione CSV è completa.</caption>"
			<< "<thead><tr><th>Data UTC</th><th>Stazione</th><th>Temperatura °C</th><th>Altezza m</th><th>Descrittore</th><th>" << (options.operational ? "Qualità" : "File di origine") << "</th></tr></thead><tbody>";
		for (std::size_t i = 0; i < std::min<std::size_t>(ordered.size(), 200); ++i) {
			const auto& obs = *ordered[ordered.size()-1-i];
			page << "<tr><td>" << html(obs.timestamp) << "</td><td>" << html(obs.station_id) << "</td><td>"
				<< (obs.temperature_set ? number(obs.temperature_c) : "mancante") << "</td><td>" << (obs.sensor_height_m ? number(*obs.sensor_height_m) : "non determinata")
				<< "</td><td>" << html(obs.variable_code) << "</td><td>" << html(options.operational ? obs.quality : source_name(obs)) << "</td></tr>";
		}
		page << "</tbody></table></div></section>\n";
	}
	if (!options.operational && !warnings.empty()) {
		page << "<section class=\"card notice\"><h2>Diagnostica dell'importazione</h2><p>" << warnings.size()
			<< " segnalazioni relative ad assunzioni o esclusioni. Le osservazioni non interpretabili non sono incluse nei grafici.</p><details><summary>Mostra le segnalazioni</summary><ul>";
		for (std::size_t i = 0; i < std::min<std::size_t>(warnings.size(), 100); ++i) page << "<li>" << html(warnings[i]) << "</li>";
		if (warnings.size() > 100) page << "<li>Mostrate le prime 100 segnalazioni; consultare anche l'output del programma.</li>";
		page << "</ul></details></section>\n";
	}
	page << "<footer>Le serie distinguono stazione, descrittore e altezza del sensore. Un'altezza non determinata rimane tale.</footer></main></body></html>\n";
	page.close();
	// Explicit permissions make the generated directory readable by Apache even
	// when the generating user's umask is restrictive. Only our staging is changed.
	fs::permissions(staging.path, fs::perms::owner_all | fs::perms::group_read | fs::perms::group_exec | fs::perms::others_read | fs::perms::others_exec);
	for (const auto& entry : fs::directory_iterator(staging.path)) fs::permissions(entry.path(), fs::perms::owner_read | fs::perms::owner_write | fs::perms::group_read | fs::perms::others_read);
	if (fs::exists(target) || fs::is_symlink(target)) throw std::runtime_error("La destinazione è stata creata da un altro processo: " + target.string());
	fs::rename(staging.path, target);
	staging.published = true;
}

} // namespace diario
