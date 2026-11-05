// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace wreport {
class BufrBulletin;
}

namespace diario {

struct Observation
{
	std::string station_id;
	std::string station_name;
	std::string timestamp;
	std::int64_t epoch_seconds = 0;
	double temperature_c = 0.0;
	std::optional<double> sensor_height_m;
	std::string variable_code;
	std::string source;
	std::size_t message_index = 0;
	std::size_t subset_index = 0;
};

/**
 * Extract a deliberately limited surface-station profile, not arbitrary BUFR.
 *
 * Requires category 0, a WMO block/station identifier and exactly one defined
 * B12001/B12101/B12004/B12104 per subset. Identification and date/time must
 * precede that temperature. Contradictory repeated fields and unsupported
 * temporal contexts cause a warning and skip the subset.
 *
 * Timestamps use the subset's UTC calendar fields, never the bulletin date.
 * When B04006 is absent or undefined, seconds are assumed to be zero and the
 * assumption is reported in warnings. Leap seconds are not supported.
 *
 * Height is 2 m for explicit B12004/B12104. Generic temperature descriptors
 * keep an unknown height: interpreting B07032 requires a supported template.
 * Quality attributes are not used to certify or filter the measurements.
 *
 * message_index is preserved unchanged; subset_index is zero-based.
 */
std::vector<Observation> extract_temperatures(
	const wreport::BufrBulletin& bulletin,
	const std::string& source,
	std::size_t message_index,
	std::vector<std::string>& warnings);

} // namespace diario
