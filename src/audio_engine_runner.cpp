#include "audio_engine_runner.h"
#include "automatic_transmission.h"
#include "engine.h"
#include "ignition_module.h"
#include "simulator.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <ctime>
#if defined(__APPLE__)
#include <pthread/qos.h>
#include <mach/mach.h>
#include <mach/mach_time.h>
#include <mach/thread_policy.h>
#endif

namespace {
double threadCpuSeconds() {
#if defined(CLOCK_THREAD_CPUTIME_ID)
    timespec value{};
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &value);
    return value.tv_sec + value.tv_nsec / 1e9;
#else
    return static_cast<double>(std::clock()) / CLOCKS_PER_SEC;
#endif
}
}

bool AudioEngineRunner::start(Simulator &simulator, double reserveSeconds, bool realtime) {
    stop();
    if (!std::isfinite(reserveSeconds) || reserveSeconds < 0.02 || reserveSeconds > 0.1
        || !simulator.getEngine() || simulator.synthesizer().m_thread) return false;
    m_simulator = &simulator;
    m_reserveSeconds = reserveSeconds;
    m_realtime = realtime;
    m_schedulingStatus = -1;
    m_commands.initialize(64);
    m_visuals.initialize(8);
    simulator.setSynthesizerLatencyCorrectionEnabled(false);
    simulator.setMaximumSynthesizerInputLatency(-1);
    // The extra block allows a whole physics block to become PCM immediately.
    simulator.synthesizer().setOutputLeadSamples(
        static_cast<int>(std::ceil((reserveSeconds + 0.005) * 44100)));
    m_ready = false;
    m_run = true;
    m_rpm = 0; m_simulatedSeconds = 0; m_queuedMs = 0; m_maxBlockMs = 0; m_blocks = 0;
    m_cpuSeconds = 0; m_maxCpuBlockMs = 0; m_maxWakeupOverrunMs = 0;
    m_throttle = 0; m_volume = simulator.synthesizer().getAudioParameters().volume;
    m_exhaustMix = simulator.synthesizer().getAudioParameters().convolution;
    m_roughness = simulator.synthesizer().getAudioParameters().inputSampleNoise;
    m_ignition = false; m_cranking = false; m_blipping = false;
    m_drive = false; m_shifting = false; m_gear = -1;
    m_vehicleSpeed = 0; m_clutch = 0; m_brake = 0; m_appliedThrottle = 0;
    m_thread = std::thread(&AudioEngineRunner::run, this);
    const auto deadline = SDL_GetTicks() + 10000;
    while (!m_ready && SDL_GetTicks() < deadline) SDL_Delay(1);
    if (!m_ready) { stop(); return false; }
    return true;
}

void AudioEngineRunner::stop() {
    m_run = false;
    if (m_thread.joinable()) m_thread.join();
    m_simulator = nullptr;
}

AudioEngineRunner::Snapshot AudioEngineRunner::snapshot() const {
    return {m_rpm.load(), m_simulatedSeconds.load(), m_queuedMs.load(),
        m_maxBlockMs.load(), m_cpuSeconds.load(), m_maxCpuBlockMs.load(),
        m_maxWakeupOverrunMs.load(), m_blocks.load(), m_schedulingStatus.load(),
        m_throttle.load(), m_volume.load(), m_exhaustMix.load(), m_roughness.load(),
        m_ignition.load(), m_cranking.load(), m_blipping.load(),
        m_vehicleSpeed.load(), m_clutch.load(), m_brake.load(), m_appliedThrottle.load(),
        m_gear.load(), m_drive.load(), m_shifting.load()};
}

