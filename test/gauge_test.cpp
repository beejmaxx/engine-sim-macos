#include "gauge.h"

#include <gtest/gtest.h>
#include <limits>

namespace {
class TestGauge : public Gauge {
public:
    TestGauge() {
        m_min = 0.0f;
        m_max = 100.0f;
    }
    float position() const { return m_needlePosition; }
};
}

TEST(GaugeAnimation, ConstantReadingsSettleAtSlowFrameRates) {
    for (const float damping : {20.0f, 25.0f, 50.0f}) {
        for (const float dt : {1.0f / 60.0f, 0.05f, 0.1f, 0.25f}) {
            for (const float target : {0.15f, 0.5f, 1.0f}) {
                SCOPED_TRACE(::testing::Message()
                    << "damping=" << damping << " dt=" << dt << " target=" << target);
                TestGauge gauge;
                gauge.m_needleKd = damping;
                gauge.m_value = target * gauge.m_max;
                for (float elapsed = 0; elapsed < 2.0f; elapsed += dt) {
                    gauge.update(dt);
                }
                for (int frame = 0; frame < 20; ++frame) {
                    gauge.update(dt);
                    EXPECT_NEAR(gauge.position(), target, 0.001f);
                }
            }
        }
    }
}

TEST(GaugeAnimation, TracksChangedValuesWithUnevenFrameTimes) {
    TestGauge gauge;
    const float frameTimes[] = {0.016f, 0.1f, 0.25f, 0.005f, 0.05f};
    for (const float target : {0.8f, 0.1f, 0.6f}) {
        gauge.m_value = target * gauge.m_max;
        for (int frame = 0; frame < 30; ++frame) {
            const float previous = gauge.position();
            const float dt = frameTimes[frame % 5];
            gauge.update(dt);
            EXPECT_GE(gauge.position(), 0.0f);
            EXPECT_LE(gauge.position(), 1.0f);
            EXPECT_LE(std::abs(gauge.position() - previous),
                gauge.m_needleMaxVelocity * dt + 0.00001f);
        }
        EXPECT_NEAR(gauge.position(), target, 0.001f);
    }
}

TEST(GaugeAnimation, InvalidFrameTimesDoNotCorruptNeedle) {
    TestGauge gauge;
    gauge.m_value = 50.0f;
    gauge.update(0.0f);
    gauge.update(-0.1f);
    gauge.update(std::numeric_limits<float>::infinity());
    gauge.update(std::numeric_limits<float>::quiet_NaN());
    EXPECT_EQ(gauge.position(), 0.0f);
    gauge.m_max = gauge.m_min;
    gauge.update(0.1f);
    EXPECT_EQ(gauge.position(), 0.0f);
}
