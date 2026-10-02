#include "leveling_filter.h"
#include <gtest/gtest.h>
#include <cmath>

TEST(LevelingFilter, SuddenLoudPulsesStayWithinTarget) {
    LevelingFilter filter;
    filter.p_maxLevel = 1.9f;
    // Let the leveler settle on quiet material, then inject loud pulses of
    // both polarities. The first sample must be protected too.
    for (int i = 0; i < 20000; ++i) filter.f(1000);
    for (float pulse : {100000.0f, -1000000.0f, 300000.0f, -8000000.0f}) {
        EXPECT_LE(std::abs(filter.f(pulse)), filter.p_target + 1);
        for (int i = 0; i < 1000; ++i) EXPECT_LE(std::abs(filter.f(1000)), filter.p_target + 1);
    }
}

TEST(LevelingFilter, RecoversGraduallyAfterLoudPulse) {
    LevelingFilter filter;
    filter.f(1000000);
    const float immediately = filter.f(1000);
    float recovered = 0;
    for (int i = 0; i < 20000; ++i) recovered = filter.f(1000);
    EXPECT_LT(immediately, 100);
    EXPECT_GT(recovered, 900);
    EXPECT_LE(recovered, 1000);
    EXPECT_FLOAT_EQ(filter.f(0), 0);
}

TEST(LevelingFilter, RespectsConfiguredGainCeiling) {
    LevelingFilter filter;
    filter.p_maxLevel = 0.25f;
    for (int i = 0; i < 2000; ++i) EXPECT_LE(std::abs(filter.f(-1000)), 250);
}
