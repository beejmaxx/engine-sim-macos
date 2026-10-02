#include "compiler.h"
#include "engine.h"
#include "ignition_module.h"
#include "simulator.h"
#include "automatic_transmission.h"

#include <gtest/gtest.h>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <chrono>

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

TEST(EngineRuntime, AutomaticDrivetrainAcceleratesShiftsAndStopsUnderVehicleLoad) {
    const std::filesystem::path assets(ENGINE_SIM_TEST_ASSET_DIRECTORY);
    for (const char *script : {"engines/atg-video-2/03_2jz.mr", "engines/porsche/01_porsche_911_gt3.mr"}) {
        SCOPED_TRACE(script);
        const auto entry = std::filesystem::temp_directory_path() / ("engine-sim-auto-" +
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".mr");
        { std::ofstream file(entry); file << "import " << std::quoted((assets / script).generic_string()) << "\nmain()\n"; }
        es_script::Compiler compiler;
        compiler.initialize(assets.string());
        const bool compiled = compiler.compile(entry.string());
        std::filesystem::remove(entry);
        if (!compiled) { compiler.destroy(); FAIL() << "Drive test script failed to compile"; }
        auto output = compiler.execute();
        compiler.destroy();
        ASSERT_NE(output.engine, nullptr);
        ASSERT_NE(output.vehicle, nullptr);
        ASSERT_NE(output.transmission, nullptr);
        output.engine->calculateDisplacement();
        auto *simulator = output.engine->createSimulator(output.vehicle, output.transmission, 44100);
        simulator->setSimulationFrequency(5000);
        simulator->setSynthesizerLatencyCorrectionEnabled(false);
        output.engine->getIgnitionModule()->m_enabled = true;
        AutomaticTransmission automatic;
        automatic.setEnabled(true, *output.transmission);
        int maxGear = 0, previousGear = 0, upshifts = 0, audible = 0;
        double maxSpeed = 0, beforeShiftRpm = 0, biggestRpmDrop = 0, afterShiftTime = -1;
        bool torqueCut = false;
        int16_t samples[512]{};
        for (int frame = 0; frame < 5000; ++frame) {
            const double time = frame * .01;
            const bool cranking = time < 2;
            const double pedal = time >= 3 && time < 35 ? 1 : 0;
            output.vehicle->setBrake(time >= 35 ? 1 : 0);
            simulator->m_starterMotor.m_enabled = cranking;
            const double applied = automatic.update(*output.transmission, *output.engine,
                *output.vehicle, pedal, cranking, .01);
            torqueCut |= pedal == 1 && automatic.shifting() && applied < .5;
            output.engine->setSpeedControl(std::max(applied, cranking ? .02 : 0));
            simulator->startFrame(.01);
            while (simulator->simulateStep()) { }
            simulator->endFrame();
            while (simulator->synthesizer().pumpAudioRendering()) { }
            const int count = simulator->readAudioOutput(512, samples);
            audible += std::count_if(samples, samples + count, [](int16_t v) { return std::abs(int(v)) > 20; });
            const int gear = output.transmission->getGear();
            const double rpm = output.engine->getRpm();
            maxGear = std::max(maxGear, gear);
            maxSpeed = std::max(maxSpeed, output.vehicle->getSpeed());
            if (gear > previousGear && time < 35) {
                ++upshifts; beforeShiftRpm = rpm; afterShiftTime = time;
            }
            if (afterShiftTime >= 0 && time - afterShiftTime < .8)
                biggestRpmDrop = std::max(biggestRpmDrop, beforeShiftRpm - rpm);
            previousGear = gear;
        }
        EXPECT_GE(maxGear, 2);
        EXPECT_GE(upshifts, 2);
        EXPECT_GT(maxSpeed, 15);
        EXPECT_GT(biggestRpmDrop, 300);
        EXPECT_TRUE(torqueCut);
        EXPECT_GT(audible, 10000);
        EXPECT_LT(output.vehicle->getSpeed(), 1);
        EXPECT_GT(output.engine->getRpm(), 400);
        EXPECT_EQ(output.transmission->getGear(), 0);
        automatic.setEnabled(false, *output.transmission);
        EXPECT_EQ(output.transmission->getGear(), -1);
        EXPECT_EQ(output.transmission->getClutchPressure(), 0);
        std::cout << "DRIVE " << script << " max_gear=" << maxGear + 1 << " upshifts=" << upshifts
            << " peak_mph=" << maxSpeed / .44704 << " rpm_drop=" << biggestRpmDrop
            << " stopped_mph=" << output.vehicle->getSpeed() / .44704
            << " idle_rpm=" << output.engine->getRpm() << '\n';
        simulator->releaseSimulation(); delete simulator;
        output.engine->destroy(); delete output.engine;
        delete output.vehicle; delete output.transmission;
    }
}
