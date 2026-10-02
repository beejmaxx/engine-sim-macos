#ifndef ENGINE_SIM_AUDIO_ENGINE_RUNNER_H
#define ENGINE_SIM_AUDIO_ENGINE_RUNNER_H

#include "spsc_audio_ring.h"
#include "engine_visual_state.h"
#include <atomic>
#include <cstdint>
#include <thread>

class Simulator;

// Native host: one producer owns both physics and synthesis. Device consumption
// determines how much to produce; UI/terminal work never advances engine time.
class AudioEngineRunner {
public:
    enum class Action { Start, Stop, Throttle, Volume, Blip, ExhaustMix, Roughness,
        HighFrequency, LowNoise, Dyno, DynoSpeed, Clutch, Gear, Drive, Brake };
    struct Command { Action action; double value = 0; };
    struct VehicleTelemetry { double time=0,distance=0,speed=0; };
    struct Snapshot {
        double rpm, simulatedSeconds, queuedMs, maxBlockMs, cpuSeconds, maxCpuBlockMs, maxWakeupOverrunMs;
        std::uint64_t blocks;
        int schedulingStatus;
        double throttle, volume, exhaustMix, roughness;
        bool ignition, cranking, blipping;
        double vehicleSpeed, clutch, brake, appliedThrottle;
        int gear;
        bool drive, shifting;
    };

    ~AudioEngineRunner() { stop(); }
    bool start(Simulator &simulator, double reserveSeconds = 0.03, bool realtime = true);
    void stop();
    bool command(Command command) { return m_run && m_commands.push(command); }
    Snapshot snapshot() const;
    bool vehicleTelemetry(VehicleTelemetry &value) const;
    void setRoadDeceleration(double value);
    void enableVisualTelemetry(bool enabled) { m_visualTelemetry=enabled; }
    bool readVisualTelemetry(EngineVisualSnapshot &snapshot) { return m_visuals.pop(snapshot); }

private:
    void run();
    void publishVisuals(double simulatedSeconds);
    bool m_visualTelemetry=false;
    SpscAudioRing<EngineVisualSnapshot> m_visuals;
    Simulator *m_simulator = nullptr;
    double m_reserveSeconds = 0.06;
    bool m_realtime = false;
    SpscAudioRing<Command> m_commands;
    std::thread m_thread;
    std::atomic<bool> m_run{false}, m_ready{false};
    std::atomic<double> m_rpm{0}, m_simulatedSeconds{0}, m_queuedMs{0}, m_maxBlockMs{0};
    std::atomic<double> m_cpuSeconds{0}, m_maxCpuBlockMs{0}, m_maxWakeupOverrunMs{0};
    std::atomic<std::uint64_t> m_blocks{0};
    std::atomic<int> m_schedulingStatus{-1};
    std::atomic<double> m_throttle{0}, m_volume{1};
    std::atomic<double> m_exhaustMix{1}, m_roughness{0};
    std::atomic<bool> m_ignition{false}, m_cranking{false}, m_blipping{false};
    std::atomic<double> m_vehicleSpeed{0}, m_clutch{0}, m_brake{0}, m_appliedThrottle{0};
    std::atomic<int> m_gear{-1};
    std::atomic<bool> m_drive{false}, m_shifting{false};
    // Latest-value game mailboxes. The producer never waits for gameplay or
    // graphics. Atomic payload fields also make seqlock retries race-free.
    std::atomic<unsigned> m_vehicleSequence{0};
    std::atomic<double> m_vehicleTime{0},m_vehicleDistance{0},m_gameSpeed{0};
    std::atomic<double> m_roadDeceleration{0},m_roadLoadUntil{0};
};

#endif
