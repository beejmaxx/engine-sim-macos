#ifndef ENGINE_SIM_SOUND_SESSION_H
#define ENGINE_SIM_SOUND_SESSION_H

#include "audio_engine_runner.h"
#include "sdl_audio_output.h"
#include <array>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

// Native sound host shared by the Mac controls and the headless validation.
// The control thread owns this object; the runner exclusively owns live physics.
class SoundSession {
public:
    struct Preset {
        std::string id, title, detail, script;
        double startThrottle, revThrottle;
        bool needsEntrypoint = false;
        double simulationHz = 2500;
    };
    static const std::vector<Preset> &presets();
    static constexpr double DefaultVolume = 0.35;

    SoundSession();
    ~SoundSession();
    SoundSession(const SoundSession &) = delete;
    SoundSession &operator=(const SoundSession &) = delete;
    bool open(std::size_t preset, const std::filesystem::path &assets, double volume = DefaultVolume);
    void close();
    bool command(AudioEngineRunner::Action action, double value = 0);
    bool startEngine();
    bool rev();
    AudioEngineRunner::Snapshot snapshot() const;
    SdlAudioOutput::Statistics statistics() const;
    SDL_AudioDeviceID device() const;
    bool readVisualization(SdlAudioOutput::VisualSamples &samples);
    bool readEngineVisualization(EngineVisualSnapshot &snapshot);
    const EngineVisualLayout &visualLayout() const { return m_visualLayout; }
    double defaultHighFrequency() const { return m_defaultHighFrequency; }
    double defaultLowNoise() const { return m_defaultLowNoise; }
    const std::string &error() const { return m_error; }
    const std::string &engineName() const { return m_engineName; }
    const std::string &deviceName() const { return m_deviceName; }
    const Preset &preset() const { return presets()[m_preset]; }
    double redline() const { return m_redline; }
    double defaultRoughness() const { return m_defaultRoughness; }
    bool ready() const { return m_ready; }

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
    std::string m_error, m_engineName, m_deviceName;
    std::size_t m_preset = 0;
    double m_redline = 7000, m_defaultRoughness = 0;
    double m_defaultHighFrequency = 0, m_defaultLowNoise = 0;
    EngineVisualLayout m_visualLayout;
    bool m_ready = false;
};

// Uses the same session/command boundary as the native window; no AppKit needed.
int verifySoundSession(const std::filesystem::path &assets, std::size_t preset,
    const std::string &prefix, int uiStallMs);
int verifyDriveSession(const std::filesystem::path &assets, std::size_t preset,
    const std::string &prefix);

#endif
