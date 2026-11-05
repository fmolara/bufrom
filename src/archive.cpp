// SPDX-License-Identifier: GPL-2.0-or-later
#include "diario/archive.hpp"
#include <sqlite3.h>
#include <openssl/evp.h>
#include <array>
#include <chrono>
#include <format>
#include <fstream>
#include <memory>
#include <map>
#include <stdexcept>
#include <unistd.h>

namespace diario {
namespace {
namespace fs=std::filesystem;
struct Database {
	sqlite3* db=nullptr;
	explicit Database(const fs::path& path, int flags) {
		if (sqlite3_open_v2(path.c_str(), &db, flags, nullptr)!=SQLITE_OK) {
			const std::string error=db?sqlite3_errmsg(db):"apertura fallita";
			if (db) sqlite3_close(db);
			throw std::runtime_error("SQLite: "+error);
		}
		sqlite3_busy_timeout(db,10000);
	}
	~Database() { sqlite3_close(db); }
	void exec(const char* sql) {
		char* error=nullptr;
		if (sqlite3_exec(db,sql,nullptr,nullptr,&error)!=SQLITE_OK) {
			const std::string text=error?error:sqlite3_errmsg(db);
			sqlite3_free(error); throw std::runtime_error("SQLite: "+text);
		}
	}
};
struct Statement {
	sqlite3_stmt* s=nullptr;
	sqlite3* db;
	Statement(Database& d, const char* sql):db(d.db) {
		if (sqlite3_prepare_v2(db,sql,-1,&s,nullptr)!=SQLITE_OK) fail();
	}
	~Statement() { sqlite3_finalize(s); }
	[[noreturn]] void fail() { throw std::runtime_error("SQLite: "+std::string(sqlite3_errmsg(db))); }
	void check(int rc) { if (rc!=SQLITE_OK) fail(); }
	void bind(int i,const std::string& v) { check(sqlite3_bind_text(s,i,v.c_str(),-1,SQLITE_TRANSIENT)); }
	void bind(int i,std::int64_t v) { check(sqlite3_bind_int64(s,i,v)); }
	void bind(int i,double v) { check(sqlite3_bind_double(s,i,v)); }
	void bind(int i,const std::optional<double>& v) {
		if (v) bind(i,*v); else check(sqlite3_bind_null(s,i));
	}
	bool row() {
		int rc=sqlite3_step(s); if (rc==SQLITE_ROW) return true;
		if (rc!=SQLITE_DONE) fail();
		return false;
	}
	std::string text(int i) const {
		const auto* v=sqlite3_column_text(s,i); return v?reinterpret_cast<const char*>(v):"";
	}
	std::int64_t integer(int i) const { return sqlite3_column_int64(s,i); }
	std::optional<double> real(int i) const {
		if (sqlite3_column_type(s,i)==SQLITE_NULL) return {};
		return sqlite3_column_double(s,i);
	}
};
std::string framed(const std::string& s) { return std::to_string(s.size())+":"+s; }
std::string key(const Observation& o) {
	std::string level=o.level_context;
	if (o.profile=="wmo-surface-legacy") {
		level=o.sensor_height_m?std::format("height={}",*o.sensor_height_m)
			: "unknown:"+o.source+":"+std::to_string(o.message_index)+":"+std::to_string(o.subset_index);
	}
	return framed(o.profile)+framed(o.station_key)+framed(o.variable_code)
		+framed(o.timestamp)+framed(level)+framed(o.time_context);
}
std::string content(const Observation& o) {
	return framed(o.station_id)+framed(o.station_name)+framed(o.quality)
		+framed(o.native_unit)+framed(std::to_string(o.native_scale))
		+framed(o.native_value?std::format("{:.17g}",*o.native_value):"missing")
		+framed(o.ground_height_m?std::format("{:.17g}",*o.ground_height_m):"unknown");
}
void schema(Database& d) {
	d.exec(R"SQL(
PRAGMA foreign_keys=ON;
CREATE TABLE IF NOT EXISTS schema_version(version INTEGER PRIMARY KEY CHECK(version=1));
INSERT OR IGNORE INTO schema_version VALUES(1);
CREATE TABLE IF NOT EXISTS batches(
 hash TEXT PRIMARY KEY, origin TEXT NOT NULL, dataset TEXT NOT NULL,
 acquired_utc TEXT NOT NULL, raw_path TEXT NOT NULL, observations INTEGER NOT NULL);
CREATE TABLE IF NOT EXISTS revisions(
 id INTEGER PRIMARY KEY, obs_key TEXT NOT NULL, content TEXT NOT NULL,
 hash TEXT NOT NULL REFERENCES batches(hash), message_index INTEGER NOT NULL,
 subset_index INTEGER NOT NULL, position INTEGER NOT NULL,
 acquired_utc TEXT NOT NULL, network TEXT NOT NULL, station_id TEXT NOT NULL,
 station_name TEXT NOT NULL, station_key TEXT NOT NULL, latitude REAL, longitude REAL,
 ground_height_m REAL, timestamp TEXT NOT NULL, epoch_seconds INTEGER NOT NULL,
 temperature_c REAL, native_value REAL, native_unit TEXT NOT NULL, native_scale INTEGER NOT NULL,
 sensor_height_m REAL, variable_code TEXT NOT NULL, profile TEXT NOT NULL,
 level_context TEXT NOT NULL, time_context TEXT NOT NULL, quality TEXT NOT NULL);
CREATE TABLE IF NOT EXISTS current_observations(
 obs_key TEXT PRIMARY KEY, revision_id INTEGER NOT NULL REFERENCES revisions(id));
CREATE INDEX IF NOT EXISTS revisions_time ON revisions(epoch_seconds);
)SQL");
}
} // namespace

std::string utc_now() {
	const auto now=std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now());
	return std::format("{:%FT%TZ}",now);
}
std::string sha256_file(const fs::path& path) {
	std::ifstream in(path,std::ios::binary);
	if (!in) throw std::runtime_error("Impossibile leggere "+path.string());
	std::unique_ptr<EVP_MD_CTX,decltype(&EVP_MD_CTX_free)> ctx(EVP_MD_CTX_new(),EVP_MD_CTX_free);
	if (!ctx || EVP_DigestInit_ex(ctx.get(),EVP_sha256(),nullptr)!=1) throw std::runtime_error("SHA256 init fallita");
	std::array<char,65536> buffer;
	while (in) {
		in.read(buffer.data(),buffer.size());
		if (in.gcount() && EVP_DigestUpdate(ctx.get(),buffer.data(),in.gcount())!=1)
			throw std::runtime_error("SHA256 update fallito");
	}
	if (in.bad()) throw std::runtime_error("Errore di lettura per SHA256");
	unsigned char digest[EVP_MAX_MD_SIZE]; unsigned size=0;
	if (EVP_DigestFinal_ex(ctx.get(),digest,&size)!=1) throw std::runtime_error("SHA256 final fallito");
	std::string out; for (unsigned i=0;i<size;++i) out+=std::format("{:02x}",digest[i]); return out;
}
ImportCounts import_batches(const fs::path& database, const fs::path& raw_directory,
	const std::vector<Batch>& batches)
{
	if (batches.empty()) throw std::runtime_error("Nessun lotto");
	if (!database.parent_path().empty()) fs::create_directories(database.parent_path());
	fs::create_directories(raw_directory);
	fs::permissions(raw_directory,fs::perms::owner_all);
	Database d(database,SQLITE_OPEN_READWRITE|SQLITE_OPEN_CREATE);
	fs::permissions(database,fs::perms::owner_read|fs::perms::owner_write);
	schema(d);
	ImportCounts count;
	d.exec("BEGIN IMMEDIATE");
	try {
		for (const auto& batch:batches) {
			if (batch.observations.empty()) throw std::runtime_error("Lotto senza osservazioni supportate");
			const auto hash=sha256_file(batch.file);
			count.excluded+=batch.excluded;
			Statement seen(d,"SELECT observations FROM batches WHERE hash=?"); seen.bind(1,hash);
			if (seen.row()) { count.duplicate+=batch.observations.size(); continue; }
			const auto raw=fs::absolute(raw_directory/(hash+".bufr"));
			if (!fs::exists(raw)) {
				std::string pattern=(raw_directory/("."+hash+"-XXXXXX")).string();
				std::vector<char> temp(pattern.begin(),pattern.end()); temp.push_back('\0');
				const int fd=mkstemp(temp.data());
				if (fd<0) throw std::runtime_error("Creazione BUFR temporaneo fallita");
				close(fd);
				const fs::path staging=temp.data();
				try {
					fs::copy_file(batch.file,staging,fs::copy_options::overwrite_existing);
					fs::permissions(staging,fs::perms::owner_read|fs::perms::owner_write);
					if(sha256_file(staging)!=hash) throw std::runtime_error("Input BUFR modificato durante l'archiviazione");
					fs::rename(staging,raw);
				} catch (...) { fs::remove(staging); throw; }
			}
			if (sha256_file(raw)!=hash) throw std::runtime_error("Hash del BUFR archiviato discordante");
			Statement receipt(d,"INSERT INTO batches VALUES(?,?,?,?,?,?)");
			receipt.bind(1,hash); receipt.bind(2,batch.origin); receipt.bind(3,batch.dataset);
			receipt.bind(4,batch.acquired_utc); receipt.bind(5,raw.string());
			receipt.bind(6,static_cast<std::int64_t>(batch.observations.size())); receipt.row();
			std::map<std::string,std::string> batch_values;
			for (auto o:batch.observations) {
				o.source=hash;
				const auto k=key(o), v=content(o);
				const auto [it,inserted]=batch_values.emplace(k,v);
				if (!inserted && it->second!=v) throw std::runtime_error("Misure discordanti nello stesso lotto: "+o.station_key+" "+o.timestamp);
			}
			for (const auto& input:batch.observations) {
				auto o=input; o.source=hash;
				const auto k=key(o), value=content(o);
				Statement old(d,"SELECT r.content,r.acquired_utc,r.hash FROM current_observations c JOIN revisions r ON r.id=c.revision_id WHERE c.obs_key=?");
				old.bind(1,k); const bool exists=old.row();
				if (exists && old.text(0)==value) { ++count.duplicate; continue; }
				if (exists && old.text(2)==hash) throw std::runtime_error("Valori in conflitto nello stesso lotto: "+o.station_key+" "+o.timestamp);
				if (exists && old.text(1)>=batch.acquired_utc)
					throw std::runtime_error("Rettifica con acquisizione non successiva: import rifiutato");
				Statement row(d,"INSERT INTO revisions(obs_key,content,hash,message_index,subset_index,position,acquired_utc,network,station_id,station_name,station_key,latitude,longitude,ground_height_m,timestamp,epoch_seconds,temperature_c,native_value,native_unit,native_scale,sensor_height_m,variable_code,profile,level_context,time_context,quality) VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)");
				row.bind(1,k); row.bind(2,value); row.bind(3,hash);
				row.bind(4,static_cast<std::int64_t>(o.message_index)); row.bind(5,static_cast<std::int64_t>(o.subset_index));
				row.bind(6,static_cast<std::int64_t>(o.position)); row.bind(7,batch.acquired_utc);
				row.bind(8,o.network); row.bind(9,o.station_id); row.bind(10,o.station_name); row.bind(11,o.station_key);
				row.bind(12,o.latitude); row.bind(13,o.longitude); row.bind(14,o.ground_height_m);
				row.bind(15,o.timestamp); row.bind(16,o.epoch_seconds);
				row.bind(17,o.temperature_set?std::optional<double>{o.temperature_c}:std::nullopt);
				row.bind(18,o.native_value); row.bind(19,o.native_unit); row.bind(20,static_cast<std::int64_t>(o.native_scale));
				row.bind(21,o.sensor_height_m); row.bind(22,o.variable_code); row.bind(23,o.profile);
				row.bind(24,o.level_context); row.bind(25,o.time_context); row.bind(26,o.quality); row.row();
				const std::int64_t id=sqlite3_last_insert_rowid(d.db);
				Statement current(d,"INSERT INTO current_observations VALUES(?,?) ON CONFLICT(obs_key) DO UPDATE SET revision_id=excluded.revision_id");
				current.bind(1,k); current.bind(2,id); current.row();
				if (exists) ++count.updated; else ++count.added;
			}
		}
		d.exec("COMMIT");
	} catch (...) { d.exec("ROLLBACK"); throw; }
	return count;
}
std::vector<Observation> load_archive(const fs::path& database, std::int64_t since_epoch)
{
	Database d(database,SQLITE_OPEN_READONLY);
	Statement q(d,"SELECT r.network,r.station_id,r.station_name,r.station_key,r.latitude,r.longitude,r.ground_height_m,r.timestamp,r.epoch_seconds,r.temperature_c,r.native_value,r.native_unit,r.native_scale,r.sensor_height_m,r.variable_code,r.profile,r.level_context,r.time_context,r.quality,r.hash,r.message_index,r.subset_index,r.position FROM current_observations c JOIN revisions r ON r.id=c.revision_id WHERE r.epoch_seconds>=? ORDER BY r.epoch_seconds,r.station_key");
	q.bind(1,since_epoch); std::vector<Observation> result;
	while(q.row()) {
		Observation o; o.network=q.text(0); o.station_id=q.text(1); o.station_name=q.text(2); o.station_key=q.text(3);
		o.latitude=q.real(4); o.longitude=q.real(5); o.ground_height_m=q.real(6); o.timestamp=q.text(7);
		o.epoch_seconds=q.integer(8); o.temperature_set=q.real(9).has_value(); o.temperature_c=q.real(9).value_or(0);
		o.native_value=q.real(10); o.native_unit=q.text(11); o.native_scale=q.integer(12);
		o.sensor_height_m=q.real(13); o.variable_code=q.text(14); o.profile=q.text(15);
		o.level_context=q.text(16); o.time_context=q.text(17); o.quality=q.text(18); o.source=q.text(19);
		o.message_index=q.integer(20); o.subset_index=q.integer(21); o.position=q.integer(22); result.push_back(std::move(o));
	}
	return result;
}
} // namespace diario
