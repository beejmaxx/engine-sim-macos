#include "compiler.h"
#include "engine.h"
#include "exhaust_system.h"
#include "ignition_module.h"
#include "impulse_response.h"
#include "sdl_audio_output.h"
#include "simulator.h"

#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

namespace {
std::uint64_t threadCpuNs() {
#if defined(CLOCK_THREAD_CPUTIME_ID)
    timespec time{};
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &time);
    return static_cast<std::uint64_t>(time.tv_sec) * 1000000000 + time.tv_nsec;
#else
    return static_cast<std::uint64_t>(std::clock()) * 1000000000 / CLOCKS_PER_SEC;
#endif
}
struct Block {
    std::uint64_t ns;
    int frames, rate;
    float peak, rms;
};
struct Capture {
    std::vector<Block> blocks;
    std::vector<std::int16_t> samples;
    std::size_t blockCount = 0, sampleCount = 0;
    bool overflow = false;
};
void SDLCALL postmix(void *userdata, const SDL_AudioSpec *spec, float *buffer, int bytes) {
    auto &capture = *static_cast<Capture *>(userdata);
    const auto ns = SDL_GetTicksNS();
    const int frames = bytes / (sizeof(float) * spec->channels);
    float peak = 0;
    double squares = 0;
    for (int frame = 0; frame < frames; ++frame) {
        float sample = 0;
        for (int channel = 0; channel < spec->channels; ++channel)
            sample += buffer[frame * spec->channels + channel] / spec->channels;
        peak = std::max(peak, std::abs(sample));
        squares += sample * sample;
        if (capture.sampleCount < capture.samples.size())
            capture.samples[capture.sampleCount++] = static_cast<std::int16_t>(
                std::clamp(sample, -1.0f, 1.0f) * 32767);
        else capture.overflow = true;
    }
    if (capture.blockCount < capture.blocks.size())
        capture.blocks[capture.blockCount++] = {ns, frames, spec->freq, peak,
            static_cast<float>(std::sqrt(squares / std::max(1, frames)))};
    else capture.overflow = true;
}
void writeWav(const std::string &path, const Capture &capture, int rate) {
    std::ofstream file(path, std::ios::binary);
    const auto u32 = [&](std::uint32_t n) { file.write(reinterpret_cast<const char *>(&n), 4); };
    const auto u16 = [&](std::uint16_t n) { file.write(reinterpret_cast<const char *>(&n), 2); };
    const auto bytes = static_cast<std::uint32_t>(capture.sampleCount * 2);
    file.write("RIFF", 4); u32(36 + bytes); file.write("WAVEfmt ", 8); u32(16);
    u16(1); u16(1); u32(rate); u32(rate * 2); u16(2); u16(16);
    file.write("data", 4); u32(bytes);
    file.write(reinterpret_cast<const char *>(capture.samples.data()), bytes);
}
struct Marker { double seconds; const char *name; bool silence; std::uint64_t ns = 0; };
}

