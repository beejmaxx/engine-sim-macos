#include "sound_session.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <vector>

namespace {
struct Capture {
    struct Block { Uint64 ns; float peak; int frames; };
    std::vector<Block> blocks;
    std::vector<std::int16_t> samples;
    std::size_t blockCount = 0, sampleCount = 0;
    bool overflow = false;
    static void SDLCALL mix(void *context, const SDL_AudioSpec *spec, float *data, int bytes) {
        auto &capture = *static_cast<Capture *>(context);
        const auto now = SDL_GetTicksNS();
        const int frames = bytes / (sizeof(float) * spec->channels);
        float peak = 0;
        for (int frame = 0; frame < frames; ++frame) {
            float sample = 0;
            for (int channel = 0; channel < spec->channels; ++channel)
                sample += data[frame * spec->channels + channel] / spec->channels;
            peak = std::max(peak, std::abs(sample));
            if (capture.sampleCount < capture.samples.size())
                capture.samples[capture.sampleCount++] = static_cast<std::int16_t>(std::clamp(sample, -1.0f, 1.0f) * 32767);
            else capture.overflow = true;
        }
        if (capture.blockCount < capture.blocks.size()) capture.blocks[capture.blockCount++] = {now, peak, frames};
        else capture.overflow = true;
    }
    bool save(const std::string &prefix, Uint64 start, int rate) const {
        std::ofstream csv(prefix + ".csv"), wav(prefix + ".wav", std::ios::binary);
        csv << "seconds,frames,peak\n" << std::setprecision(9);
        for (std::size_t i = 0; i < blockCount; ++i)
            csv << (blocks[i].ns - start) / 1e9 << ',' << blocks[i].frames << ',' << blocks[i].peak << '\n';
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
}

int verifySoundSession(const std::filesystem::path &assets, std::size_t preset,
    const std::string &prefix, int uiStallMs) {
    std::ofstream log(prefix + ".log");
    if (!log) { std::cerr << "Cannot create test log: " << prefix << '\n'; return 2; }
    SoundSession session;
    bool passed = true;
    const auto check = [&](bool condition, const char *name) {
        log << "CHECK " << name << '=' << (condition ? "PASS" : "FAIL") << std::endl;
        passed = passed && condition;
    };
    check(!session.open(99, assets) && !session.ready(), "invalid_preset_rejected");
    if (!session.open(preset, assets)) { std::cerr << session.error() << '\n'; return 2; }
    check(!session.command(AudioEngineRunner::Action::Throttle, NAN), "invalid_command_rejected");
    Capture capture;
    // Fixed storage allocated before attaching the callback. The callback does
    // no allocation or file I/O; files are written only after it is detached.
    capture.blocks.resize(60000);
    capture.samples.resize(30 * 192000);
    SDL_AudioSpec spec{}; int frames = 0;
    SDL_GetAudioDeviceFormat(session.device(), &spec, &frames);
    const auto start = SDL_GetTicksNS();
    if (!SDL_SetAudioPostmixCallback(session.device(), Capture::mix, &capture)) return 2;
    struct Marker { double at; bool mute; Uint64 ns = 0; };
    std::vector<Marker> markers{{4, true}, {5, false}, {12, true}, {13, false}};
    log << "preset=" << session.preset().id << " driver=" << SDL_GetCurrentAudioDriver()
        << " rate=" << spec.freq << " ui_stall_ms=" << uiStallMs << std::endl;
    log << "assets=" << assets << '\n';
    if (std::string(SDL_GetCurrentAudioDriver()) == "dummy")
        log << "Silent test: no output to speakers.\n";
    check(session.visualLayout().cylinderCount>0 && session.visualLayout().bankCount>0 &&
        session.visualLayout().displacementLiters>0,"visual_geometry_available");
    check(session.startEngine(), "start_queued");
    SdlAudioOutput::Statistics warmup{};
    int stage = 0;
    double lastLog = -1;
    double elapsed = 0, rpmBeforeRev = 0, highestRev = 0;
    while ((elapsed = (SDL_GetTicksNS() - start) / 1e9) < 22) {
        auto state = session.snapshot();
        const auto audio = session.statistics();
        if (elapsed < 1) warmup = audio;
        for (auto &marker : markers) {
            if (!marker.ns && elapsed >= marker.at) {
                marker.ns = SDL_GetTicksNS();
                check(session.command(AudioEngineRunner::Action::Volume, marker.mute ? 0 : SoundSession::DefaultVolume), "volume_queued");
            }
        }
        if (stage == 0 && elapsed >= 3) {
            check(state.ignition && !state.cranking && state.rpm > 200, "started");
            rpmBeforeRev = state.rpm; ++stage;
        } else if (stage == 1 && elapsed >= 6) {
            check(session.command(AudioEngineRunner::Action::Throttle, session.preset().revThrottle), "throttle_queued"); ++stage;
        } else if (stage == 2 && elapsed >= 8) {
            check(std::abs(state.throttle - session.preset().revThrottle) < .001, "throttle_applied");
            check(state.rpm > rpmBeforeRev + 300, "rev_increases_rpm");
            session.command(AudioEngineRunner::Action::Throttle, 0);
            check(session.rev(), "blip_queued");
            // The GUI could stall at this exact moment; the rev must end anyway.
            SDL_Delay(1200); ++stage;
        } else if (stage == 3 && elapsed >= 9.3) {
            check(!state.blipping && state.throttle == 0, "blip_ends_without_ui");
            EngineVisualSnapshot visual;
            while(session.readEngineVisualization(visual)) {}
            check(state.blocks>1000 && visual.block>0 && visual.block<state.blocks/2,
                "full_visual_queue_does_not_block_audio");
            session.command(AudioEngineRunner::Action::HighFrequency,.002);
            session.command(AudioEngineRunner::Action::LowNoise,.3);
            session.command(AudioEngineRunner::Action::Clutch,.4); // Neutral: no vehicle load.
            session.command(AudioEngineRunner::Action::DynoSpeed,2000);
            session.command(AudioEngineRunner::Action::ExhaustMix, .8);
            session.command(AudioEngineRunner::Action::Roughness, .3); ++stage;
        } else if (stage == 4 && elapsed >= 11) {
            EngineVisualSnapshot visual;
            while(session.readEngineVisualization(visual)) {}
            check(std::abs(visual.highFrequency-.002)<.00001 && std::abs(visual.lowNoise-.3)<.001 &&
                std::abs(visual.clutch-.4)<.001 && std::abs(visual.dynoRpm-2000)<1,"dashboard_controls_applied");
            check(std::isfinite(visual.cylinders[0].piston.x) && std::isfinite(visual.cylinders[0].pressure) &&
                visual.cylinders[0].volume>0,"visual_physics_valid");
            session.command(AudioEngineRunner::Action::HighFrequency,session.defaultHighFrequency());
            session.command(AudioEngineRunner::Action::LowNoise,session.defaultLowNoise());
            session.command(AudioEngineRunner::Action::Clutch,0);
            session.command(AudioEngineRunner::Action::DynoSpeed,1000);
            session.command(AudioEngineRunner::Action::Gear,1);
            session.command(AudioEngineRunner::Action::Dyno,1);
            check(std::abs(state.exhaustMix - .8) < .001 && std::abs(state.roughness - .3) < .001, "sound_settings_applied");
            session.command(AudioEngineRunner::Action::ExhaustMix, 1);
            session.command(AudioEngineRunner::Action::Roughness, session.defaultRoughness()); ++stage;
        } else if (stage == 5 && elapsed >= 14) {
            EngineVisualSnapshot visual;
            while(session.readEngineVisualization(visual)) {}
            check(visual.gear==0 && visual.dyno,"gear_and_dyno_controls_applied");
            session.command(AudioEngineRunner::Action::Gear,-1);
            session.command(AudioEngineRunner::Action::Dyno,0);
            check(state.exhaustMix == 1 && std::abs(state.roughness - session.defaultRoughness()) < .001, "sound_defaults_restored");
            session.command(AudioEngineRunner::Action::Stop); ++stage;
        } else if (stage == 6 && elapsed >= 15) {
            check(!state.ignition && !state.cranking && state.throttle == 0, "stopped");
            session.startEngine(); ++stage;
        } else if (stage == 7 && elapsed >= 19) {
            check(state.ignition && !state.cranking && state.rpm > 200, "restarted"); ++stage;
        }
        highestRev = std::max(highestRev, state.rpm);
        if (elapsed - lastLog >= .9) {
            lastLog = elapsed;
            log << std::fixed << std::setprecision(3) << "t=" << elapsed << " rpm=" << state.rpm
                << " queued_ms=" << state.queuedMs << " missing=" << audio.silenceFrames - warmup.silenceFrames
                << " block_max_ms=" << state.maxBlockMs << std::endl;
        }
        SDL_Delay(uiStallMs ? uiStallMs : 50);
    }
    SDL_SetAudioPostmixCallback(session.device(), nullptr, nullptr);
    const auto state = session.snapshot();
    const auto audio = session.statistics();
    session.close();
    check(stage == 8, "all_control_stages_completed");
    check(audio.silenceFrames == warmup.silenceFrames && audio.writeErrors == 0, "no_missing_audio");
    check(!capture.overflow && capture.blockCount > 0, "capture_complete");
    std::size_t clipped = 0;
    for (std::size_t i = 0; i < capture.sampleCount; ++i)
        if (std::abs(static_cast<int>(capture.samples[i])) >= 32760) ++clipped;
    check(clipped == 0, "no_clipping");
    std::size_t unexpectedSilence = 0;
    double maxCallbackGapMs = 0;
    for (std::size_t i = 0; i < capture.blockCount; ++i) {
        const auto &block = capture.blocks[i];
        const double time = (block.ns - start) / 1e9;
        bool shouldBeAudible = time > 3 && time < 14;
        for (std::size_t marker = 0; marker + 1 < markers.size(); marker += 2) {
            if (block.ns >= markers[marker].ns && block.ns <= markers[marker + 1].ns + 150000000)
                shouldBeAudible = false;
        }
        if (shouldBeAudible && block.peak == 0) ++unexpectedSilence;
        if (i) maxCallbackGapMs = std::max(maxCallbackGapMs, (block.ns - capture.blocks[i - 1].ns) / 1e6);
    }
    check(unexpectedSilence == 0, "no_unexpected_silent_blocks");
    double maxLatency = 0;
    for (const auto &marker : markers) {
        double latency = -1;
        for (std::size_t i = 0; i < capture.blockCount; ++i) {
            const auto &block = capture.blocks[i];
            if (block.ns <= marker.ns || (block.ns - marker.ns) / 1e9 > .5) continue;
            if (marker.mute ? block.peak == 0 : block.peak > .00001f) {
                latency = (block.ns - marker.ns) / 1e6; break;
            }
        }
        log << "command_to_mixer_ms=" << latency << " mute=" << marker.mute << '\n';
        check(latency >= 0 && latency < 100, "mixer_response_under_100ms");
        maxLatency = std::max(maxLatency, latency);
    }
    check(capture.save(prefix, start, spec.freq), "capture_saved");
    // Reuse the same owner after destruction, as the engine selector does.
    check(session.open(preset==0 ? 1 : 0, assets), "switch_engine");
    if (session.ready()) {
        check(session.startEngine(), "switched_engine_start_queued");
        SDL_Delay(4000);
        check(session.snapshot().rpm > 200 && !session.snapshot().cranking, "switched_engine_running");
        check(session.statistics().writeErrors == 0, "switched_output_ok");
    }
    session.close();
    log << "SUMMARY result=" << (passed ? "PASS" : "FAIL") << " duration_s=" << elapsed
        << " missing_frames=" << audio.silenceFrames - warmup.silenceFrames
        << " clipped_samples=" << clipped << " producer_cpu_percent=" << state.cpuSeconds / elapsed * 100
        << " producer_block_max_ms=" << state.maxBlockMs << " max_command_to_mixer_ms=" << maxLatency
        << " unexpected_silent_blocks=" << unexpectedSilence << " callback_gap_max_ms=" << maxCallbackGapMs
        << " peak_rpm=" << highestRev << std::endl;
    std::cout << "Sound test " << (passed ? "PASS" : "FAIL") << ": " << prefix << ".log\n";
    return passed ? 0 : 1;
}

int verifyDriveSession(const std::filesystem::path &assets, std::size_t preset,
    const std::string &prefix) {
    std::ofstream log(prefix + ".log"), telemetry(prefix + "-drive.csv");
    if (!log || !telemetry) return 2;
    SoundSession session;
    if (!session.open(preset, assets)) { std::cerr << session.error() << '\n'; return 2; }
    bool passed = true;
    const auto check = [&](bool condition, const char *name) {
        log << "CHECK " << name << '=' << (condition ? "PASS" : "FAIL") << std::endl;
        passed &= condition;
    };
    Capture capture;
    capture.blocks.resize(60000);
    capture.samples.resize(48 * 192000);
    SDL_AudioSpec format{};
    int frames = 0;
    SDL_GetAudioDeviceFormat(session.device(), &format, &frames);
    const auto start = SDL_GetTicksNS();
    if (!SDL_SetAudioPostmixCallback(session.device(), Capture::mix, &capture)) return 2;
    log << "preset=" << session.preset().id << " driver=" << SDL_GetCurrentAudioDriver()
        << " rate=" << format.freq << '\n';
    telemetry << "seconds,rpm,gear,mph,pedal,applied_throttle,clutch,brake,shifting,missing_frames,engine_seconds,distance_m\n";
    check(session.startEngine(), "start_queued");
    int stage = 0, maxGear = -1, previousGear = -1, upshifts = 0;
    double peakSpeed = 0, beforeShiftRpm = 0, shiftAt = -1, rpmDrop = 0;
    bool torqueCut = false;
    SdlAudioOutput::Statistics warmup{};
    while (true) {
        const double time = (SDL_GetTicksNS() - start) / 1e9;
        if (time >= 41) break;
        const auto state = session.snapshot();
        const auto audio = session.statistics();
        if (time < 1) warmup = audio;
        if (stage == 0 && time >= 3) {
            check(state.rpm > 200 && !state.cranking, "engine_running");
            check(session.command(AudioEngineRunner::Action::Drive, 1), "drive_queued"); ++stage;
        } else if (stage == 1 && time >= 4) {
            check(state.drive && state.gear == 0, "drive_engages_first");
            check(session.command(AudioEngineRunner::Action::Throttle, 1), "accelerator_queued"); ++stage;
        } else if (stage == 2 && time >= 6) {
            const auto before = state.blocks;
            SDL_Delay(1200);
            check(session.snapshot().blocks > before + 100, "driving_continues_without_ui"); ++stage;
        } else if (stage == 3 && time >= 28) {
            check(session.command(AudioEngineRunner::Action::Throttle, 0), "lift_queued");
            check(session.command(AudioEngineRunner::Action::Brake, 1), "brake_queued"); ++stage;
        } else if (stage == 4 && time >= 38) {
            check(state.vehicleSpeed < 1 && state.rpm > 400, "brakes_stop_car_without_stalling");
            check(state.gear == 0, "returns_to_first");
            check(session.command(AudioEngineRunner::Action::Drive, 0), "neutral_queued"); ++stage;
        } else if (stage == 5 && time >= 39) {
            check(!state.drive && state.gear == -1 && state.clutch == 0, "neutral_disengages_drivetrain");
            session.command(AudioEngineRunner::Action::Brake, 0); ++stage;
        }
        maxGear = std::max(maxGear, state.gear);
        peakSpeed = std::max(peakSpeed, state.vehicleSpeed);
        if (state.gear > previousGear && previousGear >= 0 && time < 28) {
            ++upshifts; beforeShiftRpm = state.rpm; shiftAt = time;
            log << "UPSHIFT t=" << time << " gear=" << state.gear + 1 << " rpm=" << state.rpm << '\n';
        }
        if (shiftAt >= 0 && time - shiftAt < .8) rpmDrop = std::max(rpmDrop, beforeShiftRpm - state.rpm);
        previousGear = state.gear;
        torqueCut |= state.throttle == 1 && state.shifting && state.appliedThrottle < .5;
        telemetry << time << ',' << state.rpm << ',' << state.gear + 1 << ',' << state.vehicleSpeed / .44704
            << ',' << state.throttle << ',' << state.appliedThrottle << ',' << state.clutch << ',' << state.brake
            << ',' << state.shifting << ',' << audio.silenceFrames;
        AudioEngineRunner::VehicleTelemetry vehicle;
        if(session.vehicleTelemetry(vehicle))telemetry << ',' << vehicle.time << ',' << vehicle.distance;
        else telemetry << ",nan,nan";
        telemetry << '\n';
        SDL_Delay(50);
    }
    SDL_SetAudioPostmixCallback(session.device(), nullptr, nullptr);
    const auto state = session.snapshot();
    const auto audio = session.statistics();
    session.close();
    std::size_t clipped = 0, silence = 0;
    for (std::size_t i = 0; i < capture.sampleCount; ++i)
        if (std::abs(int(capture.samples[i])) >= 32760) ++clipped;
    for (std::size_t i = 0; i < capture.blockCount; ++i) {
        const double time = (capture.blocks[i].ns - start) / 1e9;
        if (time > 3 && time < 40 && capture.blocks[i].peak == 0) ++silence;
    }
    check(stage == 6, "all_drive_stages_completed");
    check(upshifts >= 2 && maxGear >= 2 && peakSpeed > 15, "accelerates_through_gears");
    check(rpmDrop > 300 && torqueCut, "shifts_change_actual_engine_rpm_and_throttle");
    check(audio.silenceFrames == warmup.silenceFrames && audio.writeErrors == 0, "no_missing_audio");
    check(clipped == 0 && silence == 0, "no_clipping_or_silent_blocks");
    check(!capture.overflow && capture.blockCount > 0 && capture.save(prefix, start, format.freq), "pcm_capture_saved");
    telemetry.flush();
    check(telemetry.good(), "drive_telemetry_saved");
    log << "SUMMARY result=" << (passed ? "PASS" : "FAIL") << " max_gear=" << maxGear + 1
        << " upshifts=" << upshifts << " peak_mph=" << peakSpeed / .44704 << " rpm_drop=" << rpmDrop
        << " missing_frames=" << audio.silenceFrames - warmup.silenceFrames << " clipped_samples=" << clipped
        << " silent_blocks=" << silence << " max_block_ms=" << state.maxBlockMs << std::endl;
    std::cout << "Drive test " << (passed ? "PASS" : "FAIL") << ": " << prefix << ".log\n";
    return passed ? 0 : 1;
}
