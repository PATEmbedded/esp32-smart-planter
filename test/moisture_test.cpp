#include "planter/moisture.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>

using planter::MoistureCalibration;
using planter::moisture_percent;

TEST(Moisture, MapsTheCalibrationPointsToZeroAndHundred) {
    const MoistureCalibration cal{2600, 1100};
    EXPECT_FLOAT_EQ(*moisture_percent(2600, cal), 0.0f);
    EXPECT_FLOAT_EQ(*moisture_percent(1100, cal), 100.0f);
    EXPECT_FLOAT_EQ(*moisture_percent(1850, cal), 50.0f);
}

TEST(Moisture, WorksForSensorsThatReadHigherWhenWet) {
    const MoistureCalibration cal{500, 2500};
    EXPECT_FLOAT_EQ(*moisture_percent(1000, cal), 25.0f);
}

TEST(Moisture, ClampsReadingsJustOutsideTheRange) {
    const MoistureCalibration cal{2600, 1100};
    EXPECT_FLOAT_EQ(*moisture_percent(2700, cal), 0.0f);    // a little drier than the dry point
    EXPECT_FLOAT_EQ(*moisture_percent(1000, cal), 100.0f);  // a little wetter than the wet point
}

TEST(Moisture, ReportsReadingsFarOutsideTheRangeAsFaults) {
    const MoistureCalibration cal{2600, 1100};
    EXPECT_FALSE(moisture_percent(0, cal));     // broken wire
    EXPECT_FALSE(moisture_percent(3300, cal));  // shorted to the supply
    EXPECT_TRUE(moisture_percent(2600 + 200, cal));
    EXPECT_FALSE(moisture_percent(2600 + 250, cal));
}

TEST(Moisture, RejectsACalibrationWithoutSpan) {
    EXPECT_FALSE(moisture_percent(1500, MoistureCalibration{1500, 1500}));
}

TEST(Moisture, MedianIgnoresASpike) {
    EXPECT_EQ(planter::median(std::array<uint32_t, 5>{1800, 1810, 3300, 1795, 1805}), 1805u);
    EXPECT_EQ(planter::median(std::array<uint32_t, 1>{42}), 42u);
}
