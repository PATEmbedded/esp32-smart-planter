// Days of a simulated pot: the soil dries out, the pump adds water, the controller decides.
#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>

#include "planter/watering.hpp"

using planter::Fault;
using planter::Phase;
using planter::Readings;
using planter::WateringConfig;
using planter::WateringController;

namespace {

constexpr uint32_t kSecond = 1000;
constexpr uint32_t kDaySeconds = 24 * 3600;

struct Pot {
    double moisture = 40.0;            // percent
    double drying_per_hour = 1.5;      // percent
    double gain_per_pump_second = 0.8; // percent; 0 when the hose is off
    double tank_pump_seconds = 1e9;    // pumping left until the tank is empty

    void advance_one_second(bool pump_on) {
        moisture -= drying_per_hour / 3600.0;
        if (pump_on && tank_pump_seconds > 0.0) {
            moisture += gain_per_pump_second;
            tank_pump_seconds -= 1.0;
        }
        moisture = std::clamp(moisture, 0.0, 100.0);
    }
};

// Watering windows 06:00 to 09:00 and 18:00 to 21:00; the simulation starts at midnight.
bool in_window(uint32_t second_of_day) {
    const uint32_t hour = second_of_day / 3600;
    return (hour >= 6 && hour < 9) || (hour >= 18 && hour < 21);
}

struct Day {
    uint32_t pump_seconds = 0;
    uint32_t pump_seconds_outside_windows = 0;
    double lowest = 100.0;
    double highest = 0.0;
};

class Simulation {
public:
    explicit Simulation(const WateringConfig& config = WateringConfig{}) : controller(config) { controller.start(0); }

    Day run_day() { return run_for(kDaySeconds); }

    Day run_for(uint32_t seconds) {
        Day day;
        for (uint32_t s = 0; s < seconds; ++s) {
            ++second;
            Readings r;
            r.moisture = static_cast<float>(pot.moisture);
            r.tank_empty = pot.tank_pump_seconds <= 0.0;
            r.in_window = in_window(second % kDaySeconds);
            controller.update(r, second * kSecond);
            pot.advance_one_second(controller.pump_on());
            if (controller.pump_on()) {
                day.pump_seconds++;
                if (!in_window(second % kDaySeconds)) {
                    day.pump_seconds_outside_windows++;
                }
            }
            day.lowest = std::min(day.lowest, pot.moisture);
            day.highest = std::max(day.highest, pot.moisture);
        }
        return day;
    }

    Pot pot;
    WateringController controller;
    uint32_t second = 0;
};

TEST(SimulatedPot, KeepsTheSoilInRangeForAWeek) {
    Simulation sim;
    for (int d = 0; d < 7; ++d) {
        const Day day = sim.run_day();
        EXPECT_GT(day.lowest, WateringConfig{}.critical_below) << "day " << d;  // never needs an emergency
        EXPECT_LT(day.highest, 65.0) << "day " << d;
        EXPECT_GE(day.highest, 55.0) << "day " << d;  // watered up to stop_at every day
        EXPECT_EQ(day.pump_seconds_outside_windows, 0u) << "day " << d;
        EXPECT_LE(day.pump_seconds * kSecond, WateringConfig{}.daily_budget_ms) << "day " << d;
    }
    EXPECT_EQ(sim.controller.fault(), Fault::none);
}

TEST(SimulatedPot, ALooseHoseEndsInAFaultAndTheSoilDriesOut) {
    Simulation sim;
    sim.run_day();
    sim.pot.gain_per_pump_second = 0.0;
    const Day day = sim.run_day();
    EXPECT_EQ(sim.controller.fault(), Fault::no_effect);
    EXPECT_EQ(day.pump_seconds, 4u * 5u);  // four pulses, then it stops trying
    EXPECT_LT(day.lowest, WateringConfig{}.critical_below);  // nothing more was pumped

    sim.pot.gain_per_pump_second = 0.8;  // someone fixes the hose
    sim.controller.clear_fault(sim.second * kSecond);
    // The catch-up is limited by the budget. Its 24-hour period started with the day, at the clear.
    const Day recovering = sim.run_for(kDaySeconds - 1);
    EXPECT_EQ(recovering.pump_seconds * kSecond, WateringConfig{}.daily_budget_ms);
    sim.run_for(1);
    const Day after = sim.run_day();
    EXPECT_EQ(sim.controller.fault(), Fault::none);
    EXPECT_GE(after.highest, 55.0);
}

TEST(SimulatedPot, AnEmptyTankStopsThePumpUntilRefilled) {
    Simulation sim;
    sim.pot.tank_pump_seconds = 12.0;  // a little more than two pulses
    sim.run_day();
    EXPECT_EQ(sim.controller.fault(), Fault::tank_empty);
    EXPECT_EQ(sim.controller.phase(), Phase::fault);
    sim.pot.tank_pump_seconds = 1e9;  // refilled
    const Day day = sim.run_day();
    EXPECT_EQ(sim.controller.fault(), Fault::none);
    EXPECT_GE(day.highest, 55.0);
}

}  // namespace
