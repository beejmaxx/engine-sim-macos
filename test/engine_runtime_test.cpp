#include "compiler.h"
#include "engine.h"
#include "ignition_module.h"
#include "simulator.h"

#include <gtest/gtest.h>
#include <algorithm>
#include <cmath>
#include <filesystem>

TEST(EngineRuntime, DefaultEngineRunsAfterStarterReleaseAndProducesAudio) {
    es_script::Compiler compiler;
    compiler.initialize(ENGINE_SIM_TEST_ASSET_DIRECTORY);
    const auto entry = std::filesystem::path(ENGINE_SIM_TEST_ASSET_DIRECTORY) / "main.mr";
    if (!compiler.compile(entry.string())) {
        compiler.destroy();
        FAIL() << "Default engine script failed to compile";
    }
    auto output = compiler.execute();
    compiler.destroy();
    ASSERT_NE(output.engine, nullptr);
    ASSERT_NE(output.vehicle, nullptr);
    ASSERT_NE(output.transmission, nullptr);

    output.engine->calculateDisplacement();
    auto *simulator = output.engine->createSimulator(output.vehicle, output.transmission, 44100);
    simulator->setSimulationFrequency(output.engine->getSimulationFrequency());
    simulator->setSynthesizerLatencyCorrectionEnabled(false);
    output.engine->getIgnitionModule()->m_enabled = true;
    output.engine->setSpeedControl(0.0);

    int audibleSamples = 0;
    int16_t audio[512] = {};
    for (int frame = 0; frame < 400; ++frame) {
        // Crank for two simulated seconds, then idle for two more.
        simulator->m_starterMotor.m_enabled = frame < 200;
        simulator->startFrame(0.01);
        while (simulator->simulateStep()) { }
        simulator->endFrame();
        simulator->synthesizer().pumpAudioRendering();
        const int received = simulator->readAudioOutput(512, audio);
        audibleSamples += static_cast<int>(std::count_if(audio, audio + received,
            [](int16_t sample) { return std::abs(static_cast<int>(sample)) > 20; }));
    }

    EXPECT_GT(std::abs(output.engine->getRpm()), 200);
    EXPECT_GT(output.engine->getTotalFuelMassConsumed(), 0);
    EXPECT_GT(audibleSamples, 1000);

    simulator->releaseSimulation();
    delete simulator;
    output.engine->destroy();
    delete output.engine;
    delete output.vehicle;
    delete output.transmission;
}
