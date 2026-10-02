#include "audio_engine_runner.h"
#include "audio_tui.h"
#include "compiler.h"
#include "engine.h"
#include "exhaust_system.h"
#include "impulse_response.h"
#include "runtime_paths.h"
#include "sdl_audio_output.h"
#include "simulator.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <cerrno>
#include <cmath>
#include <csignal>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <poll.h>
#include <sstream>
#include <string>
#include <unistd.h>
#include <vector>

namespace {
volatile std::sig_atomic_t interrupted = 0;
void interrupt(int) { interrupted = 1; }

class Tone final : public Simulator {
public:
    Tone() {
        for (std::size_t i = 0; i < samples.size(); ++i)
            samples[i] = static_cast<int16_t>(1500 * std::sin(2 * 3.141592653589793 * i / 220.5));
    }
    int readAudioOutput(int count, int16_t *target) override {
        for (int i = 0; i < count; ++i) {
            target[i] = samples[position];
            position = (position + 1) % samples.size();
        }
        return count;
    }
private:
    void writeToSynthesizer() override { }
    std::array<int16_t, 441> samples{};
    std::size_t position = 0;
};

struct Capture {
    struct Block { Uint64 ns; int frames, rate; float peak; };
    std::vector<Block> blocks;
    std::vector<int16_t> samples;
    std::size_t count = 0, sampleCount = 0;
    bool overflow = false;
    static void SDLCALL postmix(void *context, const SDL_AudioSpec *spec, float *buffer, int bytes) {
        auto &capture = *static_cast<Capture *>(context);
        const auto now = SDL_GetTicksNS();
        const int frames = bytes / (sizeof(float) * spec->channels);
        float peak = 0;
        for (int frame = 0; frame < frames; ++frame) {
            float sample = 0;
            for (int channel = 0; channel < spec->channels; ++channel)
                sample += buffer[frame * spec->channels + channel] / spec->channels;
            peak = std::max(peak, std::abs(sample));
            if (capture.sampleCount < capture.samples.size())
                capture.samples[capture.sampleCount++] = static_cast<int16_t>(std::clamp(sample, -1.0f, 1.0f) * 32767);
            else capture.overflow = true;
        }
        if (capture.count < capture.blocks.size()) capture.blocks[capture.count++] = {now, frames, spec->freq, peak};
        else capture.overflow = true;
    }
    bool save(const std::string &prefix, Uint64 start, int rate) const {
        std::ofstream csv(prefix + ".csv"), wav(prefix + ".wav", std::ios::binary);
        csv << "seconds,frames,rate,peak\n" << std::setprecision(9);
        for (std::size_t i = 0; i < count; ++i) {
            const auto &b = blocks[i];
            csv << (b.ns - start) / 1e9 << ',' << b.frames << ',' << b.rate << ',' << b.peak << '\n';
        }
        const auto u32 = [&](std::uint32_t n) { wav.write(reinterpret_cast<const char *>(&n), 4); };
        const auto u16 = [&](std::uint16_t n) { wav.write(reinterpret_cast<const char *>(&n), 2); };
        const auto bytes = static_cast<std::uint32_t>(sampleCount * 2);
        wav.write("RIFF", 4); u32(36 + bytes); wav.write("WAVEfmt ", 8); u32(16);
        u16(1); u16(1); u32(rate); u32(rate * 2); u16(2); u16(16);
        wav.write("data", 4); u32(bytes);
        wav.write(reinterpret_cast<const char *>(samples.data()), bytes);
        csv.flush(); wav.flush();
        return csv.good() && wav.good();
    }
};

void help(std::ostream &out = std::cout) {
    out << "Audio-only Engine Simulator\n"
        << "Options: --demo --seconds N --verify FILE_PREFIX --tone --ui-stall-ms N --reserve-ms N --no-realtime\n"
        << "         --script FILE --simulation-hz N --demo-throttle 0..100 --help\n"
        << "         --demo-start-throttle 0..100 (held while cranking), --volume 0..100\n"
        << "         --tui (interactive terminal), --log FILE\n"
        << "Commands (press Enter): start, stop, throttle 0..100, volume 0..100, status, help, quit\n"
        << "start turns on ignition and cranks for two seconds; Ctrl-C also quits.\n"
        << "--demo starts, revs at 6-8 seconds, and returns to idle.\n"
        << "--verify saves outgoing PCM and checks for dropouts; requires --demo or --tone.\n"
        << "Engine verification intentionally mutes at 4-5 and 10-11 seconds to measure response.\n";
}
}

