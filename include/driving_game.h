#ifndef ENGINE_SIM_DRIVING_GAME_H
#define ENGINE_SIM_DRIVING_GAME_H
#include <array>
#include <cmath>
#include <cstdint>
#include "driving_handling.h"
#include "driving_city.h"

// Game geometry and handling have no device, GUI, GPU or engine dependencies.
class DrivingCourse {
public:
    static constexpr int Segments=360;
    static constexpr double HalfWidth=7.5, BarrierWidth=12.5;
    struct Location {DrivingPoint point{},forward{0,1},right{1,0};double distance=0,lateral=0,curvature=0;int segment=0;};
    struct CornerAdvice {double speed=80,distance=0,deceleration=0;int direction=0;};
    DrivingCourse();
    Location at(double distance) const;
    Location nearest(DrivingPoint p) const;
    static DrivingHandling::Surface tyreSurface(const Location &location,double yaw);
    CornerAdvice cornerAdvice(double progress,double speed) const;
    double length() const {return lengths.back();}
    const std::array<DrivingPoint,Segments+1> &points() const {return path;}
private:
    std::array<DrivingPoint,Segments+1> path{};
    std::array<double,Segments+1> lengths{};
};

struct DrivingSnapshot {
    DrivingWorld world=DrivingWorld::Circuit;
    double time=0,x=0,z=0,yaw=0,velocityYaw=0,speed=0,wheelDistance=0;
    double steer=0,steeringInput=0,roll=0,pitch=0,lateralG=0,progress=0,lateral=0,offroadFraction=0;
    double lapSeconds=0,lastLap=0,bestLap=0,raceSeconds=0,roadDeceleration=0,impact=0;
    double cornerSpeed=80,cornerDistance=0,cornerDeceleration=0;
    double driftAngle=0,driftScore=0,totalDriftScore=0,bestDriftScore=0,lastDriftScore=0,driftEndedAt=0;
    int cornerDirection=0;
    double cityDistance=0,destinationDistance=0;
    unsigned cityStops=0;
    unsigned laps=0,nextCheckpoint=1,collisions=0,recoveries=0;
    uint64_t steps=0;
    bool offroad=false,wrongWay=false,started=false,finished=false,recovering=false,reversing=false;
};

class DrivingGame {
public:
    explicit DrivingGame(DrivingWorld world=DrivingWorld::Circuit);
    const DrivingCourse &course() const {return track;}
    const DrivingSnapshot &snapshot() const {return state;}
    void restart();
    void recover();
    void setWorld(DrivingWorld world);
    // dt and distance come from the engine's clock/vehicle travel, not frames.
    void advance(double dt,double distance,double speed,double steering,DrivingHandling::Input input=DrivingHandling::Input::Analog,bool drift=false);
    double pilotSteering() const;
    double pilotSpeed() const;
private:
    void step(double dt,double distance,double speed,double steering,DrivingHandling::Input input,double acceleration,bool drift);
    DrivingCourse track;
    DrivingHandling handling;
    DrivingSnapshot state{};
    double lapStart=0,previousProgress=0,impactSeconds=0,recoverySeconds=0;
    double motionYaw=0;
    bool newRacePending=false;
    DrivingWorld pendingWorld=DrivingWorld::Circuit;
};
#endif
