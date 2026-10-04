#ifndef ENGINE_SIM_DRIVING_CAMERA_H
#define ENGINE_SIM_DRIVING_CAMERA_H
#include "driving_game.h"
#include <algorithm>

// Device-free camera/projection maths, shared with the renderer and tested
// without a window. One heading response keeps the chase offset and aim in sync.
class DrivingCamera {
public:
    struct Vector {
        double x=0,y=0,z=0;
        Vector operator+(Vector p) const {return {x+p.x,y+p.y,z+p.z};}
        Vector operator-(Vector p) const {return {x-p.x,y-p.y,z-p.z};}
        Vector operator*(double s) const {return {x*s,y*s,z*s};}
    };
    struct Projection {double x=0,y=0,radius=0;bool visible=false;};
    // A distant, world-fixed sun. Driving changes its view direction, never
    // its position relative to the landscape. Its angular diameter is 0.53 deg.
    static constexpr Vector SunDirection{.36,.14,1};
    static constexpr double SunRadius=.00465;

    void update(const DrivingSnapshot &pose,double dt) {
        const bool reset=!ready || pose.time<lastTime || pose.recoveries!=lastRecovery || pose.world!=lastWorld;
        // Follow travel through a drift and look behind the car in reverse.
        const double target=drivingAngle(pose.yaw+pose.driftAngle+(pose.reversing ? 3.141592653589793 : 0));
        lastWorld=pose.world;
        if(reset) {yaw=target;ready=true;}
        else if(std::isfinite(dt) && dt>0)
            yaw=drivingAngle(yaw+drivingAngle(target-yaw)*(1-std::exp(-std::min(dt,.1)*16)));
        const Vector f{std::sin(yaw),0,std::cos(yaw)},r{f.z,0,-f.x},p{pose.x,0,pose.z};
        // Follow translation exactly: speed must not make the car lag or change
        // its distance from the camera. Rendered game poses are already smooth.
        position=p-f*8.4+r*.38+Vector{0,2.55,0};
        forward=unit(p+f*9+Vector{0,.82,0}-position);
        right=unit(cross({0,1,0},forward));up=cross(forward,right);
        lastTime=pose.time;lastRecovery=pose.recoveries;
    }
    Projection sun(double width,double height) const {
        const auto direction=unit(SunDirection);
        const double depth=dot(direction,forward);
        if(depth<=.05)return {};
        Projection p{width*.5+dot(direction,right)/depth*height,
            height*.48-dot(direction,up)/depth*height,SunRadius*height/depth,false};
        const double halo=p.radius*18;
        p.visible=p.x+halo>=0 && p.x-halo<=width && p.y+halo>=0 && p.y-halo<=height;
        return p;
    }
    Vector position{},right{1,0,0},up{0,1,0},forward{0,0,1};
private:
    static double dot(Vector a,Vector b) {return a.x*b.x+a.y*b.y+a.z*b.z;}
    static Vector unit(Vector p) {return p*(1/std::max(1e-8,std::sqrt(dot(p,p))));}
    static Vector cross(Vector a,Vector b) {return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
    double yaw=0,lastTime=-1;
    unsigned lastRecovery=0;
    DrivingWorld lastWorld=DrivingWorld::Circuit;
    bool ready=false;
};
#endif
