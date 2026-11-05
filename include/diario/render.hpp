// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "diario/observations.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace diario {

// output_dir must not already exist. The complete directory is published by
// renaming a private staging directory only after every plot has succeeded.
void render_diary(const std::vector<Observation>& observations,
	const std::vector<std::string>& warnings,
	const std::filesystem::path& output_dir,
	const std::string& gnuplot_executable);

} // namespace diario