int main(int argc, char **argv) {
    bool demo = false, tone = false, realtime = true, useTui = false;
    double seconds = 0, uiStallMs = 0, reserveMs = 30, simulationHz = 0;
    double demoThrottle = 0.4, demoStartThrottle = 0, initialVolume = 1;
    std::string prefix, scriptPath, logPath;
    try {
        for (int i = 1; i < argc; ++i) {
            const std::string option = argv[i];
            if (option == "--help") { help(); return 0; }
            if (option == "--demo") demo = true;
            else if (option == "--tone") tone = true;
            else if (option == "--tui") useTui = true;
            else if (option == "--realtime") realtime = true;
            else if (option == "--no-realtime") realtime = false;
            else if (i + 1 < argc && option == "--seconds") seconds = std::stod(argv[++i]);
            else if (i + 1 < argc && option == "--ui-stall-ms") uiStallMs = std::stod(argv[++i]);
            else if (i + 1 < argc && option == "--reserve-ms") reserveMs = std::stod(argv[++i]);
            else if (i + 1 < argc && option == "--verify") prefix = argv[++i];
            else if (i + 1 < argc && option == "--script") scriptPath = argv[++i];
            else if (i + 1 < argc && option == "--log") logPath = argv[++i];
            else if (i + 1 < argc && option == "--simulation-hz") {
                simulationHz = std::stod(argv[++i]);
                if (!std::isfinite(simulationHz) || simulationHz < 1000 || simulationHz > 50000)
                    throw std::runtime_error("--simulation-hz must be between 1000 and 50000");
            }
            else if (i + 1 < argc && (option == "--demo-throttle" || option == "--demo-start-throttle")) {
                const double throttle = std::stod(argv[++i]) / 100;
                if (!std::isfinite(throttle) || throttle < 0 || throttle > 1)
                    throw std::runtime_error(option + " must be between 0 and 100");
                (option == "--demo-throttle" ? demoThrottle : demoStartThrottle) = throttle;
            }
            else if (i + 1 < argc && option == "--volume") {
                initialVolume = std::stod(argv[++i]) / 100;
                if (!std::isfinite(initialVolume) || initialVolume < 0 || initialVolume > 1)
                    throw std::runtime_error("--volume must be between 0 and 100");
            }
            else throw std::runtime_error("Unknown or incomplete option: " + option);
        }
        if (seconds == 0 && (demo || tone)) seconds = 20;
        if (!std::isfinite(seconds) || seconds < 0 || seconds > 3600
            || !std::isfinite(uiStallMs) || uiStallMs < 0 || uiStallMs > 2000
            || !std::isfinite(reserveMs) || reserveMs < 20 || reserveMs > 100)
            throw std::runtime_error("Invalid duration, stall, or reserve");
        if (!prefix.empty() && ((!demo && !tone) || seconds < 12 || seconds > 120))
            throw std::runtime_error("--verify requires --demo or --tone and 12..120 seconds");
        if (useTui && (tone || !isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO)))
            throw std::runtime_error("--tui requires an interactive terminal and an engine (no --tone)");
    } catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 2; }

    std::ofstream logFile;
    if (!logPath.empty()) {
        logFile.open(logPath);
        if (!logFile) { std::cerr << "Cannot open log: " << logPath << '\n'; return 2; }
    }
    std::ostream &log = logPath.empty() ? std::cout : logFile;
    const bool periodicLog = !useTui || !logPath.empty();
    if (!SDL_Init(SDL_INIT_AUDIO)) { std::cerr << SDL_GetError() << '\n'; return 2; }
    std::signal(SIGINT, interrupt);
    std::signal(SIGTERM, interrupt);
    SdlAudioOutput output;
    AudioEngineRunner runner;
    AudioTui tui;
    Tone toneSource;
    Engine *engine = nullptr;
    Vehicle *vehicle = nullptr;
    Transmission *transmission = nullptr;
    Simulator *simulator = nullptr;
    const auto cleanup = [&] {
        output.stop();
        runner.stop();
        if (simulator) { simulator->releaseSimulation(); delete simulator; }
        if (engine) { engine->destroy(); delete engine; }
        delete vehicle; delete transmission;
        SDL_Quit();
    };
    if (!tone) {
        const RuntimePaths paths = RuntimePaths::discover(SDL_GetBasePath(), ENGINE_SIM_SOURCE_ASSET_DIRECTORY);
        es_script::Compiler compiler;
        compiler.initialize(paths.assetDirectory.string());
        if (scriptPath.empty()) scriptPath = (paths.assetDirectory / "main.mr").string();
        if (!compiler.compile(scriptPath)) {
            compiler.destroy(); std::cerr << "Script compilation failed: " << scriptPath << '\n';
            cleanup(); return 2;
        }
        auto script = compiler.execute();
        compiler.destroy();
        engine = script.engine; vehicle = script.vehicle; transmission = script.transmission;
        if (!engine || !vehicle || !transmission) { std::cerr << "Script did not create an engine/vehicle/transmission\n"; cleanup(); return 2; }
        engine->calculateDisplacement();
        simulator = engine->createSimulator(vehicle, transmission, 44100);
        simulator->setSimulationFrequency(simulationHz > 0 ? simulationHz : engine->getSimulationFrequency());
        auto audio = simulator->synthesizer().getAudioParameters();
        audio.dF_F_mix = static_cast<float>(engine->getInitialHighFrequencyGain());
        audio.inputSampleNoise = static_cast<float>(engine->getInitialJitter());
        audio.airNoise = static_cast<float>(engine->getInitialNoise());
        audio.volume = static_cast<float>(initialVolume);
        simulator->synthesizer().setAudioParameters(audio);
        log << "engine=\"" << engine->getName() << "\" cylinders=" << engine->getCylinderCount()
            << " simulation_hz=" << simulator->getSimulationFrequency()
            << " jitter=" << audio.inputSampleNoise << " volume=" << initialVolume * 100 << std::endl;
        for (int i = 0; i < engine->getExhaustSystemCount(); ++i) {
            auto *ir = engine->getExhaustSystem(i)->getImpulseResponse();
            if (ir && !output.loadImpulseResponse(simulator->synthesizer(), ir->getFilename(), ir->getVolume(), i)) {
                std::cerr << "Impulse response failed: " << ir->getFilename() << '\n'; cleanup(); return 2;
            }
        }
    }
    Capture capture;
    if (!prefix.empty()) {
        capture.blocks.resize(static_cast<std::size_t>((seconds + 5) * 2000));
        capture.samples.resize(static_cast<std::size_t>((seconds + 5) * 192000));
    }
    if (simulator && !runner.start(*simulator, reserveMs / 1000, realtime)) {
        std::cerr << "Audio producer could not initialize\n"; cleanup(); return 2;
    }
    if (!output.start(tone ? &toneSource : simulator)) {
        std::cerr << "Audio output failed: " << SDL_GetError() << '\n'; cleanup(); return 2;
    }
    SDL_AudioSpec spec{};
    int deviceFrames = 0;
    SDL_GetAudioDeviceFormat(output.device(), &spec, &deviceFrames);
    const auto start = SDL_GetTicksNS();
    if (!prefix.empty() && !SDL_SetAudioPostmixCallback(output.device(), Capture::postmix, &capture)) {
        std::cerr << SDL_GetError() << '\n'; cleanup(); return 2;
    }
    log << std::fixed << std::setprecision(3)
        << "device=" << SDL_GetAudioDeviceName(output.device()) << " driver=" << SDL_GetCurrentAudioDriver()
        << " rate=" << spec.freq << " device_frames=" << deviceFrames
        << " mode=" << (tone ? "tone" : "engine") << " reserve_ms=" << (tone ? 0 : reserveMs)
        << " ui_stall_ms=" << uiStallMs << '\n';
    if (!tone) log << "realtime_requested=" << realtime
        << " scheduling_status=" << runner.snapshot().schedulingStatus << '\n';
    if (std::string(SDL_GetCurrentAudioDriver()) == "dummy")
        log << "Silent test: the dummy audio driver does not play through speakers.\n";
    if (!useTui) help(log);
    if (useTui && !tui.enter()) {
        std::cerr << "Could not initialize the terminal interface\n"; cleanup(); return 2;
    }
    AudioTui::State display;
    if (useTui) {
        display.engine = engine->getName();
        display.device = SDL_GetAudioDeviceName(output.device());
        display.redline = engine->getRedline() / units::rpm(1);
        display.silent = std::string(SDL_GetCurrentAudioDriver()) == "dummy";
        display.notice = "Starting engine. Press B for a short rev, or use the arrow keys.";
    }
    auto previous = output.statistics(), warmup = previous;
    bool warmed = false, quit = false, stdinOpen = true;
    struct Marker { double seconds; bool mute; Uint64 ns = 0; };
    std::array<Marker, 4> markers{{{4, true}, {5, false}, {10, true}, {11, false}}};
    double nextLog = 1, nextDraw = 0, elapsed = 0, lastThrottle = -1;
    double controlThrottle = 0, controlVolume = initialVolume, unmutedVolume = initialVolume;
    bool controlIgnition = demo || useTui;
    std::string pending;
    const auto send = [&](AudioEngineRunner::Action action, double value = 0) {
        if (!tone && !runner.command({action, value})) {
            if (useTui) display.notice = "Control queue busy; try again.";
            else std::cerr << "Command queue is full\n";
            return false;
        }
        return true;
    };
    if ((demo || useTui) && !tone) send(AudioEngineRunner::Action::Start, demoStartThrottle);
    const auto status = [&] {
        const auto snapshot = runner.snapshot();
        const auto stats = output.statistics();
        if (periodicLog) log << "t=" << elapsed << " rpm=" << snapshot.rpm << " simulated_s=" << snapshot.simulatedSeconds
            << " queued_ms=" << snapshot.queuedMs << " block_max_ms=" << snapshot.maxBlockMs
            << " pcm=" << stats.pcmFrames - previous.pcmFrames
            << " missing=" << stats.silenceFrames - previous.silenceFrames
            << " throttle_pct=" << snapshot.throttle * 100 << " volume_pct=" << snapshot.volume * 100
            << " ignition=" << snapshot.ignition << " cranking=" << snapshot.cranking
            << " blipping=" << snapshot.blipping << std::endl;
        previous = stats;
    };
    while (!quit && !interrupted) {
        elapsed = (SDL_GetTicksNS() - start) / 1e9;
        if (seconds > 0 && elapsed >= seconds) break;
        if (!warmed && elapsed >= 1) { warmup = output.statistics(); warmed = true; }
        if (demo && !tone) {
            const double throttle = elapsed < 2 ? demoStartThrottle
                : (elapsed >= 6 && elapsed < 8 ? demoThrottle : 0);
            if (throttle != lastThrottle) { send(AudioEngineRunner::Action::Throttle, throttle); lastThrottle = throttle; }
            if (!prefix.empty()) for (auto &marker : markers) {
                if (!marker.ns && elapsed >= marker.seconds) {
                    marker.ns = SDL_GetTicksNS();
                    send(AudioEngineRunner::Action::Volume, marker.mute ? 0 : initialVolume);
                    if (periodicLog) log << "event t=" << elapsed << ' ' << (marker.mute ? "mute" : "unmute") << std::endl;
                }
            }
        }
        pollfd input{STDIN_FILENO, POLLIN, 0};
        if (stdinOpen && poll(&input, 1, 0) > 0 && (input.revents & (POLLIN | POLLHUP))) {
            char buffer[1024];
            const auto count = read(STDIN_FILENO, buffer, sizeof(buffer));
            if (count == 0 || (count < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)) {
                stdinOpen = false; if (seconds == 0 || useTui) quit = true;
            }
            else if (count > 0 && useTui) {
                for (int i = 0; i < count; ++i) {
                    const auto key = tui.decode(static_cast<unsigned char>(buffer[i]));
                    using Action = AudioEngineRunner::Action;
                    using Key = AudioTui::Key;
                    double value;
                    switch (key) {
                    case Key::StartStop:
                        if (send(controlIgnition ? Action::Stop : Action::Start, demoStartThrottle)) {
                            controlIgnition = !controlIgnition;
                            if (!controlIgnition) controlThrottle = 0;
                            display.notice = controlIgnition ? "Starting engine." : "Ignition off.";
                        }
                        break;
                    case Key::ThrottleUp: case Key::ThrottleDown: case Key::Idle:
                        value = key == Key::Idle ? 0 : std::clamp(controlThrottle
                            + (key == Key::ThrottleUp ? 0.0025 : -0.0025), 0.0, 1.0);
                        if (send(Action::Throttle, value)) controlThrottle = value;
                        display.notice = "Throttle set; 0 returns to idle.";
                        break;
                    case Key::Blip:
                        send(Action::Blip, std::max(controlThrottle, demoThrottle));
                        display.notice = "Brief rev; returns automatically to your throttle setting.";
                        break;
                    case Key::VolumeUp: case Key::VolumeDown: case Key::Mute:
                        value = key == Key::Mute ? (controlVolume > 0 ? 0 : std::max(0.05, unmutedVolume))
                            : std::clamp(controlVolume + (key == Key::VolumeUp ? 0.05 : -0.05), 0.0, 1.0);
                        if (send(Action::Volume, value)) {
                            if (value == 0 && controlVolume > 0) unmutedVolume = controlVolume;
                            controlVolume = value;
                        }
                        display.notice = controlVolume == 0 ? "Muted. Press M to restore volume." : "Volume adjusted.";
                        break;
                    case Key::Quit: quit = true; break;
                    case Key::None: break;
                    }
                    if (periodicLog && key != Key::None)
                        log << "control t=" << elapsed << " key=" << static_cast<int>(key)
                            << " throttle_pct=" << controlThrottle * 100 << " volume_pct=" << controlVolume * 100
                            << " ignition=" << controlIgnition << std::endl;
                }
            }
            else if (count > 0) pending.append(buffer, static_cast<std::size_t>(count));
            std::size_t newline;
            while ((newline = pending.find('\n')) != std::string::npos) {
                std::istringstream line(pending.substr(0, newline));
                pending.erase(0, newline + 1);
                std::string command, extra; double value;
                line >> command;
                if (command == "quit") quit = true;
                else if (command == "help") help(log);
                else if (command == "status") status();
                else if (command == "start") send(AudioEngineRunner::Action::Start);
                else if (command == "stop") send(AudioEngineRunner::Action::Stop);
                else if ((command == "throttle" || command == "volume") && (line >> value)
                    && !(line >> extra) && std::isfinite(value) && value >= 0 && value <= 100)
                    send(command == "throttle" ? AudioEngineRunner::Action::Throttle : AudioEngineRunner::Action::Volume, value / 100);
                else if (!command.empty()) std::cerr << "Unknown command or invalid value; type help\n";
            }
            if (pending.size() > 4096) { pending.clear(); std::cerr << "Command too long\n"; }
        }
        if (elapsed >= nextLog) { status(); nextLog = elapsed + 1; }
        if (useTui && elapsed >= nextDraw) {
            const auto snapshot = runner.snapshot();
            display.rpm = snapshot.rpm; display.throttle = snapshot.throttle; display.volume = snapshot.volume;
            display.ignition = snapshot.ignition; display.cranking = snapshot.cranking; display.blipping = snapshot.blipping;
            display.missingFrames = output.statistics().silenceFrames;
            tui.draw(display);
            nextDraw = elapsed + 0.1;
        }
        // Stress only the observer/control thread. The producer must continue.
        SDL_Delay(uiStallMs > 0 ? static_cast<Uint32>(uiStallMs) : 10);
    }
    if (!prefix.empty()) SDL_SetAudioPostmixCallback(output.device(), nullptr, nullptr);
    output.stop(); runner.stop();
    tui.leave();
    const auto stats = output.statistics();
    const auto snapshot = runner.snapshot();
    double maxGapMs = 0, maxSilenceMs = 0, silenceMs = 0, maxPeak = 0;
    for (std::size_t i = 0; i < capture.count; ++i) {
        const auto &block = capture.blocks[i];
        if (i) maxGapMs = std::max(maxGapMs, (block.ns - capture.blocks[i - 1].ns) / 1e6);
        maxPeak = std::max(maxPeak, static_cast<double>(block.peak));
        bool intentionalMute = false;
        if (!tone) for (int marker = 0; marker < 4; marker += 2)
            intentionalMute |= markers[marker].ns && block.ns >= markers[marker].ns
                && (!markers[marker + 1].ns || block.ns <= markers[marker + 1].ns + 100000000);
        if ((block.ns - start) / 1e9 > 2 && !intentionalMute && block.peak < 1e-6) silenceMs += block.frames * 1000.0 / block.rate;
        else silenceMs = 0;
        maxSilenceMs = std::max(maxSilenceMs, silenceMs);
    }
    const bool saved = prefix.empty() || capture.save(prefix, start, spec.freq);
    bool latencyPass = true;
    if (!prefix.empty() && !tone) for (const auto &marker : markers) {
        double latencyMs = -1;
        for (std::size_t i = 0; marker.ns && i < capture.count; ++i) {
            const auto &block = capture.blocks[i];
            if (block.ns >= marker.ns && ((block.peak < 1e-6) == marker.mute)) {
                latencyMs = (block.ns - marker.ns) / 1e6;
                break;
            }
        }
        latencyPass &= latencyMs >= 0 && latencyMs < 100;
        log << "latency event=" << (marker.mute ? "mute" : "unmute")
            << " at=" << marker.seconds << " command_to_device_mix_ms=" << latencyMs << std::endl;
    }
    const auto missing = stats.silenceFrames - warmup.silenceFrames;
    const auto nearFullScale = std::count_if(capture.samples.begin(), capture.samples.begin() + capture.sampleCount,
        [](int16_t sample) { return sample <= -32760 || sample >= 32760; });
    const bool signalOk = prefix.empty() || (maxPeak > 1e-4 && maxSilenceMs == 0 && !capture.overflow);
    const bool pass = saved && missing == 0 && stats.writeErrors == 0 && signalOk && latencyPass
        && (!demo || tone || snapshot.rpm > 200);
    log << "SUMMARY result=" << (pass ? "PASS" : "FAIL") << " duration_s=" << elapsed
        << " driver=" << SDL_GetCurrentAudioDriver() << " missing_frames=" << missing
        << " total_missing_frames=" << stats.silenceFrames << " short_reads=" << stats.shortReads
        << " reader_max_ms=" << stats.longestReadNs / 1e6 << " producer_block_max_ms=" << snapshot.maxBlockMs
        << " producer_cpu_percent=" << snapshot.cpuSeconds / std::max(0.001, elapsed) * 100
        << " producer_cpu_block_max_ms=" << snapshot.maxCpuBlockMs
        << " producer_wakeup_overrun_max_ms=" << snapshot.maxWakeupOverrunMs
        << " callback_gap_max_ms=" << maxGapMs << " silence_max_ms=" << maxSilenceMs
        << " capture_enabled=" << !prefix.empty() << " near_full_scale_samples=" << nearFullScale
        << " ui_frames=" << tui.frames() << " ui_skipped_frames=" << tui.skippedFrames()
        << " final_rpm=" << snapshot.rpm << " write_errors=" << stats.writeErrors
        << " saved=" << saved << " capture_overflow=" << capture.overflow << std::endl;
    if (stats.silenceFrames == 0 && stats.writeErrors == 0)
        log << "Audio delivery: no buffer underruns or write errors detected.\n";
    else
        log << "Audio delivery: " << stats.silenceFrames * 1000.0 / 44100
            << " ms of missing samples, " << stats.writeErrors << " write errors.\n";
    if (demo && !tone && snapshot.rpm <= 200)
        log << "Engine check failed: the engine did not stay running.\n";
    if (useTui && !logPath.empty()) std::cout << "Playback log: " << logPath << '\n';
    cleanup();
    return pass ? 0 : 1;
}
