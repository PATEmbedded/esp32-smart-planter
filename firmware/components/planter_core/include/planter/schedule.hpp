// Watering windows: the times of day when the planter may water soil that is not critically dry.
#pragma once

#include <ctime>
#include <optional>
#include <string_view>

#include "ect/weekly_schedule.hpp"

namespace planter {

using WateringWindows = ect::weekly_schedule<bool>;

// Adds a window given as "HH:MM-HH:MM" on every day; "22:00-02:00" runs past midnight. An empty
// string adds nothing. Returns false, changing nothing, for anything else.
bool add_window(WateringWindows& windows, std::string_view text);

// Whether local_time falls inside a window. Pass nullptr when the time of day is not known, for
// example before the first sync with a time server; the result is then nullopt.
std::optional<bool> in_window(const WateringWindows& windows, const std::tm* local_time);

}  // namespace planter
