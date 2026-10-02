#ifndef ENGINE_SIM_VISUAL_STATE_H
#define ENGINE_SIM_VISUAL_STATE_H
#include <array>
#include <cstdint>

// Plain, bounded value types at the simulation/visualization boundary.
// The worker publishes copies; the renderer never follows live physics pointers.
constexpr int VisualMaxCylinders=32, VisualMaxBanks=16, VisualMaxCranks=8;
struct VisualPose { float x=0,y=0,angle=0; };
struct EngineVisualLayout {
    struct Point { float x=0,y=0; };
    struct Bank {
        float x=0,y=0,dx=0,dy=1,angle=0,bore=0,deck=0,chamberHeight=0,displayDepth=1;
        float intakeBaseRadius=0,exhaustBaseRadius=0;
        std::array<Point,64> intakeCam{},exhaustCam{};
        int cylinders=0;bool flip=false;
    };
    struct Cylinder {
        int bank=0,index=0,crank=0,layer=0;
        float compression=0,wrist=0,rodBig=0,rodLittle=0,rodLength=0;
    };
    struct Crank {
        float x=0,y=0,radius=0;
        int journals=0;
        std::array<float,VisualMaxCylinders> journalAngles{};
    };
    std::array<Bank,VisualMaxBanks> banks{};
    std::array<Cylinder,VisualMaxCylinders> cylinders{};
    std::array<Crank,VisualMaxCranks> cranks{};
    int bankCount=0,cylinderCount=0,crankCount=0,maxLayer=0;
    float displacementLiters=0;
};
struct EngineVisualSnapshot {
    struct Cylinder {
        VisualPose piston,rod;
        float pressure=0,temperature=0,volume=0,intakeLift=0,exhaustLift=0;
        float intakeCamAngle=0,exhaustCamAngle=0,flameX=0,flameY=0;
        bool lit=false,fired=false;
    };
    std::array<Cylinder,VisualMaxCylinders> cylinders{};
    std::array<VisualPose,VisualMaxCranks> cranks{};
    uint64_t block=0;
    double simulatedSeconds=0;
    float manifoldPressure=0,intakeFlow=0,afr=0,exhaustO2=0,exhaustFlow=0;
    float fuelLiters=0,vehicleSpeed=0,torque=0,power=0,throttleAngle=0;
    float highFrequency=0,lowNoise=0,levelerGain=0,physicsHz=0;
    float clutch=0,dynoRpm=1000;
    int gear=-1;
    bool dyno=false;
};
#endif
