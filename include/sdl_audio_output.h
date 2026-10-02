#ifndef ATG_ENGINE_SIM_SDL_AUDIO_OUTPUT_H
#define ATG_ENGINE_SIM_SDL_AUDIO_OUTPUT_H

#include "audio_output.h"
#include "spsc_audio_ring.h"

#include <SDL3/SDL_audio.h>

#include <atomic>
#include <array>
#include <cstdint>
#include <mutex>
#include <string>
#if defined(__APPLE__)
#include <pthread/qos.h>
#endif

class SdlAudioOutput final : public AudioOutput {
public:
    ~SdlAudioOutput() override { stop(); }
    bool start(Simulator *simulator) override;
    bool loadImpulseResponse(Synthesizer &synthesizer, const std::string &path, float volume, int index) override;
    void stop() override;

    struct Statistics {
        std::uint64_t pcmFrames, silenceFrames, shortReads, writeErrors;
        std::uint64_t longestReadNs;
        int queuedFrames;
    };
    Statistics statistics() const;
    SDL_AudioDeviceID device() const { return SDL_GetAudioStreamDevice(m_stream); }

    struct VisualSamples {
        std::array<float, 128> samples{};
        int count = 0;
    };
    // Enable only before start. A full visual queue drops the visual block;
    // neither rendering nor UI backpressure can hold up the audio callback.
    void enableVisualization(bool enabled) { m_visualizationEnabled = enabled; }
    bool readVisualization(VisualSamples &samples) { return m_visualization.pop(samples); }

private:
    static void SDLCALL requestAudio(void *userdata, SDL_AudioStream *stream,
        int additionalBytes, int totalBytes);
    void fillStream(int additionalBytes);
    void stopLocked();

    SDL_AudioStream *m_stream = nullptr;
    Simulator *m_simulator = nullptr;
    std::mutex m_lifecycleMutex;
    bool m_diagnostics = false;
    bool m_visualizationEnabled = false;
    SpscAudioRing<VisualSamples> m_visualization;
    std::atomic<std::uint64_t> m_pcmFrames{0}, m_silenceFrames{0};
    std::atomic<std::uint64_t> m_shortReads{0}, m_writeErrors{0}, m_longestReadNs{0};
    std::atomic<int> m_queuedFrames{0};
#if defined(__APPLE__)
    pthread_override_t m_workerPriority = nullptr;
#endif
};

#endif
