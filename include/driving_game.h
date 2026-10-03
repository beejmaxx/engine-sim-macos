#ifndef ENGINE_SIM_DRIVING_GAME_H
#define ENGINE_SIM_DRIVING_GAME_H
#include <array>
#include <cmath>
#include <cstdint>
#include "driving_handling.h"

// Game geometry and handling have no device, GUI, GPU or engine dependencies.
struct DrivingPoint {
    double x=0,z=0;
    DrivingPoint operator+(DrivingPoint p) const {return {x+p.x,z+p.z};}
    DrivingPoint operator-(DrivingPoint p) const {return {x-p.x,z-p.z};}
    DrivingPoint operator*(double s) const {return {x*s,z*s};}
};
inline double drivingDot(DrivingPoint a,DrivingPoint b) {return a.x*b.x+a.z*b.z;}
inline double drivingLength(DrivingPoint p) {return std::hypot(p.x,p.z);}
inline double drivingAngle(double a) {return std::remainder(a,6.283185307179586);}

class DrivingCourse {
public:
    static constexpr int Segments=360;
    static constexpr double HalfWidth=5.5, BarrierWidth=8.5;
    struct Location {DrivingPoint point{},forward{0,1},right{1,0};double distance=0,lateral=0,curvature=0;int segment=0;};
    struct CornerAdvice {double speed=80,distance=0,deceleration=0;int direction=0;};
    DrivingCourse();
    Location at(double distance) const;
    Location nearest(DrivingPoint p) const;
    CornerAdvice cornerAdvice(double progress,double speed) const;
    double length() const {return lengths.back();}
    const std::array<DrivingPoint,Segments+1> &points() const {return path;}
private:
    std::array<DrivingPoint,Segments+1> path{};
    std::array<double,Segments+1> lengths{};
};

struct DrivingSnapshot {
    double time=0,x=0,z=0,yaw=0,velocityYaw=0,speed=0,wheelDistance=0;
    double steer=0,roll=0,pitch=0,lateralG=0,progress=0,lateral=0;
    double lapSeconds=0,lastLap=0,bestLap=0,raceSeconds=0,roadDeceleration=0,impact=0;
    double cornerSpeed=80,cornerDistance=0,cornerDeceleration=0;
    int cornerDirection=0;
    unsigned laps=0,nextCheckpoint=1,collisions=0,recoveries=0;
    uint64_t steps=0;
    bool offroad=false,wrongWay=false,started=false,finished=false,recovering=false;
};

class DrivingGame {
public:
    DrivingGame();
    const DrivingCourse &course() const {return track;}
    const DrivingSnapshot &snapshot() const {return state;}
    void restart();
    void recover();
    // dt and distance come from the engine's clock/vehicle travel, not frames.
    void advance(double dt,double distance,double speed,double steering,DrivingHandling::Input input=DrivingHandling::Input::Analog);
    double pilotSteering() const;
    double pilotSpeed() const;
private:
    void step(double dt,double distance,double speed,double steering,DrivingHandling::Input input,double acceleration);
    DrivingCourse track;
    DrivingHandling handling;
    DrivingSnapshot state{};
    double lapStart=0,previousProgress=0,impactSeconds=0,recoverySeconds=0;
    bool newRacePending=false;
};
#endif