int main(int argc, char **argv) {
    const std::string prefix = argc > 1 ? argv[1] : "audio-probe";
    const double seconds = argc > 2 ? std::stod(argv[2]) : 30.0;
    const bool jitter = argc > 3 && std::string(argv[3]) == "jitter";
    const double inputLimit = argc > 4 ? std::stod(argv[4]) : 0.06;
    if (seconds < 12 || seconds > 120) return 2;
    if (!SDL_Init(SDL_INIT_AUDIO)) { std::cerr << SDL_GetError() << '\n'; return 2; }

    es_script::Compiler compiler;
    compiler.initialize(ENGINE_SIM_TEST_ASSET_DIRECTORY);
    if (!compiler.compile((std::filesystem::path(ENGINE_SIM_TEST_ASSET_DIRECTORY) / "main.mr").string())) {
        std::cerr << "script compilation failed\n"; return 2;
    }
    auto script = compiler.execute();
    compiler.destroy();
    auto *engine = script.engine;
    if (!engine || !script.vehicle || !script.transmission) return 2;
    engine->calculateDisplacement();
    auto *sim = engine->createSimulator(script.vehicle, script.transmission, 44100);
    sim->setSimulationFrequency(engine->getSimulationFrequency());
    sim->setSynthesizerLatencyCorrectionEnabled(false);
    sim->setMaximumSynthesizerInputLatency(inputLimit);
    SdlAudioOutput output;
    for (int i = 0; i < engine->getExhaustSystemCount(); ++i) {
        auto *ir = engine->getExhaustSystem(i)->getImpulseResponse();
        if (ir && !output.loadImpulseResponse(sim->synthesizer(), ir->getFilename(), ir->getVolume(), i)) {
            std::cerr << "impulse response failed: " << ir->getFilename() << '\n'; return 2;
        }
    }
    Capture capture;
    capture.blocks.resize(static_cast<std::size_t>((seconds + 5) * 2000));
    capture.samples.resize(static_cast<std::size_t>((seconds + 5) * 192000));
    sim->startAudioRenderingThread();
    // Match the native host's initial reserve before starting the device clock.
    for (int block = 0; block < 6; ++block) {
        sim->startFrame(0.01);
        while (sim->simulateStep()) { }
        sim->endFrame();
    }
    if (!output.start(sim)) { std::cerr << SDL_GetError() << '\n'; return 2; }
    SDL_AudioSpec spec{};
    int deviceFrames = 0;
    SDL_GetAudioDeviceFormat(output.device(), &spec, &deviceFrames);
    const auto startNs = SDL_GetTicksNS();
    if (!SDL_SetAudioPostmixCallback(output.device(), postmix, &capture)) return 2;
    std::cout << std::fixed << std::setprecision(3)
        << "device=" << SDL_GetAudioDeviceName(output.device())
        << " driver=" << SDL_GetCurrentAudioDriver() << " rate=" << spec.freq
        << " device_frames=" << deviceFrames << " device_period_ms=" << deviceFrames * 1000.0 / spec.freq
        << " simulation_hz=" << sim->getSimulationFrequency() << " jitter=" << jitter
        << " input_limit_ms=" << inputLimit * 1000 << std::endl;

    std::vector<Marker> markers{{4, "mute", true}, {4.25, "unmute", false},
        {10, "mute", true}, {10.25, "unmute", false}};
    auto audioParameters = sim->synthesizer().getAudioParameters();
    const float normalVolume = audioParameters.volume;
    auto lastTick = SDL_GetTicks();
    std::uint64_t nextLog = lastTick + 1000;
    auto previous = output.statistics();
    auto warmup = previous;
    double maxInputMs = 0, maxOutputMs = 0, maxPhysicsMs = 0;
    double maxSleepOverrunMs = 0, totalPhysicsCpuMs = 0, droppedWallMs = 0;
    double elapsed = 0;
    int tick = 0;
    engine->getIgnitionModule()->m_enabled = true;
    engine->setSpeedControl(0.0);
    std::cout << "event t=0 ignition_on starter_on\n";
    while (elapsed < seconds) {
        const auto now = SDL_GetTicks();
        elapsed = (SDL_GetTicksNS() - startNs) / 1e9;
        sim->m_starterMotor.m_enabled = elapsed < 2.0;
        engine->setSpeedControl(elapsed >= 6 && elapsed < 8 ? 0.4 : 0.0);
        for (auto &marker : markers) {
            if (!marker.ns && elapsed >= marker.seconds) {
                marker.ns = SDL_GetTicksNS();
                audioParameters.volume = marker.silence ? 0.0f : normalVolume;
                sim->synthesizer().setAudioParameters(audioParameters);
                std::cout << "event t=" << elapsed << ' ' << marker.name << std::endl;
            }
        }
        const double wallSeconds = (now - lastTick) / 1000.0;
        droppedWallMs += std::max(0.0, wallSeconds - 0.25) * 1000;
        double remaining = std::min(wallSeconds, 0.25);
        lastTick = now;
        const auto physicsStart = SDL_GetTicksNS();
        const auto physicsCpuStart = threadCpuNs();
        const double requestedMs = remaining * 1000;
        while (remaining > 0) {
            const double block = std::min(remaining, 0.01);
            sim->startFrame(block);
            while (sim->simulateStep()) { }
            sim->endFrame();
            remaining -= block;
        }
        const double physicsMs = (SDL_GetTicksNS() - physicsStart) / 1e6;
        const double physicsCpuMs = (threadCpuNs() - physicsCpuStart) / 1e6;
        totalPhysicsCpuMs += physicsCpuMs;
        maxPhysicsMs = std::max(maxPhysicsMs, physicsMs);
        if (physicsMs > 20) std::cout << "slow_tick t=" << elapsed << " requested_ms=" << requestedMs
            << " wall_ms=" << physicsMs << " cpu_ms=" << physicsCpuMs << std::endl;
        maxInputMs = std::max(maxInputMs, sim->getSynthesizerInputLatency() * 1000);
        maxOutputMs = std::max(maxOutputMs, sim->getSynthesizerOutputLatency() * 1000);
        if (now >= nextLog) {
            const auto stats = output.statistics();
            if (elapsed < 2) warmup = stats;
            std::cout << "t=" << elapsed << " rpm=" << engine->getRpm()
                << " pcm=" << stats.pcmFrames - previous.pcmFrames
                << " inserted_silence=" << stats.silenceFrames - previous.silenceFrames
                << " short_reads=" << stats.shortReads - previous.shortReads
                << " read_max_ms=" << stats.longestReadNs / 1e6
                << " input_ms=" << sim->getSynthesizerInputLatency() * 1000
                << " output_ms=" << sim->getSynthesizerOutputLatency() * 1000 << std::endl;
            previous = stats;
            nextLog = now + 1000;
        }
        // Same elapsed-time scheduling as the desktop; inject a slow render
        // frame periodically when requested, without creating a GUI.
        const int sleepMs = jitter && ++tick % 25 == 0 ? 50 : 1;
        const auto sleepStart = SDL_GetTicksNS();
        SDL_Delay(sleepMs);
        const double sleepOverrunMs = (SDL_GetTicksNS() - sleepStart) / 1e6 - sleepMs;
        maxSleepOverrunMs = std::max(maxSleepOverrunMs, sleepOverrunMs);
        if (sleepOverrunMs > 20) std::cout << "slow_wakeup t=" << elapsed
            << " requested_ms=" << sleepMs << " extra_ms=" << sleepOverrunMs << std::endl;
    }
    SDL_SetAudioPostmixCallback(output.device(), nullptr, nullptr);
    output.stop();
    const auto stats = output.statistics();
    sim->endAudioRenderingThread();
    std::ofstream csv(prefix + ".csv");
    csv << "seconds,frames,rate,peak,rms\n" << std::setprecision(9);
    double maxGapMs = 0, maxUnexpectedSilenceMs = 0, silentMs = 0;
    double sumSquares = 0;
    for (std::size_t i = 0; i < capture.blockCount; ++i) {
        const auto &block = capture.blocks[i];
        const double t = (block.ns - startNs) / 1e9;
        csv << t << ',' << block.frames << ',' << block.rate << ',' << block.peak << ',' << block.rms << '\n';
        if (i) maxGapMs = std::max(maxGapMs, (block.ns - capture.blocks[i - 1].ns) / 1e6);
        const bool intentionalMute = (t >= 4 && t <= 4.4) || (t >= 10 && t <= 10.4);
        if (t > 2 && !intentionalMute && block.peak < 1e-6) silentMs += block.frames * 1000.0 / block.rate;
        else silentMs = 0;
        maxUnexpectedSilenceMs = std::max(maxUnexpectedSilenceMs, silentMs);
        sumSquares += block.rms * block.rms;
    }
    bool latencyPass = true;
    for (const auto &marker : markers) {
        double latencyMs = -1;
        for (std::size_t i = 0; i < capture.blockCount; ++i) {
            const auto &block = capture.blocks[i];
            if (block.ns >= marker.ns && ((block.peak < 1e-6) == marker.silence)) {
                latencyMs = (block.ns - marker.ns) / 1e6;
                break;
            }
        }
        latencyPass &= latencyMs >= 0 && latencyMs < 100;
        std::cout << "latency event=" << marker.name << " at=" << marker.seconds
            << " to_device_mix_ms=" << latencyMs << std::endl;
    }
    writeWav(prefix + ".wav", capture, spec.freq);
    const auto missing = stats.silenceFrames - warmup.silenceFrames;
    const auto produced = stats.pcmFrames - warmup.pcmFrames;
    const double underrunPercent = 100.0 * missing / std::max<std::uint64_t>(1, missing + produced);
    // Even a single missing frame fails the no-dropout check. A small average
    // percentage can conceal an audible burst of silence.
    const bool pass = latencyPass && missing == 0 && maxUnexpectedSilenceMs == 0
        && !capture.overflow && stats.writeErrors == 0 && engine->getRpm() > 200 && sumSquares > 0.01;
    std::cout << "SUMMARY result=" << (pass ? "PASS" : "FAIL")
        << " duration_s=" << elapsed << " recorded_s=" << capture.sampleCount / static_cast<double>(spec.freq)
        << " underrun_percent=" << underrunPercent << " inserted_silence_frames=" << missing
        << " short_reads=" << stats.shortReads << " read_max_ms=" << stats.longestReadNs / 1e6
        << " callback_gap_max_ms=" << maxGapMs << " unexpected_silence_max_ms=" << maxUnexpectedSilenceMs
        << " input_queue_max_ms=" << maxInputMs << " output_queue_max_ms=" << maxOutputMs
        << " physics_tick_max_ms=" << maxPhysicsMs << " final_rpm=" << engine->getRpm()
        << " physics_cpu_percent=" << totalPhysicsCpuMs / (elapsed * 10)
        << " wakeup_overrun_max_ms=" << maxSleepOverrunMs << " capped_wall_ms=" << droppedWallMs
        << " write_errors=" << stats.writeErrors << " capture_overflow=" << capture.overflow << std::endl;
    sim->releaseSimulation();
    delete sim;
    engine->destroy(); delete engine;
    delete script.vehicle; delete script.transmission;
    SDL_Quit();
    return pass ? 0 : 1;
}
