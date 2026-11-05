// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <wreport/bulletin.h>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace diario {

struct InputStats {
	std::size_t files = 0;
	std::size_t messages = 0;
	std::size_t subsets = 0;
	std::size_t variables = 0;
};

// Il bollettino appartiene al lettore ed e' valido soltanto durante la callback.
using BulletinVisitor = std::function<void(
	const wreport::BufrBulletin&, const std::filesystem::path&, std::size_t)>;

InputStats read_bufr(const std::vector<std::filesystem::path>& files,
	const BulletinVisitor& visitor);

std::string csv_field(const std::string& value);
std::string raw_csv_header();
std::string raw_csv_rows(const wreport::BufrBulletin& bulletin,
	const std::string& source, std::size_t message_index);

} // namespace diario

