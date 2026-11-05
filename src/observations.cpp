// SPDX-License-Identifier: GPL-2.0-or-later
#include "diario/observations.hpp"

#include <wreport/bulletin.h>
#include <wreport/conv.h>
#include <wreport/error.h>
#include <wreport/subset.h>
#include <wreport/var.h>
#include <wreport/varinfo.h>

#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <utility>

namespace diario {
namespace {

template<typename T>
struct Field
{
	bool seen = false;
	bool conflict = false;
	std::optional<T> value;

	void observe(std::optional<T> next)
	{
		if (seen && value != next)
			conflict = true;
		else if (!seen)
			value = std::move(next);
		seen = true;
	}
};

bool is_temperature(wreport::Varcode code)
{
	return code == WR_VAR(0, 12, 1) || code == WR_VAR(0, 12, 101)
		|| code == WR_VAR(0, 12, 4) || code == WR_VAR(0, 12, 104);
}

std::optional<int> integer_value(const wreport::Var& var)
{
	return var.isset() ? std::optional<int>{var.enqi()} : std::nullopt;
}

std::optional<std::string> name_value(const wreport::Var& var)
{
	if (!var.isset())
		return std::nullopt;

	std::string name = var.enqs();
	const auto last = name.find_last_not_of(" \t\r\n");
	if (last == std::string::npos)
		name.clear();
	else
		name.resize(last + 1);
	return name;
}

std::optional<Observation> extract_subset(
	const wreport::Subset& subset,
	const std::string& source,
	std::size_t message_index,
	std::size_t subset_index,
	std::vector<std::string>& warnings)
{
	const std::string location = source + ": message="
		+ std::to_string(message_index) + ", subset="
		+ std::to_string(subset_index) + ": ";
	const auto reject = [&](const std::string& reason)
		-> std::optional<Observation>
	{
		warnings.push_back(location + reason + "; subset ignorato.");
		return std::nullopt;
	};

	std::size_t temperature_index = 0;
	std::size_t temperature_count = 0;
	for (std::size_t index = 0; index < subset.size(); ++index)
	{
		if (is_temperature(subset[index].code()) && subset[index].isset())
		{
			temperature_index = index;
			++temperature_count;
		}
	}
	if (temperature_count != 1)
		return reject("occorre una sola temperatura dell'aria definita, trovate "
			+ std::to_string(temperature_count));

	Field<int> block;
	Field<int> station;
	Field<std::string> short_name;
	Field<std::string> long_name;
	std::array<Field<int>, 6> calendar;

	for (std::size_t index = 0; index < temperature_index; ++index)
	{
		const auto& var = subset[index];
		const auto code = var.code();

		if (code >= WR_VAR(0, 4, 1) && code <= WR_VAR(0, 4, 6))
		{
			calendar[WR_VAR_Y(code) - 1].observe(integer_value(var));
			continue;
		}

		// A flat subset is insufficient to resolve periods, displacements,
		// increments or qualified times safely. In this initial profile,
		// only the six absolute calendar fields of class 4 are accepted.
		if (var.isset() && ((WR_VAR_F(code) == 0 && WR_VAR_X(code) == 4)
			|| code == WR_VAR(0, 8, 21)))
			return reject("contesto temporale non supportato prima della temperatura: "
				+ wreport::varcode_format(code));

		switch (code)
		{
			case WR_VAR(0, 1, 1):
				block.observe(integer_value(var));
				break;
			case WR_VAR(0, 1, 2):
				station.observe(integer_value(var));
				break;
			case WR_VAR(0, 1, 15):
				short_name.observe(name_value(var));
				break;
			case WR_VAR(0, 1, 19):
				long_name.observe(name_value(var));
				break;
			case WR_VAR(0, 1, 11):
				if (var.isset())
					return reject("identificativo di piattaforma mobile B01011 non supportato");
				break;
			default:
				break;
		}
	}

	if (block.conflict || station.conflict || short_name.conflict || long_name.conflict)
		return reject("identificazione della stazione ripetuta con valori discordanti");
	if (!block.value || !station.value)
		return reject("identificazione WMO B01001/B01002 assente nel prefisso");
	if (*block.value < 0 || *block.value > 99
		|| *station.value < 0 || *station.value > 999)
		return reject("identificazione WMO fuori dal formato a cinque cifre");

	for (std::size_t index = 0; index < calendar.size(); ++index)
	{
		if (calendar[index].conflict)
			return reject("campo temporale ripetuto con valori discordanti: "
				+ wreport::varcode_format(WR_VAR(0, 4, index + 1)));
		if (index < 5 && !calendar[index].value)
			return reject("campo temporale obbligatorio assente o indefinito: "
				+ wreport::varcode_format(WR_VAR(0, 4, index + 1)));
	}

	const int year = *calendar[0].value;
	const int month = *calendar[1].value;
	const int day = *calendar[2].value;
	const int hour = *calendar[3].value;
	const int minute = *calendar[4].value;
	const int second = calendar[5].value.value_or(0);
	if (year < 1 || year > 9999 || month < 1 || month > 12
		|| day < 1 || day > 31 || hour < 0 || hour > 23
		|| minute < 0 || minute > 59 || second < 0 || second > 59)
		return reject("data/ora UTC non valida o secondo intercalare non supportato");

	const std::chrono::year_month_day date{
		std::chrono::year{year},
		std::chrono::month{static_cast<unsigned>(month)},
		std::chrono::day{static_cast<unsigned>(day)}};
	if (!date.ok())
		return reject("data del calendario non valida");
	const auto instant = std::chrono::sys_days{date} + std::chrono::hours{hour}
		+ std::chrono::minutes{minute} + std::chrono::seconds{second};

	const auto& temperature = subset[temperature_index];
	const double celsius = wreport::convert_units(
		temperature.info()->unit, "C", temperature.enqd());
	if (!std::isfinite(celsius))
		return reject("temperatura convertita non finita");

	char station_id[6];
	std::snprintf(station_id, sizeof(station_id), "%02d%03d",
		*block.value, *station.value);
	char timestamp[21];
	std::snprintf(timestamp, sizeof(timestamp), "%04d-%02d-%02dT%02d:%02d:%02dZ",
		year, month, day, hour, minute, second);

	Observation result;
	result.station_id = station_id;
	result.station_name = long_name.value.value_or(short_name.value.value_or(""));
	result.timestamp = timestamp;
	result.epoch_seconds = std::chrono::duration_cast<std::chrono::seconds>(
		instant.time_since_epoch()).count();
	result.temperature_c = celsius;
	result.native_value = temperature.enqd();
	result.native_unit = temperature.info()->unit;
	result.native_scale = temperature.info()->scale;
	result.position = temperature_index;
	result.station_key = "wmo:" + result.station_id;
	if (temperature.code() == WR_VAR(0, 12, 4)
		|| temperature.code() == WR_VAR(0, 12, 104))
		result.sensor_height_m = 2.0;
	// B07032 is intentionally not interpreted without template recognition.
	result.variable_code = wreport::varcode_format(temperature.code());
	result.source = source;
	result.message_index = message_index;
	result.subset_index = subset_index;

	if (!calendar[5].value)
		warnings.push_back(location + "B04006 assente o indefinito: secondi assunti 00.");
	return result;
}

} // namespace

std::vector<Observation> extract_temperatures(
	const wreport::BufrBulletin& bulletin,
	const std::string& source,
	std::size_t message_index,
	std::vector<std::string>& warnings)
{
	if (bulletin.data_category == 255)
		return extract_meteohub(bulletin, source, message_index, warnings);
	std::vector<Observation> result;
	if (bulletin.data_category != 0)
	{
		warnings.push_back(source + ": message=" + std::to_string(message_index)
			+ ": categoria BUFR " + std::to_string(bulletin.data_category)
			+ " non supportata; richiesta categoria 0.");
		return result;
	}
	for (std::size_t index = 0; index < bulletin.subsets.size(); ++index)
	{
		try
		{
			auto observation = extract_subset(bulletin.subsets[index], source,
				message_index, index, warnings);
			if (observation)
				result.push_back(std::move(*observation));
		}
		catch (const wreport::error& error)
		{
			warnings.push_back(source + ": message=" + std::to_string(message_index)
				+ ", subset=" + std::to_string(index)
				+ ": errore wreport nell'estrazione: " + error.what()
				+ "; subset ignorato.");
		}
	}
	return result;
}

} // namespace diario
