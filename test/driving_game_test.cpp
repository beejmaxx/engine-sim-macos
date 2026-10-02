#include "driving_game.h"
#include "driving_camera.h"
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
TEST(DrivingGame, SteeringRespondsAndStopsTurningPromptlyAfterRelease) {
    DrivingGame game;
    constexpr double dt=1./240,speed=15;
    for(int i=0;i<12;++i)game.advance(dt,speed*dt,speed,1);
    EXPECT_GT(game.snapshot().lateralG,.3); // perceptible response within 50 ms
    for(int i=0;i<12;++i)game.advance(dt,speed*dt,speed,1);
    double last=game.snapshot().lateralG;
    EXPECT_GT(last,.65);
    for(int i=0;i<24;++i) {
        game.advance(dt,speed*dt,speed,0);
        EXPECT_LE(game.snapshot().lateralG,last+1e-8);
        last=game.snapshot().lateralG;
    }
    EXPECT_LT(last,.05); // centre within 100 ms, no delayed increase in turn
    EXPECT_EQ(game.snapshot().collisions,0);
}
TEST(DrivingGame, CountersteeringReversesTheTurnWithinOneTenthOfASecond) {
    DrivingGame game;
    constexpr double dt=1./240,speed=15;
    for(int i=0;i<48;++i)game.advance(dt,speed*dt,speed,1);
    ASSERT_GT(game.snapshot().lateralG,.8);
    for(int i=0;i<24;++i)game.advance(dt,speed*dt,speed,-1);
    EXPECT_LT(game.snapshot().lateralG,-.65);
    EXPECT_EQ(game.snapshot().collisions,0);
}
TEST(DrivingGame, HighSpeedKeyboardLockDoesNotOverwhelmRoadGrip) {
    for(double speed:{20.,30.,40.,60.}) {
        DrivingGame game;
        for(int i=0;i<24;++i)game.advance(1./120,speed/120,speed,1);
        EXPECT_GT(game.snapshot().lateralG,.8);
        EXPECT_LT(game.snapshot().lateralG,1.0);
        EXPECT_FALSE(game.snapshot().offroad);
        // The chassis follows its steered wheels, rather than a lagging fake
        // velocity angle that swings outside the corner after releasing input.
        EXPECT_GT(drivingAngle(game.snapshot().velocityYaw-game.snapshot().yaw),0);
    }
}
TEST(DrivingGame, SteeringMotionIsIndependentOfUpdateBatchSize) {
    DrivingGame fine,batched;
    for(int i=0;i<60;++i)fine.advance(1./120,12./120,12,.7);
    for(int i=0;i<15;++i)batched.advance(1./30,12./30,12,.7);
    EXPECT_NEAR(fine.snapshot().x,batched.snapshot().x,1e-8);
    EXPECT_NEAR(fine.snapshot().z,batched.snapshot().z,1e-8);
    EXPECT_NEAR(fine.snapshot().yaw,batched.snapshot().yaw,1e-8);
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
TEST(DrivingCamera, SunRotatesWithViewAndDisappearsBehindCamera) {
    DrivingCamera straight,turned,back;
    DrivingSnapshot pose;
    straight.update(pose,1./60);const auto a=straight.sun(1280,800);
    pose.yaw=.15;turned.update(pose,1./60);const auto b=turned.sun(1280,800);
    EXPECT_TRUE(a.visible);EXPECT_TRUE(b.visible);EXPECT_LT(b.x,a.x-100);
    EXPECT_GT(a.radius,3);EXPECT_LT(a.radius,6);
    pose.yaw=3.141592653589793;back.update(pose,1./60);
    EXPECT_FALSE(back.sun(1280,800).visible);
}
TEST(DrivingCamera, TravelDoesNotMoveDistantSunOrChangeChaseDistance) {
    DrivingCamera camera;DrivingSnapshot pose;camera.update(pose,1./60);
    const auto a=camera.sun(1280,800);
    pose.x=150;pose.z=300;pose.time=10;pose.speed=30;camera.update(pose,1./60);
    const auto b=camera.sun(1280,800);
    EXPECT_NEAR(a.x,b.x,1e-8);EXPECT_NEAR(a.y,b.y,1e-8);
    EXPECT_NEAR(camera.position.x-pose.x,.38,1e-8);
    EXPECT_NEAR(camera.position.z-pose.z,-8.4,1e-8);
}
TEST(DrivingCamera, HeadingFollowsQuicklyAndRecoveryResetsImmediately) {
    DrivingCamera camera;DrivingSnapshot pose;camera.update(pose,1./60);
    pose.yaw=.4;
    for(int i=0;i<10;++i) {pose.time+=1./60;camera.update(pose,1./60);}
    EXPECT_NEAR(std::atan2(camera.forward.x,camera.forward.z),.4,.05);
    pose.recoveries=1;pose.yaw=-.7;camera.update(pose,1./60);
    // Small horizontal chase offset is deliberate and must not act as another
    // lag filter during a recovery teleport.
    EXPECT_NEAR(std::atan2(camera.forward.x,camera.forward.z),-.7,.03);
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
