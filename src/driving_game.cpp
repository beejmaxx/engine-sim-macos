#include "driving_game.h"
#include <algorithm>

namespace {
DrivingPoint unit(DrivingPoint p) {return p*(1/std::max(1e-8,drivingLength(p)));}
double heading(DrivingPoint p) {return std::atan2(p.x,p.z);}
}
DrivingCourse::DrivingCourse() {
    // Sweeping, metre-scaled bends for high-speed driving. The old first bend
    // needed a 65 mph approach even when steering was held fully at 100 mph.
    constexpr double Scale=2;
    constexpr std::array<DrivingPoint,17> knots{{{0,0},{0,150},{65,260},{220,280},
        {340,220},{330,80},{250,25},{265,-100},{180,-220},{30,-250},
        {-140,-190},{-200,-60},{-180,60},{-105,70},{-65,-45},{-30,-120},{0,-90}}};
    for(int i=0;i<=Segments;++i) {
        const double u=i*double(knots.size())/Segments;const int k=int(u)%knots.size();
        const double t=u-std::floor(u),t2=t*t,t3=t2*t;
        const auto a=knots[(k+knots.size()-1)%knots.size()],b=knots[k];
        const auto c=knots[(k+1)%knots.size()],d=knots[(k+2)%knots.size()];
        path[i]=(b*2+(c-a)*t+(a*2-b*5+c*4-d)*t2+(b*3-a-c*3+d)*t3)*(.5*Scale);
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
DrivingHandling::Surface DrivingCourse::tyreSurface(const Location &location,double yaw) {
    constexpr double HalfTrack=.78,HalfContact=.14;
    const double angle=drivingAngle(yaw-heading(location.forward));
    auto axle=[&](double offset) {
        double grass=0;
        for(double side:{-1.0,1.0}) {
            const double across=offset*std::sin(angle)+side*HalfTrack*std::cos(angle);
            const double along=offset*std::cos(angle)-side*HalfTrack*std::sin(angle);
            const double lateral=location.lateral+across-location.curvature*along*along*.5;
            const double t=std::clamp((std::abs(lateral)-HalfWidth+HalfContact)/(2*HalfContact),0.0,1.0);
            grass+=t*t*(3-2*t)*.5;
        }
        return grass;
    };
    return {axle(DrivingHandling::FrontAxle),axle(-DrivingHandling::RearAxle)};
}
DrivingCourse::CornerAdvice DrivingCourse::cornerAdvice(double progress,double speed) const {
    CornerAdvice result;double allowable=80;
    for(double ahead=0;ahead<=250;ahead+=3) {
        const double curve=at(progress+ahead).curvature;
        if(std::abs(curve)<.002)continue;
        const double target=DrivingHandling::cornerSpeed(curve);
        // Leave reaction distance and use comfortable braking, below the
        // maximum tyre/brake force. This is advice, never an automatic pedal.
        const double distance=std::max(0.0,ahead-speed*.55-8);
        const double permitted=std::sqrt(target*target+2*14*distance);
        if(permitted<allowable) {
            allowable=permitted;result={target,ahead,std::max(0.0,(speed*speed-target*target)/(2*std::max(1.0,distance))),curve>0 ? 1 : -1};
        }
    }
    return result;
}
DrivingGame::DrivingGame(DrivingWorld world) {state.world=pendingWorld=world;restart();}
void DrivingGame::restart() {
    if(std::abs(state.speed)>1) {newRacePending=true;recover();return;}
    const double best=state.bestLap;state={};state.bestLap=best;state.world=pendingWorld;
    const auto road=track.at(2);
    const auto spawn=state.world==DrivingWorld::City ? DrivingCity::start() : DrivingCity::Spawn{road.point,heading(road.forward)};
    state.x=spawn.point.x;state.z=spawn.point.z;
    state.yaw=state.velocityYaw=motionYaw=spawn.yaw;state.progress=previousProgress=2;
    state.destinationDistance=drivingLength(DrivingCity::Destinations[0]-spawn.point);
    handling.reset();lapStart=impactSeconds=recoverySeconds=0;newRacePending=false;
}
void DrivingGame::recover() {state.recovering=true;recoverySeconds=0;}
void DrivingGame::setWorld(DrivingWorld world) {
    if(world==pendingWorld)return;
    pendingWorld=world;newRacePending=true;recover();
    if(std::abs(state.speed)<.6)restart();
}
void DrivingGame::advance(double dt,double distance,double speed,double steering,DrivingHandling::Input input,bool drift) {
    if(!std::isfinite(dt) || !std::isfinite(distance) || !std::isfinite(speed) || !std::isfinite(steering) || dt<=0)return;
    // A worker scheduling hiccup cannot create an unbounded catch-up workload.
    dt=std::min(dt,.1);const double travelLimit=std::abs(speed)*dt+1;
    distance=std::clamp(distance,-travelLimit,travelLimit);
    const int parts=std::max(1,int(std::ceil(dt*120)));
    const double acceleration=(speed-state.speed)/dt;
    for(int i=0;i<parts;++i)step(dt/parts,distance/parts,speed,std::clamp(steering,-1.0,1.0),input,acceleration,drift);
    const auto advice=state.reversing || state.world==DrivingWorld::City ? DrivingCourse::CornerAdvice{} : track.cornerAdvice(state.progress,state.speed);
    state.cornerSpeed=advice.speed;state.cornerDistance=advice.distance;
    state.cornerDeceleration=advice.deceleration;state.cornerDirection=advice.direction;
}
void DrivingGame::step(double dt,double distance,double speed,double steering,DrivingHandling::Input input,double acceleration,bool drift) {
    state.time+=dt;++state.steps;state.wheelDistance+=distance;
    state.speed=speed;state.reversing=std::signbit(speed);
    const double magnitude=std::abs(speed),reverseAngle=state.reversing ? 3.141592653589793 : 0;
    const bool city=state.world==DrivingWorld::City;
    auto location=city ? DrivingCourse::Location{} : track.nearest({state.x,state.z});
    const auto surface=city ? drivingCity().tyreSurface({state.x,state.z},state.yaw) : DrivingCourse::tyreSurface(location,state.yaw);
    state.offroadFraction=(surface.front*DrivingHandling::RearAxle+surface.rear*DrivingHandling::FrontAxle)/DrivingHandling::Wheelbase;
    state.offroad=state.offroadFraction>.01;
    const double previousYaw=handling.snapshot().yawRate,previousTravel=motionYaw;
    handling.advance(dt,speed,steering,input,surface,drift && !state.recovering);
    const auto &chassis=handling.snapshot();state.steer=chassis.steer;state.steeringInput=chassis.input;
    motionYaw=drivingAngle(motionYaw+(previousYaw+chassis.yawRate)*dt*.5);
    state.driftAngle=chassis.driftAngle;state.yaw=drivingAngle(motionYaw-state.driftAngle);
    state.velocityYaw=drivingAngle(motionYaw+reverseAngle);
    const double travelYaw=previousTravel+drivingAngle(motionYaw-previousTravel)*.5;
    state.x+=std::sin(travelYaw)*distance;state.z+=std::cos(travelYaw)*distance;
    state.lateralG=chassis.lateralG;
    state.roll+=(std::clamp(state.lateralG*.065,-.09,.09)-state.roll)*(1-std::exp(-dt*7));
    state.pitch+=(std::clamp(-acceleration*.004,-.045,.045)-state.pitch)*(1-std::exp(-dt*5));
    if(city) {
        DrivingPoint position{state.x,state.z};
        const auto contact=drivingCity().constrain(position,state.yaw);
        state.x=position.x;state.z=position.z;
        if(contact.hit) {
            const DrivingPoint velocity{std::sin(state.velocityYaw),std::cos(state.velocityYaw)};
            const double toward=-drivingDot(velocity,contact.normal);
            if(toward>0) {
                auto tangent=velocity+contact.normal*toward;
                if(drivingLength(tangent)<.15)tangent={-contact.normal.z,contact.normal.x};
                state.velocityYaw=heading(unit(tangent+contact.normal*.08));
                state.yaw=motionYaw=drivingAngle(state.velocityYaw-reverseAngle);state.driftAngle=0;
                handling.impact(speed,0);
                if(impactSeconds<=0 && magnitude>1) {++state.collisions;state.impact=std::clamp(toward*magnitude/18,.15,1.0);}
                impactSeconds=.24;
            }
        }
    } else {
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
            // Forgiving wall scrape: redirect along the barrier instead of
            // bouncing sideways and leaving the car pointed into the wall.
            const double direction=drivingDot(velocity,location.forward)>=0 ? 1 : -1;
            const auto rebound=unit(location.forward*direction-normal*.04);
            state.velocityYaw=heading(rebound);state.yaw=motionYaw=drivingAngle(state.velocityYaw-reverseAngle);state.driftAngle=0;
            handling.impact(speed,drivingAngle(state.velocityYaw-state.yaw));
            if(impactSeconds<=0 && magnitude>1) {++state.collisions;state.impact=std::clamp(toward*magnitude/18,.15,1.0);}
            impactSeconds=.24;
        }
    }
    }
    impactSeconds=std::max(0.0,impactSeconds-dt);state.impact*=std::exp(-dt*3.5);
    state.roadDeceleration=state.offroadFraction*(city ? .6+magnitude*.015 : 2.4+magnitude*.045)+std::abs(state.driftAngle)*2.5
        +(impactSeconds>0 ? 38*std::max(.15,state.impact) : 0);
    if(speed>7 && std::abs(state.driftAngle)>.12 && !state.offroad && impactSeconds==0 && !state.recovering)
        state.driftScore+=dt*speed*std::abs(state.driftAngle)*12;
    else if(state.driftScore>0 && (std::abs(state.driftAngle)<.06 || speed<=7 || state.offroad || impactSeconds>0 || state.recovering)) {
        state.lastDriftScore=state.driftScore;state.totalDriftScore+=state.driftScore;
        state.bestDriftScore=std::max(state.bestDriftScore,state.driftScore);state.driftScore=0;state.driftEndedAt=state.time;
    }
    if(state.recovering) {
        recoverySeconds+=dt;state.roadDeceleration=45;
        if(magnitude<.6) {
            if(newRacePending) {state.speed=0;restart();return;}
            const double checkpoint=(state.nextCheckpoint-1)*track.length()/8+2;
            const auto road=track.at(checkpoint);
            const auto spawn=city ? drivingCity().nearestStreet({state.x,state.z},state.yaw) : DrivingCity::Spawn{road.point,heading(road.forward)};
            state.x=spawn.point.x;state.z=spawn.point.z;
            state.yaw=motionYaw=spawn.yaw;state.velocityYaw=drivingAngle(motionYaw+reverseAngle);
            state.steer=state.steeringInput=state.roll=state.pitch=state.driftAngle=0;
            state.progress=previousProgress=checkpoint;state.lateral=state.offroadFraction=0;state.offroad=false;state.impact=0;state.roadDeceleration=0;handling.reset();state.recovering=false;
            ++state.recoveries;if(!city){state.raceSeconds+=3;lapStart-=3;}
            return;
        }
    }
    if(city) {
        state.cityDistance+=std::abs(distance);state.wrongWay=false;state.lateral=0;
        const auto target=DrivingCity::Destinations[state.cityStops%DrivingCity::Destinations.size()];
        state.destinationDistance=drivingLength(DrivingPoint{state.x,state.z}-target);
        if(!state.recovering && state.destinationDistance<12)++state.cityStops;
        return;
    }
    state.progress=location.distance;state.lateral=location.lateral;
    state.wrongWay=magnitude>2 && std::cos(drivingAngle(state.velocityYaw-heading(location.forward)))<-.25;
    if(!state.started && magnitude>1) {state.started=true;lapStart=state.time;}
    if(state.started && !state.finished) {
        state.raceSeconds+=dt;state.lapSeconds=state.time-lapStart;
        const double delta=std::remainder(location.distance-previousProgress,track.length());
        // Checkpoints must be crossed forwards on the road. No backwards laps
        // or shortcuts across the infield; the seam at the finish is explicit.
        if(!state.recovering && !state.reversing && std::abs(location.lateral)<DrivingCourse::HalfWidth && delta>0 && delta<10) {
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
    const double curvature=2*std::sin(angle)/std::max(2.0,drivingLength(target-DrivingPoint{state.x,state.z}));
    return std::clamp(curvature/DrivingHandling::maximumCurvature(state.speed),-1.0,1.0);
}
double DrivingGame::pilotSpeed() const {
    double target=50;
    // The validation driver must actually brake from its faster straights.
    // A fixed 75 m peek missed hairpins when approaching above 100 mph.
    for(double ahead=0;ahead<=250;ahead+=3) {
        const double curve=std::abs(track.at(state.progress+ahead).curvature);
        const double corner=DrivingHandling::cornerSpeed(curve);
        const double brakingDistance=std::max(0.0,ahead-state.speed*.4-4);
        target=std::min(target,std::sqrt(corner*corner+2*7.5*brakingDistance));
    }
    return target;
}
