// SPDX-License-Identifier: GPL-2.0-or-later
#include "diario/archive.hpp"
#include "diario/bufr.hpp"
#include <wreport/bulletin.h>
#include <wreport/subset.h>
#include <wreport/var.h>
#include <sqlite3.h>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <unistd.h>

namespace fs=std::filesystem;
void require(bool ok,const std::string& text) { if(!ok) throw std::runtime_error(text); }
struct Temp {
	fs::path path;
	Temp() { char p[]="/tmp/bufrom-archive-XXXXXX"; auto* out=mkdtemp(p); if(!out) throw std::runtime_error("mkdtemp"); path=out; }
	~Temp() { fs::remove_all(path); }
};
diario::Batch batch(const fs::path& path,const std::string& acquired) {
	diario::Batch b{path,"https://meteohub.agenziaitaliameteo.it/api/observations","simnbo",acquired,{},0};
	std::vector<std::string> warnings;
	diario::read_bufr({path},[&](const auto& bulletin,const auto&,std::size_t index) {
		for(auto& o:diario::extract_temperatures(bulletin,diario::sha256_file(path),index,warnings)) b.observations.push_back(std::move(o));
	});
	require(warnings.empty(),"nessun avviso atteso per fixture MeteoHub"); return b;
}
void rewrite(const fs::path& input,const fs::path& output,const std::function<void(wreport::BufrBulletin&,std::size_t)>& change) {
	std::ofstream out(output,std::ios::binary);
	diario::read_bufr({input},[&](const auto& bulletin,const auto&,std::size_t index) {
		auto copy=wreport::BufrBulletin::decode(bulletin.encode());
		change(*copy,index); const auto bytes=copy->encode(); out.write(bytes.data(),bytes.size());
	});
}
int scalar(const fs::path& path,const char* sql) {
	sqlite3* db=nullptr; require(sqlite3_open_v2(path.c_str(),&db,SQLITE_OPEN_READONLY,nullptr)==SQLITE_OK,"SQLite test open");
	sqlite3_stmt* stmt=nullptr; require(sqlite3_prepare_v2(db,sql,-1,&stmt,nullptr)==SQLITE_OK,"SQLite test prepare");
	require(sqlite3_step(stmt)==SQLITE_ROW,"SQLite test step"); int n=sqlite3_column_int(stmt,0);
	sqlite3_finalize(stmt); sqlite3_close(db); return n;
}
int main(int argc,char** argv) {
	try {
		require(argc==2 || argc==3,"Specificare fixture [fixture-temporanea-attuale]"); const fs::path fixture=argv[1];
		Temp work; const auto db=work.path/"archive.sqlite",raw=work.path/"raw";
		auto first=batch(fixture,"2026-11-05T12:00:00Z");
		require(first.observations.size()==4,"quattro osservazioni reali");
		const auto& o=first.observations.front();
		require(o.network=="simnbo" && o.station_name=="Forlì Urbana","rete e nome originali");
		require(o.station_key=="simnbo:44.22039,12.04182","identità della fonte con coordinate");
		require(o.timestamp=="2026-11-04T00:00:00Z" && o.sensor_height_m==2.0,"UTC e livello");
		require(o.native_unit=="K" && o.native_scale==2 && std::abs(*o.native_value-283.45)<1e-7,"unità e risoluzione reali");
		require(o.time_context=="254,0,0 (istantanea)" && o.quality.find("nessun flag")!=std::string::npos,"tempo e qualità espliciti");
		require(first.observations[2].station_name=="Bertinoro" && first.observations[2].station_key=="simnbo:44.15206,12.14217","seconda stazione reale");
		auto c=diario::import_batches(db,raw,{first}); require(c.added==4,"primo import");
		c=diario::import_batches(db,raw,{first}); require(c.duplicate==4 && c.added==0,"reimport idempotente");
		require(diario::load_archive(db,0).size()==4,"archivio persistente dopo riapertura");
		rewrite(fixture,work.path/"corrected.bufr",[](auto& b,std::size_t index){if(index==0)b.subsets[0][17].setd(284.45);});
		auto correction=batch(work.path/"corrected.bufr","2026-11-05T13:00:00Z");
		c=diario::import_batches(db,raw,{correction}); require(c.updated==1 && c.duplicate==3,"rettifica della fonte");
		require(scalar(db,"SELECT count(*) FROM revisions")==5,"revisione originale conservata");
		c=diario::import_batches(db,raw,{first}); require(c.duplicate==4,"vecchio lotto noto non ripristina il valore precedente");
		auto saved=diario::load_archive(db,0);
		for(const auto& row:saved) if(row.station_key==o.station_key && row.timestamp==o.timestamp)
			require(row.native_value==284.45 && row.source==diario::sha256_file(correction.file),"valore attuale e hash della rettifica");
		rewrite(fixture,work.path/"other-network.bufr",[](auto& b,std::size_t){b.subsets[0][0].sets("other-network");});
		c=diario::import_batches(db,raw,{batch(work.path/"other-network.bufr","2026-11-05T14:00:00Z")});
		require(c.added==4 && diario::load_archive(db,0).size()==8,"nessuna collisione fra reti");
		rewrite(fixture,work.path/"missing.bufr",[](auto& b,std::size_t i){if(i==0)b.subsets[0][17].unset();});
		auto missing=batch(work.path/"missing.bufr","2026-11-05T15:00:00Z");
		require(!missing.observations[0].temperature_set && !missing.observations[0].native_value,"mancante preservato");
		c=diario::import_batches(db,raw,{missing}); require(c.updated==1,"rettifica a valore mancante");
		rewrite(fixture,work.path/"older.bufr",[](auto& b,std::size_t i){if(i==0)b.subsets[0][17].setd(280.45);});
		const int revisions=scalar(db,"SELECT count(*) FROM revisions");
		bool rejected=false;
		try { diario::import_batches(db,raw,{batch(work.path/"older.bufr","2026-11-05T11:00:00Z")}); }
		catch(const std::exception&) { rejected=true; }
		require(rejected && scalar(db,"SELECT count(*) FROM revisions")==revisions,"acquisizione fuori ordine rifiutata con rollback");
		// The first duplicate equals the current value: the second conflicts.
		// This must still reject the complete batch, independent of current state.
		{
			std::ofstream conflict(work.path/"conflict.bufr",std::ios::binary);
			diario::read_bufr({fixture},[&](const auto& original,const auto&,std::size_t index) {
				if(index!=1)return;
				const auto encoded=original.encode(); conflict.write(encoded.data(),encoded.size());
				auto changed=wreport::BufrBulletin::decode(encoded); changed->subsets[0][17].setd(290.0);
				const auto altered=changed->encode(); conflict.write(altered.data(),altered.size());
			});
		}
		rejected=false;
		try { diario::import_batches(db,raw,{batch(work.path/"conflict.bufr","2026-11-05T16:00:00Z")}); }
		catch(const std::exception&) {rejected=true;}
		require(rejected && scalar(db,"SELECT count(*) FROM revisions")==revisions,"conflitto fra ripetizioni causa rollback");
		// Contradictory repeated measurements must never be silently flattened.
		diario::read_bufr({fixture},[](const auto& original,const auto&,std::size_t index) {
			if(index!=0) return;
			auto b=wreport::BufrBulletin::decode(original.encode());
			b->datadesc.push_back(WR_VAR(0,12,101));
			std::vector<std::string> warnings;
			require(diario::extract_temperatures(*b,"test",0,warnings).empty() && !warnings.empty(),"template modificato rifiutato");
		});
		std::ofstream html(work.path/"error.bufr"); html<<"<html>upstream error</html>";html.close();
		rejected=false; try { diario::read_bufr({work.path/"error.bufr"},[](const auto&,const auto&,auto){}); } catch(const std::exception&) {rejected=true;}
		require(rejected,"HTML non trattato come BUFR");
		if (argc==3) rewrite(fixture,argv[2],[](auto& b,std::size_t index) {
			const std::time_t timestamp=std::time(nullptr)-3600+(index%2)*900;
			std::tm tm{}; gmtime_r(&timestamp,&tm);
			const int fields[]={tm.tm_year+1900,tm.tm_mon+1,tm.tm_mday,tm.tm_hour,tm.tm_min,tm.tm_sec};
			for(int i=0;i<6;++i)b.subsets[0][i+1].seti(fields[i]);
		});
		std::cout<<"OK: profilo MeteoHub, UTC/livello/unità, reti, mancanti, archivio persistente, deduplica, rettifiche, provenienza e rollback\n";
		return 0;
	} catch(const std::exception& e) {std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
}
