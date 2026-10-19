#include "planter/watering.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <optional>

using planter::Fault;
using planter::Phase;
using planter::Readings;
using planter::WateringConfig;
using planter::WateringController;

namespace {

constexpr uint32_t kSecond = 1000;
constexpr uint32_t kMinute = 60 * kSecond;
constexpr uint32_t kHour = 60 * kMinute;

Readings reading(std::optional<float> moisture, std::optional<bool> in_window = true, bool tank_empty = false) {
    Readings r;
    r.moisture = moisture;
    r.in_window = in_window;
    r.tank_empty = tank_empty;
    return r;
}

// Steps the controller once a second, like the firmware's control task.
class Rig {
public:
    explicit Rig(const WateringConfig& config = WateringConfig{}) : controller(config) { controller.start(now); }

    void run(const Readings& r, uint32_t duration_ms) {
        for (uint32_t t = 0; t < duration_ms; t += kSecond) {
            now += kSecond;
            controller.update(r, now);
            if (controller.pump_on()) {
                pump_seconds++;
            }
            if (controller.pump_on() && !was_on) {
                pulses++;
            }
            was_on = controller.pump_on();
        }
    }

    WateringController controller;
    uint32_t now = 0;
    int pulses = 0;
    int pump_seconds = 0;
    bool was_on = false;
};

TEST(Watering, PulsesWhenDryInsideAWindow) {
    Rig rig;
    rig.run(reading(30.0f), kSecond);
    EXPECT_EQ(rig.controller.phase(), Phase::watering);
    EXPECT_TRUE(rig.controller.pump_on());
    rig.run(reading(30.0f), 5 * kSecond);
    EXPECT_EQ(rig.controller.phase(), Phase::soaking);
    EXPECT_FALSE(rig.controller.pump_on());
    EXPECT_EQ(rig.controller.pump_time_used_ms(), 5 * kSecond);
}

TEST(Watering, WaitsOutsideTheWindows) {
    Rig rig;
    rig.run(reading(30.0f, false), 2 * kHour);
    EXPECT_EQ(rig.pulses, 0);
    EXPECT_EQ(rig.controller.phase(), Phase::idle);
}

TEST(Watering, WatersCriticallyDrySoilAtAnyTime) {
    Rig rig;
    rig.run(reading(10.0f, false), kSecond);
    EXPECT_TRUE(rig.controller.pump_on());
}

TEST(Watering, WithoutAClockOnlyCriticalDrynessCounts) {
    Rig rig;
    rig.run(reading(30.0f, std::nullopt), kHour);
    EXPECT_EQ(rig.pulses, 0);
    rig.run(reading(10.0f, std::nullopt), kSecond);
    EXPECT_TRUE(rig.controller.pump_on());
}

TEST(Watering, PulsesUntilTheSoilReachesStopAt) {
    Rig rig;
    float moisture = 30.0f;
    int pulses_seen = 0;
    for (int second = 0; second < 30 * 60; ++second) {
        rig.run(reading(moisture), kSecond);
        if (rig.pulses > pulses_seen) {  // each pulse adds 10 %
            pulses_seen = rig.pulses;
            moisture += 10.0f;
        }
    }
    EXPECT_EQ(rig.pulses, 3);  // 30 -> 40 -> 50 -> 60
    EXPECT_FALSE(rig.controller.cycle_running());
    EXPECT_EQ(rig.controller.phase(), Phase::idle);
    EXPECT_EQ(rig.controller.ineffective_pulses(), 0);
}

TEST(Watering, AScheduledCycleEndsWhenTheWindowCloses) {
    Rig rig;
    rig.run(reading(30.0f), 64 * kSecond);  // one pulse and most of its soak, still dry
    EXPECT_EQ(rig.pulses, 1);
    EXPECT_EQ(rig.controller.phase(), Phase::soaking);
    rig.run(reading(32.0f, false), kHour);
    EXPECT_EQ(rig.pulses, 1);
    EXPECT_FALSE(rig.controller.cycle_running());
}

TEST(Watering, StopsThePumpAtOnceWhenTheTankRunsEmpty) {
    Rig rig;
    rig.run(reading(30.0f), 2 * kSecond);
    ASSERT_TRUE(rig.controller.pump_on());
    rig.run(reading(30.0f, true, true), kSecond);
    EXPECT_FALSE(rig.controller.pump_on());
    EXPECT_EQ(rig.controller.phase(), Phase::fault);
    EXPECT_EQ(rig.controller.fault(), Fault::tank_empty);
    EXPECT_EQ(rig.controller.pump_time_used_ms(), 2 * kSecond);

    rig.run(reading(30.0f, true, true), kHour);
    EXPECT_EQ(rig.pump_seconds, 2);
    rig.run(reading(30.0f), kSecond);  // refilled
    EXPECT_EQ(rig.controller.fault(), Fault::none);
}

TEST(Watering, GivesUpWhenPulsesDoNotRaiseTheReading) {
    Rig rig;
    rig.run(reading(30.0f), kHour);  // a loose hose: the reading never moves
    EXPECT_EQ(rig.pulses, 4);
    EXPECT_EQ(rig.pump_seconds, 20);
    EXPECT_EQ(rig.controller.phase(), Phase::fault);
    EXPECT_EQ(rig.controller.fault(), Fault::no_effect);

    rig.run(reading(30.0f), 5 * kHour);  // does not clear itself
    EXPECT_EQ(rig.pulses, 4);

    rig.controller.clear_fault(rig.now);
    EXPECT_EQ(rig.controller.phase(), Phase::idle);
    rig.run(reading(30.0f), kSecond);
    EXPECT_TRUE(rig.controller.pump_on());
}

TEST(Watering, KeepsToTheDailyBudget) {
    WateringConfig config;
    config.daily_budget_ms = 15 * kSecond;  // three pulses
    config.max_ineffective_pulses = 10;
    Rig rig(config);
    rig.run(reading(30.0f), 20 * kHour);
    EXPECT_EQ(rig.pulses, 3);
    EXPECT_EQ(rig.controller.pump_time_used_ms(), 15 * kSecond);
    rig.run(reading(30.0f), 5 * kHour);  // a new 24-hour period
    EXPECT_GT(rig.pulses, 3);
}

TEST(Watering, ManualPulseIgnoresMoistureAndWindowButNotTheTank) {
    Rig rig;
    rig.run(reading(80.0f, false), kSecond);
    EXPECT_TRUE(rig.controller.request_pulse(reading(80.0f, false), rig.now));
    EXPECT_TRUE(rig.controller.pump_on());
    rig.run(reading(80.0f, false), 2 * kMinute);
    EXPECT_FALSE(rig.controller.pump_on());
    EXPECT_FALSE(rig.controller.request_pulse(reading(80.0f, false, true), rig.now));
}

TEST(Watering, ManualPulsesDoNotCountAsIneffective) {
    Rig rig;
    for (int i = 0; i < 6; ++i) {
        ASSERT_TRUE(rig.controller.request_pulse(reading(80.0f, false), rig.now));
        rig.run(reading(80.0f, false), 2 * kMinute);
    }
    EXPECT_EQ(rig.controller.fault(), Fault::none);
    EXPECT_EQ(rig.controller.ineffective_pulses(), 0);
}

TEST(Watering, ASensorFaultStopsWateringAndClearsItself) {
    Rig rig;
    rig.run(reading(std::nullopt), kMinute);
    EXPECT_EQ(rig.controller.fault(), Fault::sensor);
    EXPECT_EQ(rig.pulses, 0);
    rig.run(reading(30.0f), kSecond);
    EXPECT_EQ(rig.controller.fault(), Fault::none);
    rig.run(reading(30.0f), kSecond);
    EXPECT_TRUE(rig.controller.pump_on());
}

TEST(Watering, NamesPhasesAndFaults) {
    EXPECT_STREQ(planter::to_string(Phase::soaking), "soaking");
    EXPECT_STREQ(planter::to_string(Fault::no_effect), "no effect");
}

}  // namespace
