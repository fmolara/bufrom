// SPDX-License-Identifier: GPL-2.0-or-later
#include "diario/bufr.hpp"

#include <wreport/subset.h>
#include <wreport/var.h>
#include <wreport/varinfo.h>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <format>
#include <memory>
#include <stdexcept>

namespace diario {
namespace {

struct FileCloser {
	void operator()(FILE* file) const noexcept { if (file) std::fclose(file); }
};

std::string row(const wreport::Var& variable, const std::string& source,
	std::size_t message_index, std::size_t subset_index, std::size_t position,
	const std::string& kind, const std::string& parent_code)
{
	const auto info = variable.info();
	return std::format("{},{},{},{},{},{},{},{},{},{},{},{}\n",
		csv_field(source), message_index, subset_index, position,
		kind, parent_code, wreport::varcode_format(variable.code()),
		variable.isset() ? "1" : "0",
		csv_field(variable.isset() ? variable.format() : ""),
		csv_field(info->unit), info->scale, csv_field(info->desc));
}

} // namespace

InputStats read_bufr(const std::vector<std::filesystem::path>& files,
	const BulletinVisitor& visitor)
{
	if (files.empty()) throw std::invalid_argument("Specificare almeno un file BUFR.");
	auto options = wreport::BufrCodecOptions::create();
	options->decode_adds_undef_attrs = true;
	InputStats stats;
	for (const auto& path : files) {
		const std::string filename = path.string();
		std::unique_ptr<FILE, FileCloser> input(std::fopen(filename.c_str(), "rb"));
		if (!input) throw std::runtime_error(std::format("{}: {}", filename, std::strerror(errno)));
		std::string encoded;
		off_t offset = 0;
		std::size_t message_index = 0;
		try {
			for (;;) {
				if (!wreport::BufrBulletin::read(input.get(), encoded, filename.c_str(), &offset)) break;
				if (offset < 0) throw std::runtime_error("Offset BUFR negativo.");
				auto bulletin = wreport::BufrBulletin::decode(
					encoded, *options, filename.c_str(), static_cast<std::size_t>(offset));
				visitor(*bulletin, path, message_index);
				++message_index;
				++stats.messages;
				stats.subsets += bulletin->subsets.size();
				for (const auto& subset : bulletin->subsets) stats.variables += subset.size();
			}
			if (std::ferror(input.get())) throw std::runtime_error("Errore di lettura del file.");
			if (message_index == 0) throw std::runtime_error("Nessun messaggio BUFR trovato.");
		} catch (const std::exception& error) {
			throw std::runtime_error(std::format("{} (messaggio {}, offset {}): {}",
				filename, message_index, static_cast<long long>(offset), error.what()));
		}
		++stats.files;
	}
	return stats;
}

std::string csv_field(const std::string& value)
{
	std::string result = "\"";
	for (char ch : value) {
		if (ch == '"') result += '"';
		result += ch;
	}
	result += '"';
	return result;
}

std::string raw_csv_header()
{
	return "source,message_index,subset_index,position,kind,parent_code,code,is_set,value,unit,scale,description\n";
}

std::string raw_csv_rows(const wreport::BufrBulletin& bulletin,
	const std::string& source, std::size_t message_index)
{
	std::string result;
	for (std::size_t subset_index = 0; subset_index < bulletin.subsets.size(); ++subset_index) {
		const auto& subset = bulletin.subsets[subset_index];
		for (std::size_t position = 0; position < subset.size(); ++position) {
			const auto& variable = subset[position];
			const auto code = wreport::varcode_format(variable.code());
			result += row(variable, source, message_index, subset_index, position, "variable", "");
			for (const wreport::Var* attribute = variable.next_attr(); attribute; attribute = attribute->next_attr()) {
				result += row(*attribute, source, message_index, subset_index, position, "attribute", code);
			}
		}
	}
	return result;
}

} // namespace diario

