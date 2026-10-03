#include "driving_game.h"
#include "driving_camera.h"
#include "driving_test_driver.h"
#include "vehicle.h"
#include "vehicle_drag_constraint.h"
#include <gtest/gtest.h>
#include <limits>

TEST(DrivingCourse, ClosedCircuitHasContinuousMetreScaledGeometry) {
    DrivingCourse track;EXPECT_GT(track.length(),3500);EXPECT_LT(track.length(),5000);
    const auto start=track.at(0),finish=track.at(track.length());
    EXPECT_NEAR(drivingLength(start.point-finish.point),0,1e-8);
    for(double s=0;s<track.length();s+=5) {
        const auto p=track.at(s),q=track.nearest(p.point+p.right*3);
        EXPECT_NEAR(q.lateral,3,.2);EXPECT_LT(std::abs(std::remainder(q.distance-s,track.length())),1);
    }
}
TEST(DrivingCourse, CornerAdviceWarnsForHighSpeedApproachBeforeReachingCorner) {
    DrivingCourse course;bool warning=false;
    for(double distance=0;distance<course.length();distance+=5) {
        const auto advice=course.cornerAdvice(distance,120*.44704);
        if(advice.deceleration>6 && advice.distance>20) {
            warning=true;EXPECT_LT(advice.speed,120*.44704);EXPECT_NE(advice.direction,0);
        }
    }
    EXPECT_TRUE(warning);
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
TEST(DrivingGame, ArcadeTurnRateIsBoundedAndBarriersContainCar) {
    DrivingGame game;
    for(int i=0;i<1000;++i) {
        game.advance(1./120,40./120,40,1);
        EXPECT_LE(std::abs(game.snapshot().lateralG),DrivingHandling::maximumCurvature(40)*40*40/DrivingHandling::Gravity+1e-5);
        const auto p=game.course().nearest({game.snapshot().x,game.snapshot().z});
        EXPECT_LE(std::abs(p.lateral),DrivingCourse::BarrierWidth-1.18+.2);
    }
    EXPECT_GT(game.snapshot().collisions,0);
}
TEST(DrivingHandling, ShortKeyboardTapsAllowSmallHighSpeedCorrections) {
    for(double speed:{26.8224,44.704}) { // 60 and 100 mph
        DrivingHandling chassis;double yaw=0,lateral=0,peakG=0;
        for(int i=0;i<360;++i) {
            chassis.advance(1./240,speed,i<24 ? 1 : 0,DrivingHandling::Input::Keyboard,{});
            const auto &p=chassis.snapshot();yaw+=p.yawRate/240;
            lateral+=std::sin(yaw+std::atan2(p.lateralSpeed,speed))*speed/240;
            peakG=std::max(peakG,std::abs(p.lateralG));
            if(i==11)EXPECT_GT(p.steer,0); // wheel response is visible within 50 ms
        }
        EXPECT_GT(lateral,.1);EXPECT_LT(lateral,.5); // useful correction, not a lane jump
        EXPECT_LT(std::abs(yaw),.0175); // less than one degree
        EXPECT_LT(peakG,.5);EXPECT_LT(std::abs(chassis.snapshot().lateralG),.01);
        EXPECT_LT(std::abs(chassis.snapshot().yawRate),.001);
    }
}
TEST(DrivingHandling, HeldKeyboardTurnBuildsGripAndCountersteeringRecovers) {
    for(double speed:{15.,26.8224,44.704}) {
        DrivingHandling chassis;
        for(int i=0;i<120;++i)chassis.advance(1./240,speed,1,DrivingHandling::Input::Keyboard,{});
        EXPECT_GT(chassis.snapshot().lateralG,.6); // held input still corners decisively
        for(int i=0;i<144;++i) {
            chassis.advance(1./240,speed,-1,DrivingHandling::Input::Keyboard,{});
            EXPECT_LE(std::abs(chassis.snapshot().lateralG),DrivingHandling::maximumCurvature(speed)*speed*speed/DrivingHandling::Gravity+1e-5);
        }
        EXPECT_LT(chassis.snapshot().yawRate,0);EXPECT_LT(chassis.snapshot().lateralG,-.4);
        for(int i=0;i<480;++i)chassis.advance(1./240,speed,0,DrivingHandling::Input::Keyboard,{});
        EXPECT_LT(std::abs(chassis.snapshot().yawRate),.001);
        EXPECT_LT(std::abs(chassis.snapshot().lateralG),.01);
    }
}
TEST(DrivingHandling, ReleasingHighSpeedTurnSettlesPromptlyWithoutYawSwing) {
    for(double speed:{26.8224,44.704}) {
        DrivingHandling chassis;
        for(int i=0;i<240;++i)chassis.advance(1./240,speed,1,DrivingHandling::Input::Keyboard,{});
        ASSERT_GT(chassis.snapshot().lateralG,.85);
        // Release must straighten the trajectory before a small correction
        // becomes a wall strike, without turning the body the other way.
        for(int i=0;i<360;++i) {
            chassis.advance(1./240,speed,0,DrivingHandling::Input::Keyboard,{});
            EXPECT_GT(chassis.snapshot().yawRate,-.005);
            if(i==23)EXPECT_LT(std::abs(chassis.snapshot().lateralG),.1); // 100 ms
        }
        EXPECT_LT(std::abs(chassis.snapshot().yawRate),.001);
        EXPECT_LT(std::abs(chassis.snapshot().lateralG),.01);
    }
}
TEST(DrivingHandling, HighSpeedCountersteerReachesOppositeCornerWithin200ms) {
    for(double speed:{26.8224,44.704}) {
        DrivingHandling chassis;
        for(int i=0;i<240;++i)chassis.advance(1./240,speed,1,DrivingHandling::Input::Keyboard,{});
        for(int i=0;i<48;++i)chassis.advance(1./240,speed,-1,DrivingHandling::Input::Keyboard,{});
        EXPECT_LT(chassis.snapshot().lateralG,-.7);
        EXPECT_LT(chassis.snapshot().yawRate,0);
    }
}
TEST(DrivingCourse, ShoulderGripChangesOnlyAsTyreContactPatchesLeaveTarmac) {
    DrivingCourse::Location straight;
    straight.lateral=DrivingCourse::HalfWidth-1;
    const auto tarmac=DrivingCourse::tyreSurface(straight,0);
    EXPECT_EQ(tarmac.front,0);EXPECT_EQ(tarmac.rear,0);
    straight.lateral=DrivingCourse::HalfWidth-.78;
    const auto edge=DrivingCourse::tyreSurface(straight,0);
    EXPECT_NEAR(edge.front,.25,1e-9);EXPECT_NEAR(edge.rear,.25,1e-9);
    straight.lateral=DrivingCourse::HalfWidth-.5;
    const auto outsidePair=DrivingCourse::tyreSurface(straight,0);
    EXPECT_EQ(outsidePair.front,.5);EXPECT_EQ(outsidePair.rear,.5);
    const auto angled=DrivingCourse::tyreSurface(straight,.5);
    EXPECT_GT(angled.front,angled.rear); // front tyres cross first
    straight.lateral=DrivingCourse::HalfWidth+1;
    const auto grass=DrivingCourse::tyreSurface(straight,0);
    EXPECT_EQ(grass.front,1);EXPECT_EQ(grass.rear,1);
    double previous=0;
    for(double lateral=DrivingCourse::HalfWidth-1.1;lateral<DrivingCourse::HalfWidth-.4;lateral+=.005) {
        straight.lateral=lateral;
        const auto surface=DrivingCourse::tyreSurface(straight,0);
        EXPECT_GE(surface.front,previous);EXPECT_LT(surface.front-previous,.02);
        previous=surface.front;
    }
}
TEST(DrivingHandling, ShoulderRetainsSteeringAuthorityWithoutStoredSideslip) {
    for(auto surface:{DrivingHandling::Surface{.5,.5},{0,.5},{.5,0},{1,1}}) {
        DrivingHandling chassis;
        for(int i=0;i<480;++i)chassis.advance(1./240,30,1,DrivingHandling::Input::Keyboard,surface);
        const double full=DrivingHandling::maximumCurvature(30)*30;
        EXPECT_GE(chassis.snapshot().yawRate,full*.84);EXPECT_LE(chassis.snapshot().yawRate,full);
        EXPECT_EQ(chassis.snapshot().lateralSpeed,0);
        for(int i=0;i<480;++i)chassis.advance(1./240,30,0,DrivingHandling::Input::Keyboard,surface);
        EXPECT_LT(std::abs(chassis.snapshot().lateralG),.01);
        EXPECT_LT(std::abs(chassis.snapshot().yawRate),.001);
    }
}
TEST(DrivingHandling, ArcadeResponseIsSmoothAndStableAtLowSpeed) {
    DrivingHandling highway;highway.advance(1./240,44.704,1,DrivingHandling::Input::Analog,{});
    EXPECT_GT(highway.snapshot().yawRate,0);EXPECT_LT(highway.snapshot().yawRate,.12);
    for(double speed:{0.,2.,5.,10.,30.,60.}) {
        DrivingHandling fine,batched;
        for(int i=0;i<120;++i)fine.advance(1./240,speed,.7,DrivingHandling::Input::Keyboard,{});
        for(int i=0;i<30;++i)batched.advance(1./60,speed,.7,DrivingHandling::Input::Keyboard,{});
        EXPECT_NEAR(fine.snapshot().yawRate,batched.snapshot().yawRate,1e-9);
        EXPECT_NEAR(fine.snapshot().lateralSpeed,batched.snapshot().lateralSpeed,1e-9);
        EXPECT_LE(std::abs(fine.snapshot().yawRate),DrivingHandling::maximumCurvature(speed)*speed+1e-5);
    }
}
TEST(DrivingHandling, ArcadeFullLockCanTurnAt100mphWithoutRealisticUndersteer) {
    DrivingHandling chassis;
    for(int i=0;i<120;++i)chassis.advance(1./240,44.704,1,DrivingHandling::Input::Keyboard,{});
    EXPECT_GT(chassis.snapshot().lateralG,3.4);
    EXPECT_LT(44.704/chassis.snapshot().yawRate,60); // old tune needed ~170 m
    EXPECT_EQ(chassis.snapshot().lateralSpeed,0);
    chassis.impact(44.704,.5);
    EXPECT_EQ(chassis.snapshot().lateralSpeed,0);
    chassis.advance(.01,0,0,DrivingHandling::Input::Keyboard,{});
    EXPECT_EQ(chassis.snapshot().yawRate,0);
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
    for(int i=0;i<100000 && !game.snapshot().finished;++i) {
        const double speed=std::min(18.0,game.pilotSpeed());
        game.advance(.01,speed*.01,speed,game.pilotSteering());
    }
    EXPECT_EQ(game.snapshot().laps,3);EXPECT_TRUE(game.snapshot().finished);
    EXPECT_EQ(game.snapshot().collisions,0);EXPECT_GT(game.snapshot().bestLap,30);
    const double best=game.snapshot().bestLap;
    game.advance(.01,0,0,0);game.restart();
    EXPECT_EQ(game.snapshot().laps,0);EXPECT_FALSE(game.snapshot().started);EXPECT_EQ(game.snapshot().bestLap,best);
}
TEST(DrivingGame, KeyboardPressesAt10HzCompleteCircuitWithoutTouchingShoulder) {
    DrivingGame game;double speed=0,key=0,maxLateral=0,peakSpeed=0;
    for(int i=0;i<18000 && game.snapshot().laps<1;++i) {
        // Accelerate and brake into the actual course's tight corners, rather
        // than teleporting the speed to a safe value or using analogue steer.
        speed+=std::clamp(game.pilotSpeed()-speed,-7.5/120,3.5/120);
        if(i%12==0)key=drivingKeyboardTestInput(game,speed);
        game.advance(1./120,speed/120,speed,key,DrivingHandling::Input::Keyboard);
        maxLateral=std::max(maxLateral,std::abs(game.snapshot().lateral));
        peakSpeed=std::max(peakSpeed,speed);
        ASSERT_EQ(game.snapshot().collisions,0);
        ASSERT_FALSE(game.snapshot().offroad);
    }
    EXPECT_EQ(game.snapshot().laps,1);EXPECT_GT(peakSpeed,36); // over 80 mph
    EXPECT_LT(maxLateral,DrivingCourse::HalfWidth-1.1);
}
TEST(DrivingGame, KeyboardCanTakeFirstSweepingBendAt100mph) {
    DrivingGame game;double key=0;
    for(int i=0;i<2400;++i) {
        if(i%12==0)key=drivingKeyboardTestInput(game,44.704);
        game.advance(1./120,44.704/120,44.704,key,DrivingHandling::Input::Keyboard);
        ASSERT_EQ(game.snapshot().collisions,0);
        ASSERT_FALSE(game.snapshot().offroad);
    }
    EXPECT_GT(game.snapshot().progress,800);
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
