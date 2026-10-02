#ifndef ENGINE_SIM_AUDIO_TUI_H
#define ENGINE_SIM_AUDIO_TUI_H

#include <cstdint>
#include <string>
#include <termios.h>

// Main-thread terminal presentation only. It never accesses the simulator.
class AudioTui {
public:
    enum class Key { None, StartStop, ThrottleUp, ThrottleDown, Idle, Blip,
        VolumeUp, VolumeDown, Mute, Quit };
    struct State {
        std::string engine, device, notice;
        double rpm = 0, redline = 6500, throttle = 0, volume = 0.5;
        bool ignition = false, cranking = false, blipping = false, silent = false;
        std::uint64_t missingFrames = 0;
    };
    ~AudioTui() { leave(); }
    bool enter();
    void leave();
    Key decode(unsigned char byte);
    void draw(const State &state);
    std::uint64_t frames() const { return m_frames; }
    std::uint64_t skippedFrames() const { return m_skippedFrames; }

private:
    bool flush();
    bool m_active = false;
    termios m_savedTerminal{};
    int m_inputFlags = -1, m_outputFlags = -1;
    int m_escape = 0;
    std::string m_pending;
    std::size_t m_offset = 0;
    std::uint64_t m_frames = 0, m_skippedFrames = 0;
};

#endif
