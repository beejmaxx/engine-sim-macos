#include "convolution_filter.h"

#include <gtest/gtest.h>
#include <cmath>
#include <vector>

TEST(ConvolutionFilter, MatchesReferenceAcrossRingWrapAndVectorTails) {
    for (const int count : {1, 3, 4, 7, 16, 19, 64, 129}) {
        SCOPED_TRACE(count);
        ConvolutionFilter filter;
        filter.initialize(count);
        for (int i = 0; i < count; ++i) {
            filter.getImpulseResponse()[i] = static_cast<float>(std::cos(i * 0.7) / (i + 1));
        }
        std::vector<float> inputs;
        for (int sample = 0; sample < count * 3; ++sample) {
            inputs.push_back(static_cast<float>(std::sin(sample * 0.13)));
            double expected = 0;
            for (int lag = 0; lag < count && lag <= sample; ++lag) {
                expected += static_cast<double>(filter.getImpulseResponse()[lag]) * inputs[sample - lag];
            }
            EXPECT_NEAR(filter.f(inputs.back()), expected, 0.00001 * (1 + std::abs(expected)));
        }
        filter.destroy();
    }
}
