#include "driving_city.h"
#include <algorithm>
#include <limits>

DrivingCity::DrivingCity() {
    constexpr uint32_t colors[]={0xB9A38D,0xCBD1CB,0x9BA8B0,0xAC8872,0x89949F,0xDBCFB7};
    blocks.reserve(144);
    for(int x=-GridRadius;x<GridRadius;++x)for(int z=-GridRadius;z<GridRadius;++z) {
        if(plaza(x,z))continue;
        const DrivingPoint lo{x*Block+roadHalfWidth(x)+7,z*Block+roadHalfWidth(z)+7};
        const DrivingPoint hi{(x+1)*Block-roadHalfWidth(x+1)-7,(z+1)*Block-roadHalfWidth(z+1)-7};
        const auto mid=(lo+hi)*.5;
        for(int a=0;a<2;++a)for(int b=0;b<2;++b) {
            const unsigned seed=unsigned((x+3)*113+(z+3)*41+a*17+b*7);
            const bool downtown=x>=0 && x<2 && z>=-1 && z<2;
            const double h=(downtown ? 25 : 7)+(seed%6)*(downtown ? 8 : 3);
            DrivingPoint p{a ? mid.x+5 : lo.x,b ? mid.z+5 : lo.z};
            DrivingPoint q{a ? hi.x : mid.x-5,b ? hi.z : mid.z-5};
            // Setbacks and gaps form accessible alleys between the buildings.
            p.x+=seed%4;p.z+=(seed/3)%4;
            blocks.push_back({p,q,h,colors[seed%std::size(colors)],downtown && seed%3!=0});
        }
    }
}
const DrivingCity &drivingCity() {static const DrivingCity city;return city;}
const char *DrivingCity::district(DrivingPoint p) {
    if(p.x>320)return "EAST QUAYS";
    if(p.z<-160)return "OLD TOWN";
    if(p.x<0)return "GARDEN DISTRICT";
    return "DOWNTOWN";
}
double DrivingCity::roadDistance(DrivingPoint p) const {
    double d=std::numeric_limits<double>::max();
    for(int i=-GridRadius;i<=GridRadius;++i)
        d=std::min({d,std::abs(p.x-i*Block)-roadHalfWidth(i),std::abs(p.z-i*Block)-roadHalfWidth(i)});
    // Plazas are driveable tarmac too, without an invisible circuit barrier.
    for(auto cell:{DrivingPoint{-1,-1},DrivingPoint{1,1}}) {
        const DrivingPoint lo{cell.x*Block+roadHalfWidth(int(cell.x)),cell.z*Block+roadHalfWidth(int(cell.z))};
        const DrivingPoint hi{(cell.x+1)*Block-roadHalfWidth(int(cell.x)+1),(cell.z+1)*Block-roadHalfWidth(int(cell.z)+1)};
        d=std::min(d,std::max({lo.x-p.x,p.x-hi.x,lo.z-p.z,p.z-hi.z}));
    }
    return d;
}
DrivingHandling::Surface DrivingCity::tyreSurface(DrivingPoint p,double yaw) const {
    const DrivingPoint f{std::sin(yaw),std::cos(yaw)},r{f.z,-f.x};
    auto axle=[&](double offset) {
        double amount=0;
        for(double side:{-1.,1.}) {
            const double t=std::clamp((roadDistance(p+f*offset+r*(side*.78))+.14)/.28,0.0,1.0);
            amount+=t*t*(3-2*t)*.5;
        }
        return amount;
    };
    return {axle(DrivingHandling::FrontAxle),axle(-DrivingHandling::RearAxle)};
}
DrivingCity::Spawn DrivingCity::nearestStreet(DrivingPoint p,double yaw) const {
    p.x=std::clamp(p.x,-GridRadius*Block,GridRadius*Block);
    p.z=std::clamp(p.z,-GridRadius*Block,GridRadius*Block);
    double best=1e30;Spawn result=start();
    for(int i=-GridRadius;i<=GridRadius;++i)for(bool vertical:{false,true}) {
        const double direction=(vertical ? std::cos(yaw) : std::sin(yaw))<0 ? -1 : 1;
        const DrivingPoint q=vertical ? DrivingPoint{i*Block+5*direction,p.z} : DrivingPoint{p.x,i*Block-5*direction};
        const double d=drivingLength(q-p);
        if(d<best) {best=d;result={q,vertical ? (direction>0 ? 0 : 3.141592653589793) : direction*1.5707963267948966};}
    }
    return result;
}
bool DrivingCity::occupied(DrivingPoint p,double radius) const {
    if(std::abs(p.x)+radius>Extent || std::abs(p.z)+radius>Extent)return true;
    for(const auto &b:blocks) {
        const DrivingPoint q{std::clamp(p.x,b.lo.x,b.hi.x),std::clamp(p.z,b.lo.z,b.hi.z)};
        if(drivingLength(p-q)<=radius)return true;
    }
    return false;
}
DrivingCity::Contact DrivingCity::constrain(DrivingPoint &p,double yaw) const {
    Contact contact;
    constexpr double radius=1.12;
    const DrivingPoint forward{std::sin(yaw),std::cos(yaw)};
    // Two circles approximate the body and avoid snagging a rectangular car
    // on building corners. Small 120 Hz steps prevent tunnelling at speed.
    for(int pass=0;pass<3;++pass)for(double axle:{-1.05,1.05}) {
        auto center=p+forward*axle;
        const DrivingPoint bounded{std::clamp(center.x,-Extent+radius,Extent-radius),std::clamp(center.z,-Extent+radius,Extent-radius)};
        if(drivingLength(bounded-center)>.000001) {
            const auto delta=bounded-center;p=p+delta;center=bounded;
            contact={delta*(1/drivingLength(delta)),true};
        }
        for(const auto &b:blocks) {
            if(center.x<b.lo.x-radius || center.x>b.hi.x+radius || center.z<b.lo.z-radius || center.z>b.hi.z+radius)continue;
            const DrivingPoint nearest{std::clamp(center.x,b.lo.x,b.hi.x),std::clamp(center.z,b.lo.z,b.hi.z)};
            auto delta=center-nearest;const double distance=drivingLength(delta);
            if(distance>=radius)continue;
            if(distance>1e-8)delta=delta*((radius-distance+.001)/distance);
            else {
                const double sides[]={center.x-b.lo.x,b.hi.x-center.x,center.z-b.lo.z,b.hi.z-center.z};
                const int side=int(std::min_element(sides,sides+4)-sides);
                const double depth=sides[side]+radius+.001;
                delta=side==0 ? DrivingPoint{-depth,0} : side==1 ? DrivingPoint{depth,0} : side==2 ? DrivingPoint{0,-depth} : DrivingPoint{0,depth};
            }
            p=p+delta;center=center+delta;contact={delta*(1/drivingLength(delta)),true};
        }
    }
    return contact;
}
