#include "driving_city.h"
#include <algorithm>
#include <limits>

DrivingCity::Lot DrivingCity::lot(int x,int z) {
    if(plaza(x,z))return Lot::Plaza;
    if(x==-5 && z==-1)return Lot::Stadium;
    if((x==0 && z==-3)||(x==5 && z==-6)||(x==-7 && z==3))return Lot::Fuel;
    if(x>=7 && z>=-2 && z<=2)return Lot::Harbor;
    if((x>=-3 && x<=-2 && z>=0 && z<=2) || (x==-6 && z==5) ||
       (x==3 && z==5) || (x==-8 && z==-3) || (x==4 && z==-4))return Lot::Park;
    return Lot::Built;
}
DrivingCity::Zone DrivingCity::zone(DrivingPoint p) {
    if(p.x>=960)return Zone::Harbor;
    if(p.x<-1120)return Zone::Beach;
    if(p.z<-960)return Zone::Industrial;
    if(p.x<-480 && p.z>=640)return Zone::University;
    if(p.z>=640)return Zone::Northside;
    if(p.x<-320 && p.z>=-320)return Zone::Garden;
    if(p.z<-160)return Zone::OldTown;
    return Zone::Downtown;
}
DrivingCity::DrivingCity() {
    constexpr uint32_t masonry[]={0xC1AA8B,0xC9CCC5,0x9FABB1,0xAE7862,0x9C9690,0xD4C8AA};
    constexpr uint32_t houses[]={0xDDD3BC,0xB8C3BF,0xC49A80,0x9EB9BC,0xD2BB91,0xB7AFB0};
    constexpr uint32_t containers[]={0x9E4135,0x306B80,0xBA8D3F,0x53735F,0xBFBCB0};
    blocks.reserve(1600);
    auto add=[&](DrivingPoint lo,DrivingPoint hi,double h,uint32_t color,Style style) {
        blocks.push_back({lo,hi,h,color,style==Style::Glass,style});
    };
    for(int x=-GridRadius;x<GridRadius;++x)for(int z=-GridRadius;z<GridRadius;++z) {
        const auto use=lot(x,z);
        const DrivingPoint lo{x*Block+roadHalfWidth(x)+8,z*Block+roadHalfWidth(z)+8};
        const DrivingPoint hi{(x+1)*Block-roadHalfWidth(x+1)-8,(z+1)*Block-roadHalfWidth(z+1)-8};
        const auto mid=(lo+hi)*.5;
        const unsigned seed=unsigned((x+9)*113+(z+9)*41);
        if(use==Lot::Park || use==Lot::Plaza)continue;
        if(use==Lot::Stadium) {
            add(lo,{lo.x+13,hi.z},18,0xC8C4B9,Style::Stadium);
            add({hi.x-13,lo.z},hi,18,0xC8C4B9,Style::Stadium);
            add({lo.x+20,hi.z-12},{hi.x-20,hi.z},12,0xD4CEC0,Style::Stadium);
            continue;
        }
        if(use==Lot::Fuel) {
            add({lo.x+4,hi.z-24},{hi.x-4,hi.z-4},5,0xE1D8BA,Style::Civic);
            continue;
        }
        if(use==Lot::Harbor) {
            for(int a=0;a<3;++a)for(int b=0;b<3;++b) {
                DrivingPoint p{lo.x+6+a*28,lo.z+6+b*29};
                add(p,p+DrivingPoint{12,24},(1+(a+b)%3)*2.7,containers[(seed+a+b)%5],Style::Container);
            }
            continue;
        }
        const auto area=zone(mid);
        if(area==Zone::Industrial) {
            add(lo,{mid.x-7,hi.z},10+seed%5,masonry[seed%6],Style::Warehouse);
            add({mid.x+7,lo.z},hi,8+seed%7,masonry[(seed+2)%6],Style::Warehouse);
        } else if(area==Zone::Garden || area==Zone::Northside || area==Zone::Beach) {
            // Smaller footprints, gardens and driveways distinguish residential
            // streets from the downtown canyon, without blocking any through road.
            for(int a=0;a<3;++a)for(int b=0;b<2;++b) {
                const DrivingPoint p{lo.x+5+a*(hi.x-lo.x)/3,lo.z+9+b*(hi.z-lo.z)/2};
                add(p,p+DrivingPoint{22,28},6+(seed+a+b)%3,houses[(seed+a+b*2)%6],Style::House);
            }
        } else if(area==Zone::University) {
            add(lo,{lo.x+26,hi.z},18,0xB89073,Style::Civic);
            add({hi.x-26,lo.z},hi,14,0xBDAA90,Style::Civic);
            add({lo.x+36,hi.z-24},{hi.x-36,hi.z},9,0xC6B194,Style::Civic);
        } else {
            const bool downtown=area==Zone::Downtown;
            for(int a=0;a<2;++a)for(int b=0;b<2;++b) {
                const unsigned n=seed+a*17+b*7;
                DrivingPoint p{a ? mid.x+6 : lo.x,b ? mid.z+6 : lo.z};
                DrivingPoint q{a ? hi.x : mid.x-6,b ? hi.z : mid.z-6};
                p.x+=n%4;p.z+=(n/3)%4;
                const double h=downtown ? 30+(n%9)*13 : 10+(n%4)*4;
                add(p,q,h,masonry[n%6],downtown && n%3!=0 ? Style::Glass : Style::Masonry);
            }
            // Recognizable stepped skyline landmark at the financial centre.
            if(x==2 && z==1) {blocks.back().height=204;blocks.back().style=Style::Glass;blocks.back().glass=true;}
        }
    }
    // Broad phase is immutable, allocation-free at 120 Hz, and shared by body
    // contacts and occupied queries. Renderer culling has its own static cells.
    for(unsigned i=0;i<blocks.size();++i) {
        const auto &b=blocks[i];
        for(int x=cell(b.lo.x);x<=cell(b.hi.x);++x)
            for(int z=cell(b.lo.z);z<=cell(b.hi.z);++z)spatial[x*Cells+z].push_back(i);
    }
}
const DrivingCity &drivingCity() {static const DrivingCity city;return city;}
const char *DrivingCity::district(DrivingPoint p) {
    switch(zone(p)) {
        case Zone::Downtown:return "DOWNTOWN";
        case Zone::OldTown:return "OLD TOWN";
        case Zone::Garden:return "GARDEN DISTRICT";
        case Zone::University:return "UNIVERSITY";
        case Zone::Northside:return "NORTHSIDE";
        case Zone::Harbor:return "EAST QUAYS";
        case Zone::Industrial:return "SOUTH INDUSTRIAL";
        case Zone::Beach:return "OCEAN DRIVE";
    }
    return "PORTSIDE";
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
    for(int x=cell(p.x-radius);x<=cell(p.x+radius);++x)
      for(int z=cell(p.z-radius);z<=cell(p.z+radius);++z)for(unsigned id:spatial[x*Cells+z]) {
        const auto &b=blocks[id];
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
        const int x0=cell(center.x-radius),x1=cell(center.x+radius),z0=cell(center.z-radius),z1=cell(center.z+radius);
        for(int x=x0;x<=x1;++x)for(int z=z0;z<=z1;++z)for(unsigned id:spatial[x*Cells+z]) {
            const auto &b=blocks[id];
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
