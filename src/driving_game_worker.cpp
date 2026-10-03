#include "driving_game_worker.h"
#include "driving_test_driver.h"
#include "sound_session.h"
#include <algorithm>
#if defined(__APPLE__)
#include <pthread/qos.h>
#endif

void DrivingGameWorker::start() {stop();running=true;thread=std::thread([this]{run();});}
void DrivingGameWorker::stop() {running=false;if(thread.joinable())thread.join();connect(nullptr);}
void DrivingGameWorker::connect(std::shared_ptr<SoundSession> value) {
    std::lock_guard<std::mutex> lock(sessionMutex);session=std::move(value);++generation;
}
void DrivingGameWorker::controls(double value,bool active,bool testPilot,bool keyboardTest,bool drifting) {
    steering=std::isfinite(value) ? std::clamp(value,-1.0,1.0) : 0;enabled=active;pilot=testPilot;keyboardPilot=keyboardTest;drift=drifting;
}
DrivingSnapshot DrivingGameWorker::snapshot() const {std::lock_guard<std::mutex> lock(poseMutex);return current;}
DrivingSnapshot DrivingGameWorker::presented() const {
    DrivingSnapshot a,b;std::chrono::steady_clock::time_point stamp;
    {std::lock_guard<std::mutex> lock(poseMutex);a=previous;b=current;stamp=published;}
    const double f=std::clamp(std::chrono::duration<double>(std::chrono::steady_clock::now()-stamp).count()/std::max(.001,b.time-a.time),0.0,1.0);
    auto interpolate=[&](double DrivingSnapshot::*field) {b.*field=a.*field+(b.*field-a.*field)*f;};
    for(auto field:{&DrivingSnapshot::x,&DrivingSnapshot::z,&DrivingSnapshot::speed,&DrivingSnapshot::wheelDistance,
            &DrivingSnapshot::steer,&DrivingSnapshot::steeringInput,&DrivingSnapshot::roll,&DrivingSnapshot::pitch,&DrivingSnapshot::driftAngle})interpolate(field);
    b.yaw=drivingAngle(a.yaw+drivingAngle(b.yaw-a.yaw)*f);
    b.velocityYaw=drivingAngle(a.velocityYaw+drivingAngle(b.velocityYaw-a.velocityYaw)*f);
    return b;
}
void DrivingGameWorker::run() {
#if defined(__APPLE__)
    pthread_set_qos_class_self_np(QOS_CLASS_USER_INITIATED,0);
#endif
    DrivingGame game;std::shared_ptr<SoundSession> audio;uint64_t seen=~uint64_t(0);
    AudioEngineRunner::VehicleTelemetry last{};
    double keyboardAt=0,keyboardInput=0;
    auto deadline=std::chrono::steady_clock::now();
    while(running) {
        deadline+=std::chrono::nanoseconds(8333333);const auto begin=std::chrono::steady_clock::now();
        {
            std::lock_guard<std::mutex> lock(sessionMutex);
            if(seen!=generation) {
                if(audio)audio->setRoadDeceleration(0);
                audio=session;seen=generation;game=DrivingGame{};last={};requests=0;keyboardAt=keyboardInput=0;
                if(audio)audio->vehicleTelemetry(last);
                std::lock_guard<std::mutex> poses(poseMutex);previous=current=game.snapshot();published=begin;
            }
        }
        const auto action=requests.exchange(0);
        if(action&2)game.restart();else if(action&1)game.recover();
        AudioEngineRunner::VehicleTelemetry next;
        if(audio && audio->vehicleTelemetry(next) && next.time>last.time) {
            if(enabled) {
                const bool automatic=pilot.load(),keys=keyboardPilot.load();
                if(automatic && keys && game.snapshot().time>=keyboardAt) {
                    keyboardInput=drivingKeyboardTestInput(game,next.speed);keyboardAt=game.snapshot().time+.1;
                }
                const double input=automatic ? (keys ? keyboardInput : game.pilotSteering()) : steering.load();
                game.advance(next.time-last.time,next.distance-last.distance,next.speed,input,
                    automatic && !keys ? DrivingHandling::Input::Analog : DrivingHandling::Input::Keyboard,drift);
            }
            audio->setRoadDeceleration(enabled ? game.snapshot().roadDeceleration : 0);
            last=next;speedTarget=pilot ? game.pilotSpeed() : 0;
            std::lock_guard<std::mutex> lock(poseMutex);
            previous=current;current=game.snapshot();
            if(current.time<=previous.time || current.recoveries!=previous.recoveries)previous=current;
            published=std::chrono::steady_clock::now();
        }
        cpuMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();
        const auto now=std::chrono::steady_clock::now();if(deadline<now)deadline=now;
        std::this_thread::sleep_until(deadline);
    }
    if(audio)audio->setRoadDeceleration(0);
}
