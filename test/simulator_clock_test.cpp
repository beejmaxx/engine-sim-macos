#include "engine.h"
#include "simulator.h"
#include <gtest/gtest.h>
#include <cmath>

namespace {
class ClockSimulator : public Simulator {
    void writeToSynthesizer() override { }
};
}

#if !defined(__EMSCRIPTEN__)
TEST(SimulatorClock, SubstepFramesDoNotChangeTheAudioClock) {
    Engine engine;
    ClockSimulator sim;
    sim.loadSimulation(&engine, nullptr, nullptr);
    sim.setSimulationFrequency(2500);
    sim.setSynthesizerLatencyCorrectionEnabled(false);
    int steps = 0;
    for (int frame = 0; frame < 1000; ++frame) {
        sim.startFrame(0.001);
        steps += sim.simulationSteps();
    }
    EXPECT_EQ(steps, 2500);
}

TEST(SimulatorClock, IrregularFramesPreserveElapsedSimulationTime) {
    Engine engine;
    ClockSimulator sim;
    sim.loadSimulation(&engine, nullptr, nullptr);
    sim.setSimulationFrequency(2500);
    sim.setSynthesizerLatencyCorrectionEnabled(false);
    const double frameTimes[] = {0.001, 0.0001, 0.003, 0.0002, 0.007, 0.01};
    int steps = 0;
    double elapsed = 0;
    for (int frame = 0; frame < 10000; ++frame) {
        const double dt = frameTimes[frame % 6];
        sim.startFrame(dt);
        steps += sim.simulationSteps();
        elapsed += dt;
        EXPECT_NEAR(steps / 2500.0, elapsed, 1.0 / 2500.0 + 1e-9);
    }
}
#endif
