#include "vehicle_visual_motion.h"
#include <gtest/gtest.h>

TEST(VehicleVisualMotion, SixtyMphMoves268224MetresInTenSecondsAtDisplayRate) {
    VehicleVisualMotion motion;double simulation=0,previous=0,first=0,last=0;
    constexpr double speed=60*.44704,dt=1.0/60;
    for(int frame=0;frame<=660;++frame) {
        while(simulation<(frame+1)*dt) {
            simulation+=.005;motion.accept(simulation,simulation*speed,speed);
        }
        const auto shown=motion.advance(dt);
        if(frame==60)first=shown.distance;
        if(frame>60)EXPECT_NEAR(shown.distance-previous,speed*dt,1e-8);
        if(frame==660)last=shown.distance;
        previous=shown.distance;
    }
    EXPECT_NEAR(last-first,268.224,1e-7);
}
TEST(VehicleVisualMotion, StoppedVehicleDoesNotDriftOrInventMotionWhenSnapshotsStop) {
    VehicleVisualMotion motion;
    for(int i=0;i<100;++i)motion.accept(i*.005,42,0);
    for(int i=0;i<120;++i)EXPECT_DOUBLE_EQ(motion.advance(1.0/60).distance,42);
    motion.reset();motion.accept(0,0,0);EXPECT_DOUBLE_EQ(motion.advance(.016).distance,0);
}
