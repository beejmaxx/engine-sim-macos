#include "compiler.h"
#include "engine.h"
#include "ignition_module.h"
#include "simulator.h"
#include "automatic_transmission.h"

#include <gtest/gtest.h>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <chrono>

TEST(EngineRuntime, DefaultEngineRunsAfterStarterReleaseAndProducesAudio) {
    es_script::Compiler compiler;
    compiler.initialize(ENGINE_SIM_TEST_ASSET_DIRECTORY);
    const auto entry = std::filesystem::path(ENGINE_SIM_TEST_ASSET_DIRECTORY) / "main.mr";
    if (!compiler.compile(entry.string())) {
        compiler.destroy();
        FAIL() << "Default engine script failed to compile";
    }
    auto output = compiler.execute();
    compiler.destroy();
    ASSERT_NE(output.engine, nullptr);
    ASSERT_NE(output.vehicle, nullptr);
    ASSERT_NE(output.transmission, nullptr);

    output.engine->calculateDisplacement();
    auto *simulator = output.engine->createSimulator(output.vehicle, output.transmission, 44100);
    simulator->setSimulationFrequency(output.engine->getSimulationFrequency());
    simulator->setSynthesizerLatencyCorrectionEnabled(false);
    output.engine->getIgnitionModule()->m_enabled = true;
    output.engine->setSpeedControl(0.0);

    int audibleSamples = 0;
    int16_t audio[512] = {};
    for (int frame = 0; frame < 400; ++frame) {
        // Crank for two simulated seconds, then idle for two more.
        simulator->m_starterMotor.m_enabled = frame < 200;
        simulator->startFrame(0.01);
        while (simulator->simulateStep()) { }
        simulator->endFrame();
        simulator->synthesizer().pumpAudioRendering();
        const int received = simulator->readAudioOutput(512, audio);
        audibleSamples += static_cast<int>(std::count_if(audio, audio + received,
            [](int16_t sample) { return std::abs(static_cast<int>(sample)) > 20; }));
    }

    EXPECT_GT(std::abs(output.engine->getRpm()), 200);
    EXPECT_GT(output.engine->getTotalFuelMassConsumed(), 0);
    EXPECT_GT(audibleSamples, 1000);

    simulator->releaseSimulation();
    delete simulator;
    output.engine->destroy();
    delete output.engine;
    delete output.vehicle;
    delete output.transmission;
}

