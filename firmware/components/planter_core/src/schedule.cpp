#include "planter/schedule.hpp"

#include <cstdint>

namespace planter {

bool add_window(WateringWindows& windows, std::string_view text) {
    if (text.empty()) {
        return true;
    }
    if (text.size() != 11 || text[5] != '-') {
        return false;
    }
    const auto start = ect::time_of_day::parse(text.substr(0, 5));
    const auto end = ect::time_of_day::parse(text.substr(6, 5));
    if (!start || !end) {
        return false;
    }
    return windows.set(ect::every_day, *start, *end, true);
}

std::optional<bool> in_window(const WateringWindows& windows, const std::tm* local_time) {
    if (local_time == nullptr) {
        return std::nullopt;
    }
    const auto seconds = static_cast<std::uint32_t>(local_time->tm_hour * 3600 + local_time->tm_min * 60 +
                                                    (local_time->tm_sec < 60 ? local_time->tm_sec : 59));
    return windows.value_at(ect::weekday_from_tm(local_time->tm_wday), ect::time_of_day{seconds});
}

}  // namespace planter
