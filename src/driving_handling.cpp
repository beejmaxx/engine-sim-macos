#include "driving_handling.h"
#include <algorithm>
#include <cmath>

namespace {
constexpr double Mass=1450, YawInertia=1900, WheelLock=.55;
// Cornering stiffness per unit axle load. A little front understeer makes
// countersteering and lift-off stable without inventing a lagging travel angle.
constexpr double FrontStiffness=22, RearStiffness=28, AssistedG=.92;
double approach(double value,double target,double amount) {
    return value+std::clamp(target-value,-amount,amount);
}
}
double DrivingHandling::maximumCurvature(double speed) {
    return std::min(std::tan(WheelLock)/Wheelbase,AssistedG*Gravity/std::max(4.0,speed*speed));
}
void DrivingHandling::advance(double dt,double speed,double steering,Input input,bool offroad) {
    if(!std::isfinite(dt) || !std::isfinite(speed) || !std::isfinite(steering) || dt<=0)return;
    dt=std::min(dt,.1);speed=std::max(0.0,speed);steering=std::clamp(steering,-1.0,1.0);
    const int parts=std::max(1,int(std::ceil(dt*240)));
    for(int i=0;i<parts;++i)step(dt/parts,speed,steering,input,offroad);
}
void DrivingHandling::step(double dt,double speed,double steering,Input input,bool offroad) {
    if(input==Input::Keyboard) {
        // A tap makes a correction; a sustained press builds a corner. Release
        // and reversal have priority over the slower build toward full lock.
        const double rate=steering==0 || steering*state.input<0 ? 10 : 3;
        state.input=approach(state.input,steering,dt*rate);
    } else state.input=steering;
    const double grip=offroad ? DirtGrip : RoadGrip;
    const double curve=state.input*maximumCurvature(speed)*(offroad ? .45 : 1);
    const double requestedG=curve*speed*speed/Gravity;
    const double slip=grip*std::atanh(std::clamp(requestedG/grip,-.9,.9));
    const double target=std::clamp(std::atan(Wheelbase*curve)+slip*(1/FrontStiffness-1/RearStiffness),-WheelLock,WheelLock);
    state.steer+=(target-state.steer)*(1-std::exp(-dt*26));

    const double beta=std::atan(RearAxle/Wheelbase*std::tan(state.steer));
    const double slowYaw=speed/Wheelbase*std::cos(beta)*std::tan(state.steer);
    if(speed<=3) {
        state.yawRate=slowYaw;
        state.lateralSpeed=speed*std::tan(beta);
        state.lateralG=slowYaw*speed/Gravity;
        if(speed<.05)state.yawRate=state.lateralSpeed=state.lateralG=0;
        return;
    }

    // Bicycle tyre forces with independent front/rear slip. Changing the wheel
    // angle cannot instantly set yaw rate or rotate the velocity sideways.
    const double forwardSpeed=std::max(3.0,speed);
    const double frontLoad=Mass*Gravity*RearAxle/Wheelbase,rearLoad=Mass*Gravity*FrontAxle/Wheelbase;
    const double frontSlip=state.steer-std::atan2(state.lateralSpeed+FrontAxle*state.yawRate,forwardSpeed);
    const double rearSlip=-std::atan2(state.lateralSpeed-RearAxle*state.yawRate,forwardSpeed);
    const double frontForce=grip*frontLoad*std::tanh(FrontStiffness*frontSlip/grip)*std::cos(state.steer);
    const double rearForce=grip*rearLoad*std::tanh(RearStiffness*rearSlip/grip);
    const double acceleration=(frontForce+rearForce)/Mass;
    state.lateralSpeed+=(acceleration-speed*state.yawRate)*dt;
    state.yawRate+=(FrontAxle*frontForce-RearAxle*rearForce)/YawInertia*dt;

    // Parking speeds use the same geometry without singular tyre-slip maths.
    const double blend=std::clamp((speed-3)/4,0.0,1.0);
    const double parkingResponse=1-std::exp(-dt*20*(1-blend));
    state.yawRate+=(slowYaw-state.yawRate)*parkingResponse;
    state.lateralSpeed+=(speed*std::tan(beta)-state.lateralSpeed)*parkingResponse;
    state.lateralG=slowYaw*speed/Gravity*(1-blend)+acceleration/Gravity*blend;
}
void DrivingHandling::impact(double speed,double sideslip) {
    state.lateralSpeed=speed*std::tan(std::clamp(sideslip,-.5,.5));
    state.yawRate*=.35;
}
