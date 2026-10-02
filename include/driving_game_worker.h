#ifndef ENGINE_SIM_DRIVING_GAME_WORKER_H
#define ENGINE_SIM_DRIVING_GAME_WORKER_H
#include "driving_game.h"
#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <thread>

class SoundSession;
// Host-only 120 Hz worker. Neither engine/DSP nor the device callback takes
// these locks. A render stall cannot pause steering, collision or race time.
class DrivingGameWorker {
public:
    ~DrivingGameWorker() {stop();}
    void start();
    void stop();
    void connect(std::shared_ptr<SoundSession> session);
    void controls(double steering,bool enabled,bool pilot);
    void recover() {requests.fetch_or(1);}
    void restart() {requests.fetch_or(2);}
    DrivingSnapshot snapshot() const;
    DrivingSnapshot presented() const;
    double targetSpeed() const {return speedTarget.load();}
    double cpuMilliseconds() const {return cpuMs.load();}
private:
    void run();
    mutable std::mutex sessionMutex,poseMutex;
    std::shared_ptr<SoundSession> session;
    uint64_t generation=0;
    DrivingSnapshot previous{},current{};
    std::chrono::steady_clock::time_point published{};
    std::thread thread;
    std::atomic<bool> running{false},enabled{false},pilot{false};
    std::atomic<double> steering{0},speedTarget{0},cpuMs{0};
    std::atomic<unsigned> requests{0};
};
#endif
