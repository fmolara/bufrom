// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "diario/observations.hpp"
#include <filesystem>
#include <string>
#include <vector>

namespace diario {
struct Batch {
	std::filesystem::path file;
	std::string origin;
	std::string dataset;
	std::string acquired_utc;
	std::vector<Observation> observations;
	std::size_t excluded = 0;
};
struct ImportCounts {
	std::size_t added=0, updated=0, duplicate=0, excluded=0;
};
std::string utc_now();
std::string sha256_file(const std::filesystem::path&);
ImportCounts import_batches(const std::filesystem::path& database,
	const std::filesystem::path& raw_directory, const std::vector<Batch>& batches);
std::vector<Observation> load_archive(const std::filesystem::path& database,
	std::int64_t since_epoch);
} // namespace diario
