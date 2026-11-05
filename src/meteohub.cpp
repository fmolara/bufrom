// SPDX-License-Identifier: GPL-2.0-or-later
#include "diario/observations.hpp"
#include <wreport/bulletin.h>
#include <wreport/conv.h>
#include <wreport/subset.h>
#include <wreport/var.h>
#include <wreport/varinfo.h>
#include <chrono>
#include <cmath>
#include <format>
#include <stdexcept>

namespace diario {
std::vector<Observation> extract_meteohub(const wreport::BufrBulletin& bulletin,
	const std::string& source, std::size_t message_index, std::vector<std::string>& warnings)
{
	// This is the exact expanded template observed on the public map export.
	// No map by descriptor: each field has a single checked position/context.
	const std::vector<wreport::Varcode> expected{
		WR_VAR(0,1,194), WR_VAR(0,4,1), WR_VAR(0,4,2), WR_VAR(0,4,3),
		WR_VAR(0,4,4), WR_VAR(0,4,5), WR_VAR(0,4,6), WR_VAR(0,1,19),
		WR_VAR(0,5,1), WR_VAR(0,6,1), WR_VAR(0,7,30), WR_VAR(0,7,31),
		WR_VAR(0,7,192), WR_VAR(0,7,193), WR_VAR(0,4,192),
		WR_VAR(0,4,193), WR_VAR(0,4,194), WR_VAR(0,12,101)};
	const auto where = source + ": messaggio " + std::to_string(message_index);
	if (bulletin.edition_number != 4 || bulletin.data_category != 255
		|| bulletin.originating_centre != 200 || bulletin.originating_subcentre != 0
		|| bulletin.master_table_number != 0 || bulletin.master_table_version_number != 14
		|| bulletin.master_table_version_number_local != 1 || bulletin.datadesc != expected) {
		warnings.push_back(where + ": template/tabelle MeteoHub non supportati; usare inspect --details.");
		return {};
	}
	std::vector<Observation> result;
	for (std::size_t index = 0; index < bulletin.subsets.size(); ++index) {
		try {
			const auto& s = bulletin.subsets[index];
			if (s.size() != expected.size()) throw std::runtime_error("numero di variabili inatteso");
			for (std::size_t pos = 0; pos < s.size(); ++pos) {
				if (s[pos].code() != expected[pos]) throw std::runtime_error("ordine delle variabili inatteso");
				if (pos != 10 && pos != 11 && pos != 17 && !s[pos].isset())
					throw std::runtime_error("identità, data o contesto indefiniti");
			}
			if (s[12].enqi() != 103 || s[13].enqi() != 2000
				|| s[14].enqi() != 254 || s[15].enqi() != 0 || s[16].enqi() != 0)
				throw std::runtime_error("richiesti livello 103,2000 e tempo 254,0,0");
			if (std::string(s[17].info()->unit) != "K" || s[17].info()->scale != 2)
				throw std::runtime_error("unità o risoluzione B12101 inattese");
			const int y=s[1].enqi(), m=s[2].enqi(), d=s[3].enqi();
			const int h=s[4].enqi(), min=s[5].enqi(), sec=s[6].enqi();
			const std::chrono::year_month_day date{std::chrono::year{y},
				std::chrono::month{static_cast<unsigned>(m)}, std::chrono::day{static_cast<unsigned>(d)}};
			if (y<1 || y>9999 || !date.ok() || h<0 || h>23 || min<0 || min>59 || sec<0 || sec>59)
				throw std::runtime_error("data UTC non valida");
			Observation o;
			o.network = s[0].enqs();
			o.station_id = o.station_name = s[7].enqs();
			for (auto* text : {&o.network, &o.station_id, &o.station_name}) {
				const auto last = text->find_last_not_of(" \t\r\n");
				if (last == std::string::npos) throw std::runtime_error("rete/nome stazione vuoto");
				text->resize(last+1);
			}
			o.latitude=s[8].enqd(); o.longitude=s[9].enqd();
			if (!std::isfinite(*o.latitude) || !std::isfinite(*o.longitude)
				|| std::abs(*o.latitude)>90 || std::abs(*o.longitude)>180)
				throw std::runtime_error("coordinate non valide");
			// Fixed stations in this source have no separate numeric identifier.
			// Preserve the source name; identity is its network + exact coordinates.
			o.station_key=std::format("{}:{:.5f},{:.5f}", o.network, *o.latitude, *o.longitude);
			o.timestamp=std::format("{:04}-{:02}-{:02}T{:02}:{:02}:{:02}Z",y,m,d,h,min,sec);
			o.epoch_seconds=(std::chrono::sys_days{date}+std::chrono::hours{h}
				+std::chrono::minutes{min}+std::chrono::seconds{sec}).time_since_epoch().count();
			o.sensor_height_m=2.0;
			if (s[10].isset()) o.ground_height_m=s[10].enqd();
			o.profile="meteohub-generic-200-14-1-v1";
			o.level_context="103,2000,-,-";
			o.time_context="254,0,0 (istantanea)";
			o.native_scale=2;
			o.temperature_set=s[17].isset();
			if (o.temperature_set) {
				o.native_value=s[17].enqd();
				o.temperature_c=wreport::convert_units("K","C",*o.native_value);
				if (!std::isfinite(o.temperature_c)) throw std::runtime_error("temperatura non finita");
			}
			o.quality="nessun flag presente; dato non certificato";
			if (s[17].next_attr()) {
				o.quality="attributi non interpretati:";
				for (auto* a=s[17].next_attr();a;a=a->next_attr())
					o.quality += " " + wreport::varcode_format(a->code()) + "=" + (a->isset()?a->format():"mancante");
			}
			o.variable_code="B12101"; o.source=source; o.message_index=message_index;
			o.subset_index=index; o.position=17;
			result.push_back(std::move(o));
		} catch (const std::exception& e) {
			warnings.push_back(where+", subset "+std::to_string(index)+": "+e.what()+"; escluso.");
		}
	}
	return result;
}
} // namespace diario
