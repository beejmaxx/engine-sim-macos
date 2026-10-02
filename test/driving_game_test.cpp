#include "driving_game.h"
#include "vehicle.h"
#include "vehicle_drag_constraint.h"
#include <gtest/gtest.h>
#include <limits>

TEST(DrivingCourse, ClosedCircuitHasContinuousMetreScaledGeometry) {
    DrivingCourse track;EXPECT_GT(track.length(),1000);EXPECT_LT(track.length(),2500);
    const auto start=track.at(0),finish=track.at(track.length());
    EXPECT_NEAR(drivingLength(start.point-finish.point),0,1e-8);
    for(double s=0;s<track.length();s+=5) {
        const auto p=track.at(s),q=track.nearest(p.point+p.right*3);
        EXPECT_NEAR(q.lateral,3,.2);EXPECT_LT(std::abs(std::remainder(q.distance-s,track.length())),1);
    }
}
TEST(DrivingGame, EngineTravelAndSteeringDetermineMotionWithoutGraphics) {
    DrivingGame game;const auto start=game.snapshot();
    for(int i=0;i<120;++i)game.advance(1./120,10./120,10,0);
    const auto straight=game.snapshot();
    EXPECT_NEAR(drivingLength(DrivingPoint{straight.x-start.x,straight.z-start.z}),10,.03);
    for(int i=0;i<60;++i)game.advance(1./120,10./120,10,.6);
    EXPECT_GT(drivingAngle(game.snapshot().yaw-straight.yaw),.08);
    EXPECT_NEAR(game.snapshot().wheelDistance,15,1e-7);
}
TEST(DrivingGame, TireGripLimitsCorneringAndBarriersContainCar) {
    DrivingGame game;
    for(int i=0;i<1000;++i) {
        game.advance(1./120,40./120,40,1);
        EXPECT_LE(std::abs(game.snapshot().lateralG),1.12001);
        const auto p=game.course().nearest({game.snapshot().x,game.snapshot().z});
        EXPECT_LE(std::abs(p.lateral),DrivingCourse::BarrierWidth-1.18+.2);
    }
    EXPECT_GT(game.snapshot().collisions,0);
}
TEST(DrivingGame, OffroadAddsVehicleLoadAndRecoveryUsesCheckpoint) {
    DrivingGame game;bool leftRoad=false,loaded=false;
    for(int i=0;i<600;++i) {
        game.advance(1./120,15./120,15,1);
        leftRoad|=game.snapshot().offroad;loaded|=game.snapshot().roadDeceleration>0;
    }
    EXPECT_TRUE(leftRoad);EXPECT_TRUE(loaded);
    game.recover();game.advance(.01,0,0,0);
    EXPECT_EQ(game.snapshot().recoveries,1);EXPECT_FALSE(game.snapshot().recovering);
    EXPECT_LT(std::abs(game.course().nearest({game.snapshot().x,game.snapshot().z}).lateral),.1);
}
TEST(DrivingGame, FullCourseRequiresOrderedCheckpointsAndFinishesThreeLaps) {
    DrivingGame game;
    for(int i=0;i<60000 && !game.snapshot().finished;++i) {
        const double speed=std::min(18.0,game.pilotSpeed());
        game.advance(.01,speed*.01,speed,game.pilotSteering());
    }
    EXPECT_EQ(game.snapshot().laps,3);EXPECT_TRUE(game.snapshot().finished);
    EXPECT_EQ(game.snapshot().collisions,0);EXPECT_GT(game.snapshot().bestLap,30);
    const double best=game.snapshot().bestLap;
    game.advance(.01,0,0,0);game.restart();
    EXPECT_EQ(game.snapshot().laps,0);EXPECT_FALSE(game.snapshot().started);EXPECT_EQ(game.snapshot().bestLap,best);
}
TEST(DrivingGame, CirclingAndRepeatedFinishAreaDoNotAwardLaps) {
    DrivingGame game;
    for(int i=0;i<20000;++i)game.advance(.01,.05,5,1);
    EXPECT_EQ(game.snapshot().laps,0);EXPECT_FALSE(game.snapshot().finished);
}
TEST(DrivingGame, NoTravelCannotDriftAndBadInputsCannotPoisonGame) {
    DrivingGame game;const auto start=game.snapshot();
    for(int i=0;i<120;++i)game.advance(1./120,0,0,1);
    EXPECT_EQ(game.snapshot().x,start.x);EXPECT_EQ(game.snapshot().z,start.z);
    EXPECT_EQ(game.snapshot().yaw,start.yaw);
    game.advance(.01,1,std::numeric_limits<double>::quiet_NaN(),1);
    EXPECT_TRUE(std::isfinite(game.snapshot().x));EXPECT_EQ(game.snapshot().wheelDistance,0);
}
TEST(VehicleRoadLoad, ResistanceIsBoundedFiniteAndAddsPhysicalDragTorque) {
    Vehicle vehicle;Vehicle::Parameters params{1000,.3,2,3.5,.32,100};vehicle.initialize(params);
    atg_scs::RigidBody body;body.I=100;body.v_theta=60;vehicle.addToSystem(nullptr,&body);
    VehicleDragConstraint drag;drag.initialize(&body,&vehicle);
    VehicleDragConstraint::Output ordinary{},loaded{};drag.calculate(&ordinary,nullptr);
    vehicle.setRoadDeceleration(4);drag.calculate(&loaded,nullptr);
    EXPECT_NEAR(ordinary.limits[0][0]-loaded.limits[0][0],vehicle.linearForceToVirtualTorque(4000),1e-6);
    vehicle.setRoadDeceleration(100);EXPECT_EQ(vehicle.getRoadDeceleration(),45);
    vehicle.setRoadDeceleration(-2);EXPECT_EQ(vehicle.getRoadDeceleration(),0);
    vehicle.setRoadDeceleration(std::numeric_limits<double>::quiet_NaN());EXPECT_EQ(vehicle.getRoadDeceleration(),0);
}
