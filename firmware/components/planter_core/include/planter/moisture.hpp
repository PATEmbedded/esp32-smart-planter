// Soil moisture in percent from the voltage of a capacitive sensor.
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace planter {

// The sensor's output in dry air and with its probe in water, in millivolts. Capacitive sensors
// usually read lower when wetter, but the mapping works either way round. Measure your own: the
// firmware logs the raw voltage.
struct MoistureCalibration {
    uint32_t dry_mv = 2600;
    uint32_t wet_mv = 1100;
};

// A reading further outside the calibrated range than this share of the span counts as a sensor
// fault, such as a broken wire reading close to 0 V or to the supply.
inline constexpr float kFaultMargin = 0.15f;

// 0 % at dry_mv, 100 % at wet_mv, clamped between the two. nullopt for a reading that points to a
// fault, or for a calibration whose two points are less than 1 mV apart.
std::optional<float> moisture_percent(uint32_t mv, const MoistureCalibration& calibration);

// Median of an odd number of samples: one spike, for example while the pump starts, does not move it.
template <std::size_t N>
uint32_t median(std::array<uint32_t, N> samples) {
    static_assert(N % 2 == 1, "use an odd number of samples");
    std::nth_element(samples.begin(), samples.begin() + N / 2, samples.end());
    return samples[N / 2];
}

}  // namespace planter
