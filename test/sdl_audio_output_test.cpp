#include "sdl_audio_output.h"
#include "simulator.h"
#include "synthesizer.h"

#include <SDL3/SDL.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <filesystem>

namespace {
class SilentSimulator final : public Simulator {
public:
    int readAudioOutput(int samples, int16_t *target) override {
        std::fill(target, target + samples, 0);
        return samples;
    }

protected:
    void writeToSynthesizer() override { }
};

class ShortReadSimulator final : public Simulator {
public:
    int readAudioOutput(int samples, int16_t *target) override {
        const int available = samples / 2;
        std::fill(target, target + available, 1000);
        return available;
    }
protected:
    void writeToSynthesizer() override { }
};

class ConstantSimulator final : public Simulator {
public:
    int readAudioOutput(int samples, int16_t *target) override {
        std::fill(target, target + samples, 8192);
        return samples;
    }
protected:
    void writeToSynthesizer() override { }
};
}

TEST(SdlAudioOutput, ConvertsWavAndClosesDummyDevice) {
    ASSERT_TRUE(SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy"));
    ASSERT_TRUE(SDL_Init(SDL_INIT_AUDIO));

    Synthesizer synthesizer;
    Synthesizer::Parameters parameters;
    parameters.inputChannelCount = 1;
    synthesizer.initialize(parameters);

    SdlAudioOutput output;
    const std::filesystem::path wav = std::filesystem::path(ENGINE_SIM_TEST_ASSET_DIRECTORY)
        / "es/sound-library/smooth/smooth_39.wav";
    EXPECT_TRUE(output.loadImpulseResponse(synthesizer, wav.string(), 1.0f, 0));

    SilentSimulator simulator;
    EXPECT_TRUE(output.start(&simulator));
    output.stop();
    output.stop();

    synthesizer.destroy();
    SDL_Quit();
}

TEST(SdlAudioOutput, DeviceCallbackReportsMissingFrames) {
    ASSERT_TRUE(SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy"));
    ASSERT_TRUE(SDL_Init(SDL_INIT_AUDIO));
    ShortReadSimulator simulator;
    SdlAudioOutput output;
    ASSERT_TRUE(output.start(&simulator));
    const auto deadline = SDL_GetTicks() + 2000;
    while (output.statistics().shortReads < 2 && SDL_GetTicks() < deadline) SDL_Delay(1);
    output.stop();
    const auto stats = output.statistics();
    EXPECT_GE(stats.shortReads, 2u);
    EXPECT_GT(stats.pcmFrames, 0u);
    EXPECT_EQ(stats.silenceFrames, stats.pcmFrames);
    EXPECT_EQ(stats.writeErrors, 0u);
    SDL_Quit();
}

TEST(SdlAudioOutput, FullVisualQueueDoesNotHoldUpAudio) {
    ASSERT_TRUE(SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy"));
    ASSERT_TRUE(SDL_Init(SDL_INIT_AUDIO));
    ConstantSimulator simulator;
    SdlAudioOutput output;
    output.enableVisualization(true);
    ASSERT_TRUE(output.start(&simulator));

    // Deliberately never drain the visual queue until the device has consumed
    // far more PCM than its 16 blocks can hold. A blocked push fails this test.
    const auto deadline = SDL_GetTicks() + 2000;
    while (output.statistics().pcmFrames < 32768 && SDL_GetTicks() < deadline) SDL_Delay(1);
    output.stop();
    const auto stats = output.statistics();
    EXPECT_GE(stats.pcmFrames, 32768u);
    EXPECT_EQ(stats.silenceFrames, 0u);
    EXPECT_EQ(stats.writeErrors, 0u);
    SdlAudioOutput::VisualSamples samples;
    int blocks = 0;
    while (output.readVisualization(samples)) {
        ++blocks;
        ASSERT_GT(samples.count, 0);
        ASSERT_LE(samples.count, 128);
        for (int i = 0; i < samples.count; ++i) EXPECT_FLOAT_EQ(samples.samples[i], .25f);
    }
    EXPECT_EQ(blocks, 16);
    SDL_Quit();
}