void AudioEngineRunner::run() {
#if defined(__APPLE__)
    pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
    if (m_realtime) {
        // Only this dedicated physics/DSP producer gets a deadline. It never
        // runs UI, terminal I/O, or script compilation, and sleeps when full.
        mach_timebase_info_data_t timebase{};
        mach_timebase_info(&timebase);
        const auto ticks = [&](double ms) {
            return static_cast<uint32_t>(ms * 1e6 * timebase.denom / timebase.numer);
        };
        thread_time_constraint_policy_data_t policy{ticks(5), ticks(3), ticks(5), true};
        const auto thread = mach_thread_self();
        m_schedulingStatus = thread_policy_set(thread, THREAD_TIME_CONSTRAINT_POLICY,
            reinterpret_cast<thread_policy_t>(&policy), THREAD_TIME_CONSTRAINT_POLICY_COUNT);
        mach_port_deallocate(mach_task_self(), thread);
    }
#else
    if (m_realtime) m_schedulingStatus = SDL_SetCurrentThreadPriority(SDL_THREAD_PRIORITY_TIME_CRITICAL) ? 0 : 1;
    else SDL_SetCurrentThreadPriority(SDL_THREAD_PRIORITY_HIGH);
#endif
    auto &sim = *m_simulator;
    auto &synth = sim.synthesizer();
    auto &engine = *sim.getEngine();
    auto &transmission = *sim.getTransmission();
    auto &vehicle = *sim.getVehicle();
    AutomaticTransmission automatic;
    double brake = 0;
    double starterSeconds = 0, simulatedSeconds = 0, starterThrottle = 0;
    double throttle = 0, blipSeconds = 0, blipThrottle = 0;
    const double initialCpu = threadCpuSeconds();
    auto audio = synth.getAudioParameters();
    double targetExhaustMix = audio.convolution, targetRoughness = audio.inputSampleNoise;
    while (m_run) {
        Command command;
        // Limit control processing per pass so an input flood cannot starve
        // physics. All timed actions advance on engine time, never UI time.
        for (int commands = 0; commands < 16 && m_commands.pop(command); ++commands) {
            if (!std::isfinite(command.value)) continue;
            switch (command.action) {
            case Action::Start:
                engine.getIgnitionModule()->m_enabled = true;
                starterSeconds = 2.0;
                starterThrottle = std::clamp(command.value, 0.0, 1.0);
                break;
            case Action::Stop:
                engine.getIgnitionModule()->m_enabled = false;
                starterSeconds = 0;
                throttle = 0; blipSeconds = 0;
                break;
            case Action::Throttle:
                throttle = std::clamp(command.value, 0.0, 1.0);
                blipSeconds = 0;
                break;
            case Action::Volume:
                audio.volume = std::clamp(command.value, 0.0, 1.0);
                synth.setAudioParameters(audio);
                break;
            case Action::ExhaustMix:
                targetExhaustMix = std::clamp(command.value, 0.0, 1.0);
                break;
            case Action::Roughness:
                targetRoughness = std::clamp(command.value, 0.0, 1.0);
                break;
            case Action::HighFrequency:
                audio.dF_F_mix=std::clamp(command.value,0.0,.01);synth.setAudioParameters(audio);break;
            case Action::LowNoise:
                audio.airNoise=std::clamp(command.value,0.0,1.0);synth.setAudioParameters(audio);break;
            case Action::Drive:
                automatic.setEnabled(command.value > 0, transmission);
                if (automatic.enabled()) sim.m_dyno.m_enabled = false;
                break;
            case Action::Brake: brake = std::clamp(command.value, 0.0, 1.0);break;
            case Action::Dyno:
                if (command.value > 0 && automatic.enabled()) automatic.setEnabled(false, transmission);
                sim.m_dyno.m_enabled=command.value>0;break;
            case Action::DynoSpeed: sim.m_dyno.m_rotationSpeed=units::rpm(std::clamp(command.value,500.0,20000.0));break;
            case Action::Clutch:
                if (automatic.enabled()) automatic.setEnabled(false, transmission);
                transmission.setClutchPressure(std::clamp(command.value,0.0,1.0));break;
            case Action::Gear: {
                const int next = transmission.getGear() + int(command.value);
                if (automatic.enabled()) automatic.setEnabled(false, transmission);
                transmission.changeGear(next);break;
            }
            case Action::Blip:
                if (engine.getIgnitionModule()->m_enabled) {
                    blipThrottle = std::clamp(command.value, 0.0, 1.0);
                    blipSeconds = 0.6;
                }
                break;
            }
        }
        while (synth.pumpAudioRendering()) { }
        const double queued = sim.getSynthesizerInputLatency() + sim.getSynthesizerOutputLatency();
        m_queuedMs = queued * 1000;
        if (queued >= m_reserveSeconds) {
            m_ready = true;
            const auto sleepStart = SDL_GetTicksNS();
            SDL_Delay(1);
            m_maxWakeupOverrunMs = std::max(m_maxWakeupOverrunMs.load(),
                (SDL_GetTicksNS() - sleepStart) / 1e6 - 1);
            continue;
        }
        const auto blockStart = SDL_GetTicksNS();
        const double cpuStart = threadCpuSeconds();
        // Morph the sound character on engine time. An immediate wet/dry jump
        // can outrun the leveler's attack and clip even at moderate volume.
        const auto approach = [](float current, double target) {
            return static_cast<float>(current + std::clamp(target - current, -0.025, 0.025));
        };
        const float exhaustMix = approach(audio.convolution, targetExhaustMix);
        const float roughness = approach(audio.inputSampleNoise, targetRoughness);
        if (exhaustMix != audio.convolution || roughness != audio.inputSampleNoise) {
            audio.convolution = exhaustMix;
            audio.inputSampleNoise = roughness;
            synth.setAudioParameters(audio);
        }
        sim.m_starterMotor.m_enabled = starterSeconds > 0;
        vehicle.setBrake(brake);
        const double requested = std::max(brake > 0 ? 0 : throttle,
            std::max(starterSeconds > 0 ? starterThrottle : 0, brake == 0 && blipSeconds > 0 ? blipThrottle : 0));
        const double applied = automatic.update(transmission, engine, vehicle, requested, starterSeconds > 0, 0.005);
        engine.setSpeedControl(applied);
        sim.startFrame(0.005);
        while (sim.simulateStep()) { }
        sim.endFrame();
        const double simulated = sim.simulationSteps() * sim.getTimestep();
        starterSeconds -= simulated;
        blipSeconds -= simulated;
        simulatedSeconds += simulated;
        // Finish the filters here, before sleeping. There is no intermediate
        // reservoir that needs a second thread to wake up and render it.
        while (synth.pumpAudioRendering()) { }
        ++m_blocks;
        if(m_visualTelemetry)publishVisuals(simulatedSeconds);
        const double blockMs = (SDL_GetTicksNS() - blockStart) / 1e6;
        m_maxBlockMs = std::max(m_maxBlockMs.load(), blockMs);
        m_maxCpuBlockMs = std::max(m_maxCpuBlockMs.load(), (threadCpuSeconds() - cpuStart) * 1000);
        m_cpuSeconds = threadCpuSeconds() - initialCpu;
        m_rpm = engine.getRpm();
        m_throttle = throttle; m_volume = audio.volume;
        m_exhaustMix = audio.convolution; m_roughness = audio.inputSampleNoise;
        m_ignition = engine.getIgnitionModule()->m_enabled;
        m_cranking = starterSeconds > 0; m_blipping = blipSeconds > 0;
        m_vehicleSpeed = vehicle.getSpeed(); m_clutch = transmission.getClutchPressure();
        m_brake = brake; m_appliedThrottle = applied; m_gear = transmission.getGear();
        m_drive = automatic.enabled(); m_shifting = automatic.shifting();
        m_simulatedSeconds = simulatedSeconds;
    }
}