TEST(EngineRuntime, AutomaticDrivetrainAcceleratesShiftsAndStopsUnderVehicleLoad) {
    const std::filesystem::path assets(ENGINE_SIM_TEST_ASSET_DIRECTORY);
    struct Performance { double sixty=0,hundred=0,speed=0; } gt3,sprint;
    for (const char *script : {"engines/atg-video-2/03_2jz.mr", "engines/porsche/01_porsche_911_gt3.mr",
                              "engines/porsche/03_porsche_911_gt3_sprint.mr"}) {
        SCOPED_TRACE(script);
        const auto entry = std::filesystem::temp_directory_path() / ("engine-sim-auto-" +
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".mr");
        { std::ofstream file(entry); file << "import " << std::quoted((assets / script).generic_string()) << "\nmain()\n"; }
        es_script::Compiler compiler;
        compiler.initialize(assets.string());
        const bool compiled = compiler.compile(entry.string());
        std::filesystem::remove(entry);
        if (!compiled) { compiler.destroy(); FAIL() << "Drive test script failed to compile"; }
        auto output = compiler.execute();
        compiler.destroy();
        ASSERT_NE(output.engine, nullptr);
        ASSERT_NE(output.vehicle, nullptr);
        ASSERT_NE(output.transmission, nullptr);
        output.engine->calculateDisplacement();
        auto *simulator = output.engine->createSimulator(output.vehicle, output.transmission, 44100);
        simulator->setSimulationFrequency(std::string(script).find("porsche")!=std::string::npos ? 5000 : 2500);
        simulator->setSynthesizerLatencyCorrectionEnabled(false);
        output.engine->getIgnitionModule()->m_enabled = true;
        AutomaticTransmission automatic;
        automatic.setEnabled(true, *output.transmission);
        int maxGear = 0, previousGear = 0, upshifts = 0, audible = 0;
        double maxSpeed = 0, beforeShiftRpm = 0, biggestRpmDrop = 0, afterShiftTime = -1;
        bool torqueCut = false;
        int brakeUpshifts=0;
        double sixtyDistance=-1,sixtyTime=-1,hundredDistance=-1,hundredTime=-1,stopDistance=-1,stopTime=-1;
        double accelerationSixty=-1,accelerationHundred=-1;
        int16_t samples[512]{};
        constexpr double block=.005; // same controller cadence as the sound host
        for (int frame = 0; frame < 8200; ++frame) {
            const double time = frame * block;
            const bool cranking = time < 2;
            const double pedal = time >= 4 && time < 28 ? 1 : 0;
            output.vehicle->setBrake(time >= 28 ? 1 : 0);
            simulator->m_starterMotor.m_enabled = cranking;
            const double applied = automatic.update(*output.transmission, *output.engine,
                *output.vehicle, pedal, cranking, block);
            torqueCut |= pedal == 1 && automatic.shifting() && applied < .5;
            output.engine->setSpeedControl(std::max(applied, cranking ? .02 : 0));
            simulator->startFrame(block);
            while (simulator->simulateStep()) { }
            simulator->endFrame();
            while (simulator->synthesizer().pumpAudioRendering()) { }
            const int count = simulator->readAudioOutput(512, samples);
            audible += std::count_if(samples, samples + count, [](int16_t v) { return std::abs(int(v)) > 20; });
            const int gear = output.transmission->getGear();
            const double rpm = output.engine->getRpm();
            maxGear = std::max(maxGear, gear);
            maxSpeed = std::max(maxSpeed, output.vehicle->getSpeed());
            if(time>=4 && time<28) {
                if(accelerationSixty<0 && output.vehicle->getSpeed()>=60*.44704)accelerationSixty=time+block-4;
                if(accelerationHundred<0 && output.vehicle->getSpeed()>=100*.44704)accelerationHundred=time+block-4;
            }
            if(time>28.4 && gear>previousGear)++brakeUpshifts; // permit an already started shift
            if(time>=28 && hundredTime<0 && output.vehicle->getSpeed()<=100*.44704) {
                hundredTime=time;hundredDistance=output.vehicle->getTravelledDistance();
            }
            if(time>=28 && sixtyTime<0 && output.vehicle->getSpeed()<=60*.44704) {
                sixtyTime=time;sixtyDistance=output.vehicle->getTravelledDistance();
            }
            if(sixtyTime>=0 && stopTime<0 && output.vehicle->getSpeed()<.5*.44704) {
                stopTime=time;stopDistance=output.vehicle->getTravelledDistance();
            }
            if (gear > previousGear && time < 28) {
                ++upshifts; beforeShiftRpm = rpm; afterShiftTime = time;
            }
            if (afterShiftTime >= 0 && time - afterShiftTime < .8)
                biggestRpmDrop = std::max(biggestRpmDrop, beforeShiftRpm - rpm);
            previousGear = gear;
        }
        EXPECT_GE(maxGear, 2);
        EXPECT_GE(upshifts, 2);
        EXPECT_GT(maxSpeed, 15);
        EXPECT_GT(biggestRpmDrop, 300);
        EXPECT_TRUE(torqueCut);
        EXPECT_GT(audible, 10000);
        EXPECT_GT(accelerationSixty,0);EXPECT_GT(accelerationHundred,accelerationSixty);
        EXPECT_EQ(brakeUpshifts,0);
        ASSERT_GT(sixtyTime,28);ASSERT_GT(stopTime,sixtyTime);
        EXPECT_LT(stopDistance-sixtyDistance,17);EXPECT_LT(stopTime-sixtyTime,1.3);
        ASSERT_GT(hundredTime,28);EXPECT_LT(stopDistance-hundredDistance,44);EXPECT_LT(stopTime-hundredTime,2.1);
        EXPECT_LT(output.vehicle->getSpeed(), 1);
        EXPECT_GT(output.engine->getRpm(), 400);
        EXPECT_EQ(output.transmission->getGear(), 0);
        automatic.setEnabled(false, *output.transmission);
        EXPECT_EQ(output.transmission->getGear(), -1);
        EXPECT_EQ(output.transmission->getClutchPressure(), 0);
        if(std::string(script)=="engines/porsche/01_porsche_911_gt3.mr")gt3={accelerationSixty,accelerationHundred,maxSpeed};
        if(std::string(script)=="engines/porsche/03_porsche_911_gt3_sprint.mr")sprint={accelerationSixty,accelerationHundred,maxSpeed};
        std::cout << "DRIVE " << script << " max_gear=" << maxGear + 1 << " upshifts=" << upshifts
            << " peak_mph=" << maxSpeed / .44704 << " rpm_drop=" << biggestRpmDrop
            << " zero_to_sixty_s=" << accelerationSixty << " zero_to_hundred_s=" << accelerationHundred
            << " stopped_mph=" << output.vehicle->getSpeed() / .44704
            << " sixty_stop_m=" << stopDistance-sixtyDistance << " sixty_stop_s=" << stopTime-sixtyTime
            << " hundred_stop_m=" << stopDistance-hundredDistance << " hundred_stop_s=" << stopTime-hundredTime
            << " idle_rpm=" << output.engine->getRpm() << '\n';
        simulator->releaseSimulation(); delete simulator;
        output.engine->destroy(); delete output.engine;
        delete output.vehicle; delete output.transmission;
    }
    // Exercise the real driveline: the Sprint must accelerate substantially
    // faster while still passing the same shifts, audio and stopping checks.
    EXPECT_LT(sprint.sixty,gt3.sixty*.8);EXPECT_LT(sprint.sixty,3);
    EXPECT_LT(sprint.hundred,gt3.hundred*.75);EXPECT_LT(sprint.hundred,6);
    EXPECT_GT(sprint.speed,gt3.speed*1.15);
}

