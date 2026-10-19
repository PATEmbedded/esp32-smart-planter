#include "planter/watering.hpp"

namespace planter {

namespace {
constexpr uint32_t kBudgetPeriodMs = 24u * 60u * 60u * 1000u;
}

const char* to_string(Phase phase) {
    switch (phase) {
        case Phase::idle:
            return "idle";
        case Phase::watering:
            return "watering";
        case Phase::soaking:
            return "soaking";
        case Phase::fault:
            return "fault";
    }
    return "unknown";
}

const char* to_string(Fault fault) {
    switch (fault) {
        case Fault::none:
            return "none";
        case Fault::tank_empty:
            return "tank empty";
        case Fault::sensor:
            return "sensor";
        case Fault::no_effect:
            return "no effect";
    }
    return "unknown";
}

bool WateringController::budget_left(const Context& c) {
    return c.used_ms + c.config.pulse_ms <= c.config.daily_budget_ms;
}

bool WateringController::should_start(const Context& c) {
    if (!c.readings.moisture || c.readings.tank_empty || !budget_left(c)) {
        return false;
    }
    if (c.cycle != Cycle::none) {
        return true;  // prepare() has ended the cycle if it is done
    }
    const float moisture = *c.readings.moisture;
    return moisture < c.config.critical_below ||
           (c.readings.in_window.value_or(false) && moisture < c.config.start_below);
}

bool WateringController::effect_missing(const Context& c) {
    if (c.manual || !c.moisture_before || !c.readings.moisture) {
        return false;  // nothing to judge
    }
    return *c.readings.moisture - *c.moisture_before < c.config.min_rise;
}

// In table order; the first transition whose guard passes fires.
const WateringController::Transition WateringController::kTransitions[] = {
    // An empty tank stops the pump at once.
    {Phase::watering, Event::tick, Phase::fault, [](const Context& c) { return c.readings.tank_empty; },
     [](Context& c) { c.fault = Fault::tank_empty; }},
    {Phase::watering, Event::timeout, Phase::soaking, nullptr, nullptr},

    {Phase::soaking, Event::tick, Phase::fault, [](const Context& c) { return c.readings.tank_empty; },
     [](Context& c) { c.fault = Fault::tank_empty; }},
    {Phase::soaking, Event::timeout, Phase::fault,
     [](const Context& c) { return effect_missing(c) && c.ineffective + 1u >= c.config.max_ineffective_pulses; },
     [](Context& c) {
         c.fault = Fault::no_effect;
         c.cycle = Cycle::none;
     }},
    {Phase::soaking, Event::timeout, Phase::idle, nullptr,
     [](Context& c) {
         if (!c.manual && c.moisture_before && c.readings.moisture) {
             c.ineffective = effect_missing(c) ? static_cast<uint8_t>(c.ineffective + 1u) : uint8_t{0};
         }
     }},

    {Phase::idle, Event::tick, Phase::fault, [](const Context& c) { return c.readings.tank_empty; },
     [](Context& c) { c.fault = Fault::tank_empty; }},
    {Phase::idle, Event::tick, Phase::fault, [](const Context& c) { return !c.readings.moisture.has_value(); },
     [](Context& c) { c.fault = Fault::sensor; }},
    {Phase::idle, Event::tick, Phase::watering, should_start,
     [](Context& c) {
         c.manual = false;
         if (c.cycle == Cycle::none) {
             c.cycle = *c.readings.moisture < c.config.critical_below ? Cycle::critical : Cycle::scheduled;
         }
     }},
    {Phase::idle, Event::manual, Phase::watering,
     [](const Context& c) { return !c.readings.tank_empty && budget_left(c); },
     [](Context& c) { c.manual = true; }},

    // Tank and sensor faults clear themselves; no_effect needs clear_fault().
    {Phase::fault, Event::tick, Phase::idle,
     [](const Context& c) {
         return (c.fault == Fault::tank_empty && !c.readings.tank_empty) ||
                (c.fault == Fault::sensor && c.readings.moisture.has_value());
     },
     [](Context& c) { c.fault = Fault::none; }},
    {Phase::fault, Event::clear, Phase::idle, nullptr,
     [](Context& c) {
         c.fault = Fault::none;
         c.ineffective = 0;
     }},
};

WateringController::WateringController(const WateringConfig& config)
    : states_{
          {Phase::idle, 0, nullptr, nullptr},
          {Phase::watering, config.pulse_ms,
           [](Context& c) {
               c.pump_on = true;
               c.pump_since_ms = c.now_ms;
               c.moisture_before = c.readings.moisture;
           },
           [](Context& c) {
               c.pump_on = false;
               c.used_ms += c.now_ms - c.pump_since_ms;
           }},
          {Phase::soaking, config.soak_ms, nullptr, nullptr},
          {Phase::fault, 0, [](Context& c) { c.cycle = Cycle::none; }, nullptr},
      },
      machine_(states_, kTransitions, context_, Event::timeout) {
    context_.config = config;
}

void WateringController::start(uint32_t now_ms) {
    context_.now_ms = now_ms;
    context_.period_start_ms = now_ms;
    machine_.start(Phase::idle, now_ms);
}

void WateringController::prepare(const Readings& readings, uint32_t now_ms) {
    Context& c = context_;
    c.readings = readings;
    c.now_ms = now_ms;
    if (now_ms - c.period_start_ms >= kBudgetPeriodMs && !c.pump_on) {
        c.period_start_ms = now_ms;
        c.used_ms = 0;
    }
    if (c.cycle != Cycle::none) {
        const bool done = !c.readings.moisture || *c.readings.moisture >= c.config.stop_at || !budget_left(c) ||
                          (c.cycle == Cycle::scheduled && !c.readings.in_window.value_or(false));
        if (done && !c.pump_on) {
            c.cycle = Cycle::none;
        }
    }
}

void WateringController::update(const Readings& readings, uint32_t now_ms) {
    prepare(readings, now_ms);
    machine_.update(now_ms);  // pulse and soak timeouts first
    machine_.dispatch(Event::tick, now_ms);
}

bool WateringController::request_pulse(const Readings& readings, uint32_t now_ms) {
    prepare(readings, now_ms);
    return machine_.dispatch(Event::manual, now_ms);
}

void WateringController::clear_fault(uint32_t now_ms) {
    context_.now_ms = now_ms;
    machine_.dispatch(Event::clear, now_ms);
}

}  // namespace planter
