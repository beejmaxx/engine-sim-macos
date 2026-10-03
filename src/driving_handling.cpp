#include "driving_handling.h"
#include <algorithm>
#include <cmath>

namespace {
constexpr double WheelLock=.62;
double approach(double value,double target,double amount) {
    return value+std::clamp(target-value,-amount,amount);
}
}
double DrivingHandling::maximumCurvature(double speed) {
    speed=std::abs(speed);
    // Deliberately generous arcade cornering: ~57 m radius at 100 mph instead
    // of the old tyre model's ~170 m. Lower speeds retain tight parking turns.
    const double yawLimit=1.8/(1+speed/35);
    return std::min(std::tan(WheelLock)/Wheelbase,yawLimit/std::max(.1,speed));
}
double DrivingHandling::cornerSpeed(double curvature) {
    // Leave steering in reserve; advice follows the arcade turn envelope.
    return std::clamp((std::sqrt(35*35+4*35*1.8*.65/std::max(.0001,std::abs(curvature)))-35)*.5,5.0,80.0);
}
void DrivingHandling::advance(double dt,double speed,double steering,Input input,Surface surface,bool drift) {
    if(!std::isfinite(dt) || !std::isfinite(speed) || !std::isfinite(steering) || dt<=0)return;
    if(!std::isfinite(surface.front) || !std::isfinite(surface.rear))return;
    dt=std::min(dt,.1);steering=std::clamp(steering,-1.0,1.0);
    surface.front=std::clamp(surface.front,0.0,1.0);surface.rear=std::clamp(surface.rear,0.0,1.0);
    const int parts=std::max(1,int(std::ceil(dt*240)));
    for(int i=0;i<parts;++i)step(dt/parts,speed,steering,input,surface,drift);
}
void DrivingHandling::step(double dt,double speed,double steering,Input input,Surface surface,bool drift) {
    if(input==Input::Keyboard) {
        const double rate=steering==0 ? 20 : steering*state.input<0 ? 18 : 6;
        state.input=approach(state.input,steering,dt*rate);
    } else state.input=steering;
    // Small taps remain precise despite the much stronger full-lock turn.
    const double demand=input==Input::Keyboard ? state.input*state.input*state.input : state.input;
    const double grass=(surface.front*RearAxle+surface.rear*FrontAxle)/Wheelbase;
    const double control=RoadControl+(DirtControl-RoadControl)*grass;
    const double curve=demand*maximumCurvature(speed)*control;
    const double driftStrength=drift ? std::clamp((speed-5)/8,0.0,1.0) : 0;
    const double targetSlip=-.58*demand*driftStrength;
    state.driftAngle+=(targetSlip-state.driftAngle)*(1-std::exp(-dt*(drift ? 7 : 10)));
    const double requestedYaw=speed*curve*(1+.12*driftStrength);
    const double response=steering==0 || requestedYaw*state.yawRate<0 ? 48 : 32;
    state.yawRate+=(requestedYaw-state.yawRate)*(1-std::exp(-dt*response));
    const double limit=std::abs(speed)*maximumCurvature(speed)*(1+.12*driftStrength);
    state.yawRate=std::clamp(state.yawRate,-limit,limit);
    state.steer+=(std::atan(Wheelbase*curve)-state.steer)*(1-std::exp(-dt*32));
    // Assisted slip changes the body's angle to its trajectory, not the
    // trajectory itself. Release recovers grip without a sideways snap.
    state.lateralSpeed=std::abs(speed)*std::sin(state.driftAngle);
    state.lateralG=state.yawRate*speed/Gravity;
    if(std::abs(speed)<.05)state.yawRate=state.lateralG=state.driftAngle=state.lateralSpeed=0;
}
void DrivingHandling::impact(double,double) {
    state.lateralSpeed=0;state.yawRate=0;state.driftAngle=0;
}
