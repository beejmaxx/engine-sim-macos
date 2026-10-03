#ifndef ENGINE_SIM_DRIVING_TEST_DRIVER_H
#define ENGINE_SIM_DRIVING_TEST_DRIVER_H
#include "driving_game.h"
#include <algorithm>

// Programmatic checks only. Called at 10 Hz, returning actual left/released/
// right keys, never an analogue steering fraction. Preview a 100 ms press and
// its release so the test exercises the same ramp and chassis as a player.
inline double drivingKeyboardTestInput(const DrivingGame &game,double speed) {
    double best=1e30,chosen=0;
    for(double key:{0.0,-1.0,1.0}) {
        auto preview=game;double cost=0;
        for(int i=0;i<42;++i) {
            preview.advance(1./120,speed/120,speed,i<12 ? key : 0,DrivingHandling::Input::Keyboard);
            const auto &pose=preview.snapshot();
            const auto road=preview.course().at(pose.progress+std::clamp(speed*.3,3.0,12.0));
            const double angle=drivingAngle(pose.velocityYaw-std::atan2(road.forward.x,road.forward.z));
            cost+=(pose.lateral*pose.lateral+angle*angle*speed*speed*.25)/42;
        }
        if(cost<best) {best=cost;chosen=key;}
    }
    return chosen;
}
#endif
