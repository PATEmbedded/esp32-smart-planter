#include "planter/schedule.hpp"

#include <gtest/gtest.h>

#include <ctime>

using planter::add_window;
using planter::in_window;
using planter::WateringWindows;

namespace {

std::tm at(int wday, int hour, int minute) {
    std::tm t{};
    t.tm_wday = wday;
    t.tm_hour = hour;
    t.tm_min = minute;
    return t;
}

TEST(Schedule, ParsesWindowsAndAnswersForAnyDay) {
    WateringWindows windows(false);
    ASSERT_TRUE(add_window(windows, "06:00-09:00"));
    ASSERT_TRUE(add_window(windows, "18:00-21:00"));
    for (int day = 0; day < 7; ++day) {
        const std::tm early = at(day, 5, 59);
        const std::tm morning = at(day, 6, 0);
        const std::tm late_morning = at(day, 9, 0);
        const std::tm evening = at(day, 20, 59);
        EXPECT_EQ(in_window(windows, &early), false);
        EXPECT_EQ(in_window(windows, &morning), true);
        EXPECT_EQ(in_window(windows, &late_morning), false);  // the end is not part of the window
        EXPECT_EQ(in_window(windows, &evening), true);
    }
}

TEST(Schedule, AWindowCanRunPastMidnight) {
    WateringWindows windows(false);
    ASSERT_TRUE(add_window(windows, "22:00-02:00"));
    const std::tm before = at(3, 23, 30);
    const std::tm after = at(4, 1, 30);
    const std::tm later = at(4, 2, 30);
    EXPECT_EQ(in_window(windows, &before), true);
    EXPECT_EQ(in_window(windows, &after), true);
    EXPECT_EQ(in_window(windows, &later), false);
}

TEST(Schedule, AnEmptyTextAddsNothingAndBadTextIsRejected) {
    WateringWindows windows(false);
    EXPECT_TRUE(add_window(windows, ""));
    EXPECT_FALSE(add_window(windows, "6:00-9:00"));
    EXPECT_FALSE(add_window(windows, "06:00 09:00"));
    EXPECT_FALSE(add_window(windows, "25:00-26:00"));
    const std::tm noon = at(1, 12, 0);
    EXPECT_EQ(in_window(windows, &noon), false);
}

TEST(Schedule, UnknownTimeGivesNoAnswer) {
    WateringWindows windows(false);
    ASSERT_TRUE(add_window(windows, "06:00-09:00"));
    EXPECT_EQ(in_window(windows, nullptr), std::nullopt);
}

}  // namespace