TEST(EngineRuntime, ReverseBrakesBeforeChangingDirectionAndKeepsEngineRunning) {
    const std::filesystem::path assets(ENGINE_SIM_TEST_ASSET_DIRECTORY);
    for(const char *script:{"engines/atg-video-2/03_2jz.mr","engines/porsche/01_porsche_911_gt3.mr",
                           "engines/porsche/03_porsche_911_gt3_sprint.mr"}) {
        SCOPED_TRACE(script);
        const auto entry=std::filesystem::temp_directory_path()/("engine-sim-reverse-"+
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".mr");
        {std::ofstream f(entry);f<<"import "<<std::quoted((assets/script).generic_string())<<"\nmain()\n";}
        es_script::Compiler compiler;compiler.initialize(assets.string());
        const bool compiled=compiler.compile(entry.string());std::filesystem::remove(entry);
        if(!compiled){compiler.destroy();FAIL()<<"Reverse test script failed to compile";}
        auto output=compiler.execute();compiler.destroy();
        ASSERT_NE(output.engine,nullptr);ASSERT_NE(output.vehicle,nullptr);ASSERT_NE(output.transmission,nullptr);
        output.engine->calculateDisplacement();
        auto *sim=output.engine->createSimulator(output.vehicle,output.transmission,44100);
        sim->setSimulationFrequency(std::string(script).find("porsche")!=std::string::npos ? 5000 : 2500);
        sim->setSynthesizerLatencyCorrectionEnabled(false);
        output.engine->getIgnitionModule()->m_enabled=true;
        AutomaticTransmission automatic;automatic.setEnabled(true,*output.transmission);
        double peakReverse=0,peakForward=0,previousOdometer=0,reverseMetres=0;
        int reverseFrames=0,forwardAfter=0;bool reverseStop=false,guardChecked=false;
        int16_t pcm[512]{};int audible=0;
        for(int frame=0;frame<4200;++frame) {
            const double t=frame*.005;
            const bool back=t>=6 && t<15,brake=(t>=10 && t<13)||t>=18;
            const double pedal=(t>=3 && t<6)||(t>=15 && t<18) ? .65 : 0;
            output.vehicle->setBrake(brake);
            const double speedBefore=output.vehicle->getSpeed();const int directionBefore=output.vehicle->getTravelDirection();
            if(!guardChecked && t>=5 && speedBefore>3) {
                const int gear=output.transmission->getGear();output.transmission->changeGear(Transmission::Reverse);
                EXPECT_EQ(output.transmission->getGear(),gear);guardChecked=true;
            }
            const double applied=automatic.update(*output.transmission,*output.engine,*output.vehicle,pedal,t<2,.005,back);
            if(output.vehicle->getTravelDirection()!=directionBefore)EXPECT_LT(speedBefore,.15);
            sim->m_starterMotor.m_enabled=t<2;output.engine->setSpeedControl(std::max(applied,t<2 ? .02 : 0));
            sim->startFrame(.005);while(sim->simulateStep()){}sim->endFrame();
            while(sim->synthesizer().pumpAudioRendering()){}
            const int count=sim->readAudioOutput(512,pcm);
            audible+=std::count_if(pcm,pcm+count,[](int16_t x){return std::abs(int(x))>20;});
            const double speed=output.vehicle->getSignedSpeed();
            EXPECT_GE(output.vehicle->getTravelledDistance(),previousOdometer);previousOdometer=output.vehicle->getTravelledDistance();
            if(speed<-.5){++reverseFrames;peakReverse=std::max(peakReverse,-speed);reverseMetres-=speed*.005;}
            if(t>16 && speed>1)++forwardAfter;
            if(t<6)peakForward=std::max(peakForward,speed);
            if(t>12 && t<13) {
                EXPECT_EQ(output.transmission->getGear(),Transmission::Reverse);
                EXPECT_LT(std::abs(speed),.2);reverseStop=true;
            }
        }
        EXPECT_TRUE(guardChecked);EXPECT_TRUE(reverseStop);EXPECT_GT(peakForward,5);
        EXPECT_GT(reverseFrames,200);EXPECT_GT(reverseMetres,8);EXPECT_LT(peakReverse,8);
        EXPECT_GT(forwardAfter,100);EXPECT_GT(audible,10000);
        EXPECT_LT(output.vehicle->getSpeed(),.2);EXPECT_GT(output.engine->getRpm(),400);
        EXPECT_EQ(output.transmission->getGear(),0);
        std::cout<<"REVERSE "<<script<<" peak_reverse_mph="<<peakReverse/.44704<<" reverse_m="<<reverseMetres
            <<" idle_rpm="<<output.engine->getRpm()<<'\n';
        sim->releaseSimulation();delete sim;output.engine->destroy();delete output.engine;delete output.vehicle;delete output.transmission;
    }
}
