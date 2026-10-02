#include "audio_tui.h"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <fcntl.h>
#include <iomanip>
#include <poll.h>
#include <sstream>
#include <sys/ioctl.h>
#include <unistd.h>
#include <vector>

namespace {
std::string clean(std::string value) {
    // Engine scripts and device names are data, never terminal control codes.
    for (char &c : value) if (static_cast<unsigned char>(c) < 32 || c == 127) c = ' ';
    return value;
}
std::string bar(double fraction, int width) {
    const int filled = static_cast<int>(std::clamp(fraction, 0.0, 1.0) * width);
    return "[" + std::string(filled, '=') + std::string(width - filled, ' ') + "]";
}
std::string number(double value, int precision) {
    std::ostringstream text;
    text << std::fixed << std::setprecision(precision) << value;
    return text.str();
}
}

bool AudioTui::enter() {
    if (m_active || !isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO)
        || tcgetattr(STDIN_FILENO, &m_savedTerminal) != 0) return false;
    m_inputFlags = fcntl(STDIN_FILENO, F_GETFL);
    m_outputFlags = fcntl(STDOUT_FILENO, F_GETFL);
    if (m_inputFlags < 0 || m_outputFlags < 0) return false;
    termios terminal = m_savedTerminal;
    terminal.c_lflag &= ~(ICANON | ECHO);
    terminal.c_lflag |= ISIG; // Ctrl-C still reaches the normal signal handler.
    terminal.c_iflag &= ~IXON;
    terminal.c_cc[VMIN] = 1;
    terminal.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSANOW, &terminal) != 0) return false;
    m_active = true;
    if (fcntl(STDIN_FILENO, F_SETFL, m_inputFlags | O_NONBLOCK) < 0
        || fcntl(STDOUT_FILENO, F_SETFL, m_outputFlags | O_NONBLOCK) < 0) {
        leave(); return false;
    }
    m_pending = "\033[?1049h\033[?25l\033[2J\033[H";
    m_offset = 0;
    flush();
    return true;
}

void AudioTui::leave() {
    if (!m_active) return;
    tcsetattr(STDIN_FILENO, TCSANOW, &m_savedTerminal);
    // Cleanup is bounded even if nobody is draining the terminal. The audio
    // worker does not participate in terminal entry, drawing, or restoration.
    const std::string restore = "\033[0m\033[?25h\033[?1049l";
    pollfd output{STDOUT_FILENO, POLLOUT, 0};
    if (poll(&output, 1, 50) > 0) {
        const auto ignored = write(STDOUT_FILENO, restore.data(), restore.size());
        (void)ignored;
    }
    fcntl(STDIN_FILENO, F_SETFL, m_inputFlags);
    fcntl(STDOUT_FILENO, F_SETFL, m_outputFlags);
    m_active = false;
}

bool AudioTui::flush() {
    while (m_offset < m_pending.size()) {
        const auto count = write(STDOUT_FILENO, m_pending.data() + m_offset, m_pending.size() - m_offset);
        if (count > 0) m_offset += static_cast<std::size_t>(count);
        else return false; // Includes EAGAIN/EINTR: try again on a later UI tick.
    }
    m_pending.clear(); m_offset = 0;
    return true;
}

AudioTui::Key AudioTui::decode(unsigned char byte) {
    if (m_escape == 1) {
        m_escape = (byte == '[' || byte == 'O') ? 2 : 0;
        if (m_escape) return Key::None;
        // A lone Escape must not swallow the next ordinary key (including Q).
    } else if (m_escape == 2) {
        // Consume the whole CSI sequence, including modifiers, across reads.
        if (byte >= 0x40 && byte <= 0x7e) {
            m_escape = 0;
            if (byte == 'C' || byte == 'A') return Key::ThrottleUp;
            if (byte == 'D' || byte == 'B') return Key::ThrottleDown;
        } else if (byte < 0x20 || byte > 0x3f) m_escape = 0;
        return Key::None;
    }
    switch (byte) {
    case 27: m_escape = 1; return Key::None;
    case ' ': return Key::StartStop;
    case 'w': case 'W': return Key::ThrottleUp;
    case 's': case 'S': return Key::ThrottleDown;
    case '0': return Key::Idle;
    case 'b': case 'B': return Key::Blip;
    case '+': case '=': return Key::VolumeUp;
    case '-': case '_': return Key::VolumeDown;
    case 'm': case 'M': return Key::Mute;
    case 'q': case 'Q': case 4: return Key::Quit;
    default: return Key::None;
    }
}

void AudioTui::draw(const State &s) {
    if (!m_active) return;
    if (!flush()) { ++m_skippedFrames; return; }
    winsize size{};
    ioctl(STDOUT_FILENO, TIOCGWINSZ, &size);
    const int columns = size.ws_col ? size.ws_col : 80;
    const int rows = size.ws_row ? size.ws_row : 24;
    const int width = std::max(0, columns - 1); // Never wrap into the next row.
    std::vector<std::string> lines;
    if (columns < 54 || rows < 18) {
        lines = {"ENGINE SOUND", clean(s.engine),
            "RPM " + number(s.rpm, 0) + "  Throttle " + number(s.throttle * 100, 2) + "%",
            "Volume " + number(s.volume * 100, 0) + "%",
            "Space start/stop | W/S throttle | +/- volume | Q quit",
            "Enlarge terminal for full controls"};
    } else {
        const int meterWidth = std::min(34, columns - 24);
        const std::string engineState = !s.ignition ? "IGNITION OFF"
            : s.cranking ? "STARTING" : s.rpm < 200 ? "STALLED - press Space to stop, then restart" : "RUNNING";
        lines = {"ENGINE SOUND", clean(s.engine), "Output  " + clean(s.device), "",
            "RPM       " + number(s.rpm, 0) + "   " + engineState,
            "          " + bar(s.rpm / std::max(1.0, s.redline), meterWidth), "",
            "Throttle  " + number(s.throttle * 100, 2) + "%   " + bar(s.throttle, meterWidth),
            "Volume    " + number(s.volume * 100, 0) + "%      " + bar(s.volume, meterWidth), "",
            s.silent ? "Audio     SILENT TEST - dummy output"
                : s.missingFrames ? "Audio     Missing samples detected: " + std::to_string(s.missingFrames)
                : "Audio     Continuous - no missing samples detected",
            s.blipping ? "          Gentle rev in progress" : "", "",
            "Space  start / stop       B    gentle rev",
            "W/S or arrows  throttle +/-0.25%    0  idle",
            "+/-    volume            M    mute / unmute",
            "Q      quit              Ctrl-C also quits", "", clean(s.notice)};
    }
    std::ostringstream frame;
    frame << "\033[H";
    const auto count = std::min(lines.size(), static_cast<std::size_t>(rows));
    for (std::size_t i = 0; i < count; ++i) {
        frame << "\033[" << i + 1 << ";1H\033[2K";
        if (i == 0) frame << "\033[1;36m";
        frame << lines[i].substr(0, static_cast<std::size_t>(width)) << "\033[0m";
    }
    frame << "\033[J";
    m_pending = frame.str(); m_offset = 0;
    ++m_frames;
    flush();
}
