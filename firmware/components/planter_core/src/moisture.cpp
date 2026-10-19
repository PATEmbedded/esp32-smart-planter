#include "planter/moisture.hpp"

#include <cmath>

namespace planter {

std::optional<float> moisture_percent(uint32_t mv, const MoistureCalibration& calibration) {
    const float dry = static_cast<float>(calibration.dry_mv);
    const float span = static_cast<float>(calibration.wet_mv) - dry;  // negative for most sensors
    if (std::fabs(span) < 1.0f) {
        return std::nullopt;
    }
    const float position = (static_cast<float>(mv) - dry) / span;  // 0 when dry, 1 when wet
    if (position < -kFaultMargin || position > 1.0f + kFaultMargin) {
        return std::nullopt;
    }
    return std::clamp(position, 0.0f, 1.0f) * 100.0f;
}

}  // namespace planter
