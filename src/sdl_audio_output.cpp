#include "../include/sdl_audio_output.h"
#include "../include/sdl_audio_util.h"
#include "../include/simulator.h"

#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <cstdio>

bool SdlAudioOutput::start(Simulator *simulator) {
    std::lock_guard<std::mutex> lock(m_lifecycleMutex);
    stopLocked();
    if (simulator == nullptr) return false;
    m_visualization.initialize(16);
#if defined(__APPLE__)
    // The device has a real-time deadline; its physics producer must also be
    // scheduled as interactive work, including when no window is focused.
    pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
#endif
    // The synthesizer produces 44.1 kHz PCM. Keep this stream in that native
    // clock domain; SDL handles only the final conversion to the device rate.
    const SDL_AudioSpec spec = { SDL_AUDIO_S16, 1, 44100 };
    SDL_SetHintWithPriority(SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES, "256", SDL_HINT_DEFAULT);
    m_stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, requestAudio, this);
    if (m_stream == nullptr) return false;
    m_simulator = simulator;
#if defined(__APPLE__)
    if (auto *worker = simulator->synthesizer().m_thread) {
        m_workerPriority = pthread_override_qos_class_start_np(
            worker->native_handle(), QOS_CLASS_USER_INTERACTIVE, 0);
    }
#endif
    m_diagnostics = SDL_GetHintBoolean("ENGINE_SIM_AUDIO_DIAGNOSTICS", false);
    m_pcmFrames = 0;
    m_silenceFrames = 0;
    m_shortReads = 0;
    m_writeErrors = 0;
    m_longestReadNs = 0;
    m_queuedFrames = 0;
    if (m_diagnostics) {
        SDL_AudioSpec source = {}, destination = {};
        if (SDL_GetAudioStreamFormat(m_stream, &source, &destination)) {
            std::fprintf(stderr, "audio: stream=%dHz/%dch -> device=%dHz/%dch\n",
                source.freq, source.channels, destination.freq, destination.channels);
        }
    }
    if (!SDL_ResumeAudioStreamDevice(m_stream)) {
        stopLocked();
        return false;
    }
    return true;
}

void SDLCALL SdlAudioOutput::requestAudio(void *userdata, SDL_AudioStream *, int additionalBytes, int) {
    static_cast<SdlAudioOutput *>(userdata)->fillStream(additionalBytes);
}

void SdlAudioOutput::fillStream(int additionalBytes) {
    if (m_stream == nullptr || m_simulator == nullptr) return;
    constexpr int chunkFrames = 512;
    // Supply only the frames the device needs now. The former polling feeder
    // added 23 ms of buffering and inserted silence before the real deadline.
    int remaining = (additionalBytes + 1) / static_cast<int>(sizeof(std::int16_t));
    while (remaining > 0) {
        std::array<std::int16_t, chunkFrames> samples{};
        const int frames = std::min(chunkFrames, remaining);
        const auto readStart = SDL_GetTicksNS();
        const int pcmFrames = m_simulator->readAudioOutput(frames, samples.data());
        if (m_visualizationEnabled) {
            VisualSamples visual;
            for (int i = 0; i < frames; i += 4)
                visual.samples[visual.count++] = samples[i] / 32768.0f;
            m_visualization.push(visual);
        }
        m_longestReadNs.store(std::max(m_longestReadNs.load(), SDL_GetTicksNS() - readStart));
        m_pcmFrames += std::max(0, pcmFrames);
        m_silenceFrames += frames - std::max(0, pcmFrames);
        if (pcmFrames < frames) ++m_shortReads;
        const int bytes = frames * static_cast<int>(sizeof(std::int16_t));
        if (!SDL_PutAudioStreamData(m_stream, samples.data(), bytes)) {
            ++m_writeErrors;
            return;
        }
        remaining -= frames;
    }
    m_queuedFrames = std::max(0, SDL_GetAudioStreamQueued(m_stream)) / static_cast<int>(sizeof(std::int16_t));
}

SdlAudioOutput::Statistics SdlAudioOutput::statistics() const {
    return {m_pcmFrames.load(), m_silenceFrames.load(), m_shortReads.load(),
        m_writeErrors.load(), m_longestReadNs.load(), m_queuedFrames.load()};
}

bool SdlAudioOutput::loadImpulseResponse(Synthesizer &synthesizer, const std::string &path, float volume, int index) {
    return loadSdlImpulseResponse(synthesizer, path, volume, index);
}

void SdlAudioOutput::stop() {
    std::lock_guard<std::mutex> lock(m_lifecycleMutex);
    stopLocked();
}

void SdlAudioOutput::stopLocked() {
    if (m_stream != nullptr) SDL_DestroyAudioStream(m_stream);
#if defined(__APPLE__)
    if (m_workerPriority != nullptr) pthread_override_qos_class_end_np(m_workerPriority);
    m_workerPriority = nullptr;
#endif
    m_stream = nullptr;
    m_simulator = nullptr;
}