void AudioEngineRunner::publishVisuals(double simulatedSeconds) {
    auto &sim=*m_simulator;auto &engine=*sim.getEngine();
    EngineVisualSnapshot frame;
    frame.block=m_blocks;frame.simulatedSeconds=simulatedSeconds;
    auto pose=[](const atg_scs::RigidBody &body) { return VisualPose{float(body.p_x),float(body.p_y),float(body.theta)}; };
    for(int i=0;i<std::min(engine.getCylinderCount(),VisualMaxCylinders);++i) {
        auto &c=frame.cylinders[i];const auto *piston=engine.getPiston(i);
        auto *chamber=engine.getChamber(i);auto *head=chamber->getCylinderHead();
        c.piston=pose(piston->m_body);c.rod=pose(piston->getRod()->m_body);
        c.pressure=chamber->m_system.pressure();c.temperature=chamber->m_system.temperature();c.volume=chamber->getVolume();
        c.intakeLift=head->intakeValveLift(piston->getCylinderIndex());
        c.exhaustLift=head->exhaustValveLift(piston->getCylinderIndex());
        c.intakeCamAngle=head->getIntakeCamshaft()->getAngle()+head->getIntakeCamshaft()->getLobeCenterline(piston->getCylinderIndex());
        c.exhaustCamAngle=head->getExhaustCamshaft()->getAngle()+head->getExhaustCamshaft()->getLobeCenterline(piston->getCylinderIndex());
        c.lit=chamber->isLit();c.fired=chamber->popLitLastFrame();
        c.flameX=chamber->m_flameEvent.travel_x;c.flameY=chamber->m_flameEvent.travel_y;
    }
    for(int i=0;i<std::min(engine.getCrankshaftCount(),VisualMaxCranks);++i)frame.cranks[i]=pose(engine.getCrankshaft(i)->m_body);
    frame.manifoldPressure=engine.getManifoldPressure();frame.intakeFlow=engine.getIntakeFlowRate();
    frame.afr=engine.getIntakeAfr();frame.exhaustO2=engine.getExhaustO2();frame.exhaustFlow=sim.getTotalExhaustFlow();
    frame.fuelLiters=engine.getTotalVolumeFuelConsumed()/units::L;
    frame.vehicleSpeed=sim.getVehicle()->getSpeed();frame.torque=sim.getFilteredDynoTorque();frame.power=frame.torque*engine.getSpeed();
    frame.vehicleDistance=sim.getVehicle()->getTravelledDistance();
    frame.throttleAngle=engine.getThrottlePlateAngle();frame.physicsHz=sim.getSimulationFrequency();
    const auto audio=sim.synthesizer().getAudioParameters();
    frame.highFrequency=audio.dF_F_mix;frame.lowNoise=audio.airNoise;frame.levelerGain=sim.synthesizer().getLevelerGain();
    frame.clutch=sim.getTransmission()->getClutchPressure();frame.gear=sim.getTransmission()->getGear();
    frame.dyno=sim.m_dyno.m_enabled;frame.dynoRpm=sim.m_dyno.m_rotationSpeed/units::rpm(1);
    // A full visual queue is disposable. It never backpressures PCM production.
    m_visuals.push(frame);
}
