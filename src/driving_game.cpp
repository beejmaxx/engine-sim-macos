#include "driving_game.h"
#include <algorithm>

namespace {
DrivingPoint unit(DrivingPoint p) {return p*(1/std::max(1e-8,drivingLength(p)));}
double heading(DrivingPoint p) {return std::atan2(p.x,p.z);}
constexpr double Wheelbase=2.8, RearAxleDistance=1.4, Gravity=9.81;
}
DrivingCourse::DrivingCourse() {
    constexpr std::array<DrivingPoint,17> knots{{{0,0},{0,150},{65,260},{220,280},
        {340,220},{330,80},{250,25},{265,-100},{180,-220},{30,-250},
        {-140,-190},{-200,-60},{-180,60},{-105,70},{-65,-45},{-30,-120},{0,-90}}};
    for(int i=0;i<=Segments;++i) {
        const double u=i*double(knots.size())/Segments;const int k=int(u)%knots.size();
        const double t=u-std::floor(u),t2=t*t,t3=t2*t;
        const auto a=knots[(k+knots.size()-1)%knots.size()],b=knots[k];
        const auto c=knots[(k+1)%knots.size()],d=knots[(k+2)%knots.size()];
        path[i]=(b*2+(c-a)*t+(a*2-b*5+c*4-d)*t2+(b*3-a-c*3+d)*t3)*.5;
        if(i)lengths[i]=lengths[i-1]+drivingLength(path[i]-path[i-1]);
    }
    path.back()=path.front();
}
DrivingCourse::Location DrivingCourse::at(double distance) const {
    distance=std::fmod(distance,length());if(distance<0)distance+=length();
    const int i=std::min(Segments-1,int(std::upper_bound(lengths.begin(),lengths.end(),distance)-lengths.begin())-1);
    const auto direction=unit(path[i+1]-path[i]);
    const auto before=unit(path[i]-path[(i+Segments-1)%Segments]);
    const auto after=unit(path[(i+2)%Segments]-path[i+1]);
    const double curve=drivingAngle(heading(after)-heading(before))/std::max(1.0,lengths[i+1]-lengths[i])/2;
    return {path[i]+(path[i+1]-path[i])*((distance-lengths[i])/(lengths[i+1]-lengths[i])),
        direction,{direction.z,-direction.x},distance,0,curve,i};
}
DrivingCourse::Location DrivingCourse::nearest(DrivingPoint p) const {
    double best=1e30,s=0;
    for(int i=0;i<Segments;++i) {
        const auto edge=path[i+1]-path[i];const double t=std::clamp(drivingDot(p-path[i],edge)/drivingDot(edge,edge),0.0,1.0);
        const auto delta=p-(path[i]+edge*t);const double squared=drivingDot(delta,delta);
        if(squared<best) {best=squared;s=lengths[i]+t*(lengths[i+1]-lengths[i]);}
    }
    auto location=at(s);location.lateral=drivingDot(p-location.point,location.right);return location;
}
DrivingGame::DrivingGame() {restart();}
void DrivingGame::restart() {
    if(state.speed>1) {newRacePending=true;recover();return;}
    const double best=state.bestLap;state={};state.bestLap=best;
    const auto spawn=track.at(2);state.x=spawn.point.x;state.z=spawn.point.z;
    state.yaw=state.velocityYaw=heading(spawn.forward);state.progress=previousProgress=2;
    yawRate=impactSlip=lapStart=impactSeconds=recoverySeconds=0;newRacePending=false;
}
void DrivingGame::recover() {state.recovering=true;recoverySeconds=0;}
double DrivingGame::steeringLock(double speed) {
    // Keyboard full lock asks for a usable cornering force, rather than several
    // times the available grip at speed. Low-speed manoeuvring still gets 32 deg.
    return std::min(.55,std::atan(Wheelbase*.95*Gravity/(speed*speed+4)));
}
void DrivingGame::advance(double dt,double distance,double speed,double steering) {
    if(!std::isfinite(dt) || !std::isfinite(distance) || !std::isfinite(speed) || !std::isfinite(steering) || dt<=0)return;
    // A worker scheduling hiccup cannot create an unbounded catch-up workload.
    dt=std::min(dt,.1);distance=std::clamp(distance,0.0,std::max(0.0,speed)*dt+1);
    const int parts=std::max(1,int(std::ceil(dt*120)));
    for(int i=0;i<parts;++i)step(dt/parts,distance/parts,std::max(0.0,speed),std::clamp(steering,-1.0,1.0));
}
void DrivingGame::step(double dt,double distance,double speed,double steering) {
    state.time+=dt;++state.steps;state.wheelDistance+=distance;
    const double acceleration=(speed-state.speed)/std::max(.001,dt);state.speed=speed;
    auto location=track.nearest({state.x,state.z});
    state.offroad=std::abs(location.lateral)>DrivingCourse::HalfWidth-.6;
    const double grip=state.offroad ? .48 : 1.12;
    // One responsive rack filter. Releasing or countersteering centres faster;
    // no extra yaw/heading filters can keep increasing the turn after release.
    const double response=steering==0 ? 32 : (steering*state.steer<0 ? 24 : 18);
    state.steer+=(steering*steeringLock(speed)-state.steer)*(1-std::exp(-dt*response));
    // Centre-of-mass bicycle geometry: travel points slightly INTO the turn.
    // The old delayed velocity heading pointed outside it and felt like ice.
    const double tangent=std::tan(state.steer);
    const double requested=tangent/(Wheelbase*std::sqrt(1+std::pow(RearAxleDistance/Wheelbase*tangent,2)));
    const double curvature=std::clamp(requested,-grip*Gravity/std::max(1.0,speed*speed),grip*Gravity/std::max(1.0,speed*speed));
    yawRate=speed*curvature;
    const double beta=std::asin(std::clamp(RearAxleDistance*curvature,-1.0,1.0));
    impactSlip*=std::exp(-dt*8);
    const double travelYaw=state.yaw+yawRate*dt*.5+beta+impactSlip;
    state.x+=std::sin(travelYaw)*distance;state.z+=std::cos(travelYaw)*distance;
    state.yaw=drivingAngle(state.yaw+yawRate*dt);
    state.velocityYaw=drivingAngle(state.yaw+beta+impactSlip);
    state.lateralG=yawRate*speed/Gravity;
    state.roll+=(std::clamp(state.lateralG*.065,-.09,.09)-state.roll)*(1-std::exp(-dt*7));
    state.pitch+=(std::clamp(-acceleration*.004,-.045,.045)-state.pitch)*(1-std::exp(-dt*5));
    location=track.nearest({state.x,state.z});
    const double edge=DrivingCourse::BarrierWidth-1.18;
    if(std::abs(location.lateral)>edge) {
        const double side=location.lateral>0 ? 1 : -1;
        const auto normal=location.right*side;
        const DrivingPoint velocity{std::sin(state.velocityYaw),std::cos(state.velocityYaw)};
        const double toward=drivingDot(velocity,normal);
        const auto projected=location.point+normal*edge;
        state.x=projected.x;state.z=projected.z;
        if(toward>0) {
            const auto rebound=unit(velocity-normal*(toward*1.22));
            state.velocityYaw=heading(rebound);state.yaw=drivingAngle(state.yaw+drivingAngle(state.velocityYaw-state.yaw)*.55);
            impactSlip=drivingAngle(state.velocityYaw-state.yaw-beta);
            if(impactSeconds<=0 && speed>1) {++state.collisions;state.impact=std::clamp(toward*speed/18,.15,1.0);}
            impactSeconds=.24;yawRate*=.4;
        }
    }
    impactSeconds=std::max(0.0,impactSeconds-dt);state.impact*=std::exp(-dt*3.5);
    state.roadDeceleration=(state.offroad ? 2.4+speed*.045 : 0)+(impactSeconds>0 ? 38*std::max(.15,state.impact) : 0);
    if(state.recovering) {
        recoverySeconds+=dt;state.roadDeceleration=45;
        if(speed<.6) {
            if(newRacePending) {state.speed=0;restart();return;}
            const double checkpoint=(state.nextCheckpoint-1)*track.length()/8+2;
            const auto spawn=track.at(checkpoint);state.x=spawn.point.x;state.z=spawn.point.z;
            state.yaw=state.velocityYaw=heading(spawn.forward);state.steer=state.roll=state.pitch=0;
            state.progress=previousProgress=checkpoint;state.lateral=0;state.offroad=false;state.impact=0;state.roadDeceleration=0;yawRate=impactSlip=0;state.recovering=false;
            ++state.recoveries;state.raceSeconds+=3;lapStart-=3;
            return;
        }
    }
    state.progress=location.distance;state.lateral=location.lateral;
    state.wrongWay=speed>2 && std::cos(drivingAngle(state.velocityYaw-heading(location.forward)))<-.25;
    if(!state.started && speed>1) {state.started=true;lapStart=state.time;}
    if(state.started && !state.finished) {
        state.raceSeconds+=dt;state.lapSeconds=state.time-lapStart;
        const double delta=std::remainder(location.distance-previousProgress,track.length());
        // Checkpoints must be crossed forwards on the road. No backwards laps
        // or shortcuts across the infield; the seam at the finish is explicit.
        if(!state.recovering && std::abs(location.lateral)<DrivingCourse::HalfWidth && delta>0 && delta<10) {
            const double gate=state.nextCheckpoint*track.length()/8;
            if(state.nextCheckpoint<8 && previousProgress<gate && location.distance>=gate)++state.nextCheckpoint;
            if(state.nextCheckpoint==8 && previousProgress>track.length()-20 && location.distance<20) {
                ++state.laps;state.lastLap=state.lapSeconds;
                if(state.bestLap==0 || state.lastLap<state.bestLap)state.bestLap=state.lastLap;
                state.nextCheckpoint=1;lapStart=state.time;state.lapSeconds=0;
                state.finished=state.laps>=3;
            }
        }
    }
    previousProgress=location.distance;
}
double DrivingGame::pilotSteering() const {
    const auto here=track.nearest({state.x,state.z});
    const auto target=track.at(here.distance+std::clamp(8+state.speed*.7,10.0,48.0)).point;
    const double angle=drivingAngle(heading(target-DrivingPoint{state.x,state.z})-state.yaw);
    const double wheel=std::atan(Wheelbase*2*std::sin(angle)/std::max(2.0,drivingLength(target-DrivingPoint{state.x,state.z})));
    const double lock=steeringLock(state.speed);
    return std::clamp(wheel/lock,-1.0,1.0);
}
double DrivingGame::pilotSpeed() const {
    double curve=.001;
    for(double ahead=0;ahead<=75;ahead+=3)curve=std::max(curve,std::abs(track.at(state.progress+ahead).curvature));
    return std::clamp(std::sqrt(6.0/curve),5.0,38.0);
}
