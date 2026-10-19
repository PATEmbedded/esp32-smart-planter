// When to run the pump, and when not to. No hardware access: the firmware passes in readings and
// the time and switches the pump as pump_on() says; the unit tests do the same with a simulated pot.
//
// Watering happens in pulses with a soak time after each, so the water spreads before the next
// reading. A cycle starts when the soil is drier than start_below inside a watering window, or
// drier than critical_below at any time, and pulses until the soil reaches stop_at. Safety limits:
// no pumping from an empty tank, at most daily_budget_ms of pump time in 24 hours, and a fault
// when several pulses in a row do not raise the reading, which points to a loose hose or a dry
// pump. That fault needs a person to clear it; the others clear themselves.
#pragma once

#include <cstdint>
#include <optional>

#include "ect/state_machine.hpp"

namespace planter {

struct WateringConfig {
    float start_below = 35.0f;            // percent; starts a cycle inside a watering window
    float stop_at = 55.0f;                // percent; ends a cycle
    float critical_below = 15.0f;         // percent; starts a cycle at any time, also without a clock
    uint32_t pulse_ms = 5000;             // pump time of one pulse
    uint32_t soak_ms = 60000;             // wait after each pulse
    uint32_t daily_budget_ms = 60000;     // pump time per 24 hours at most
    uint8_t max_ineffective_pulses = 4;   // pulses in a row that raise the reading by less than min_rise
    float min_rise = 2.0f;                // percent
};

struct Readings {
    std::optional<float> moisture;  // percent; nullopt when the sensor reading points to a fault
    bool tank_empty = false;
    std::optional<bool> in_window;  // inside a watering window; nullopt when the time of day is unknown
};

enum class Phase : uint8_t { idle, watering, soaking, fault };
enum class Fault : uint8_t { none, tank_empty, sensor, no_effect };

const char* to_string(Phase phase);
const char* to_string(Fault fault);

class WateringController {
public:
    explicit WateringController(const WateringConfig& config = WateringConfig{});
    WateringController(const WateringController&) = delete;
    WateringController& operator=(const WateringController&) = delete;

    void start(uint32_t now_ms);

    // Call about once a second with fresh readings.
    void update(const Readings& readings, uint32_t now_ms);

    // A pulse asked for by hand, for example with a button. It obeys the tank and the budget, not
    // the moisture or the windows. Returns true if the pump started.
    bool request_pulse(const Readings& readings, uint32_t now_ms);

    // Clears a fault, as a long press of the button does. The only way out of Fault::no_effect.
    void clear_fault(uint32_t now_ms);

    bool pump_on() const { return context_.pump_on; }
    Phase phase() const { return machine_.current(); }
    Fault fault() const { return context_.fault; }
    bool cycle_running() const { return context_.cycle != Cycle::none; }
    uint8_t ineffective_pulses() const { return context_.ineffective; }
    // Pump time in the current 24-hour budget period, finished pulses only.
    uint32_t pump_time_used_ms() const { return context_.used_ms; }

private:
    enum class Event : uint8_t { tick, manual, clear, timeout };
    enum class Cycle : uint8_t { none, scheduled, critical };

    struct Context {
        WateringConfig config;
        Readings readings;
        uint32_t now_ms = 0;
        bool pump_on = false;
        uint32_t pump_since_ms = 0;
        std::optional<float> moisture_before;  // at the start of the current pulse
        bool manual = false;                   // the current pulse was asked for by hand
        Cycle cycle = Cycle::none;
        uint8_t ineffective = 0;
        Fault fault = Fault::none;
        uint32_t period_start_ms = 0;
        uint32_t used_ms = 0;
    };

    using State = ect::state_info<Phase, Context>;
    using Transition = ect::transition<Phase, Event, Context>;
    using Machine = ect::state_machine<Phase, Event, Context>;

    static bool budget_left(const Context& c);
    static bool should_start(const Context& c);
    static bool effect_missing(const Context& c);
    void prepare(const Readings& readings, uint32_t now_ms);

    static const Transition kTransitions[];

    Context context_;
    State states_[4];
    Machine machine_;
};

}  // namespace planter
