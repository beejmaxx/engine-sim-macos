#include "automatic_transmission.h"
#include "engine.h"
#include "ignition_module.h"
#include "transmission.h"
#include "vehicle.h"
#include "units.h"
#include <algorithm>
#include <cmath>

void AutomaticTransmission::setEnabled(bool enabled, Transmission &transmission) {
    m_enabled = enabled && transmission.getGearCount() > 0;
    m_clutch = 0;
    m_shiftElapsed = -1;
    m_cooldown = 0.5;
    m_changedGear = false;
    m_directionHold = 0;
    transmission.setClutchPressure(0);
    transmission.changeGear(m_enabled ? 0 : -1);
}

double AutomaticTransmission::update(Transmission &transmission, const Engine &engine,
    Vehicle &vehicle, double throttle, bool cranking, double dt, bool backPedal) {
    if (!m_enabled) {
        if(backPedal)vehicle.setBrake(1);
        return vehicle.getBrake()>0 ? 0 : throttle;
    }
    if (!std::isfinite(dt) || dt <= 0) return throttle;
    dt = std::min(dt, 0.05);
    if(transmission.getGear()==Transmission::Neutral)
        transmission.changeGear(vehicle.getTravelDirection()<0 ? Transmission::Reverse : 0);
    const bool explicitBrake=vehicle.getBrake()>.01;
    const bool forwardPedal=throttle>.01;
    bool reverse=transmission.getGear()==Transmission::Reverse;
    const bool wantsOpposite=reverse ? forwardPedal && !backPedal : backPedal && !forwardPedal;
    if(wantsOpposite) {
        vehicle.setBrake(1);
        if(vehicle.getSpeed()<.15 && !explicitBrake && !cranking && engine.getIgnitionModule()->m_enabled && engine.getRpm()>400) m_directionHold+=dt;
        else m_directionHold=0;
        if(m_directionHold>=.18) {
            transmission.setClutchPressure(0);
            transmission.changeGear(reverse ? 0 : Transmission::Reverse);
            reverse=transmission.getGear()==Transmission::Reverse;
            m_clutch=0;m_shiftElapsed=-1;m_cooldown=.5;m_directionHold=0;
            vehicle.setBrake(0);
        } else throttle=0;
    } else m_directionHold=0;
    if(reverse) {
        throttle=backPedal && !forwardPedal ? .6*std::clamp((7.0-vehicle.getSpeed())/1.5,0.0,1.0) : 0;
        // Backing out should stay manageable even on a powerful engine.
        vehicle.setBrake(std::max(vehicle.getBrake(),std::clamp((vehicle.getSpeed()-7.0)*.2,0.0,.5)));
    }
    if(backPedal && forwardPedal)vehicle.setBrake(1);
    const bool braking=vehicle.getBrake()>.01;
    if(braking)throttle=0;
    const double rpm = std::max(0.0, engine.getRpm());
    const double limit = std::max(1000.0, std::min(engine.getRedline(),
        engine.getIgnitionModule()->getRevLimit()) / units::rpm(1));
    const double wheelRpm = vehicle.getSpeed() / vehicle.getTireRadius()
        * vehicle.getDiffRatio() / units::rpm(1);
    const double launchRpm = std::clamp(limit * 0.25, 1400.0, 2800.0);
    m_cooldown = std::max(0.0, m_cooldown - dt);
    if (!shifting() && vehicle.getSpeed() < 0.5 && transmission.getGear() > 0) {
        transmission.changeGear(0);
        m_clutch = 0;
        m_cooldown = 0.5;
    }

    if (!engine.getIgnitionModule()->m_enabled || cranking || rpm < 400) {
        m_clutch = 0;
        m_shiftElapsed = -1;
        transmission.setClutchPressure(0);
        return throttle;
    }

    if (shifting()) {
        m_shiftElapsed += dt;
        // Cut engine torque, open the clutch, change ratio, then smoothly
        // reconnect the engine to the moving car. RPM is never fabricated.
        if (m_shiftElapsed >= 0.06 && !m_changedGear) {
            transmission.changeGear(m_nextGear);
            m_changedGear = true;
        }
        double engagement = std::clamp((m_shiftElapsed - 0.09) / 0.22, 0.0, 1.0);
        const double coupledRpm=wheelRpm*std::abs(transmission.getGearRatio(transmission.getGear()));
        // Braking can pass through the idle-speed boundary during a shift.
        // Do not reconnect an idling engine to wheels almost at a standstill.
        if(rpm<700 || (throttle<.01 && coupledRpm<launchRpm*.9))engagement=0;
        m_clutch = engagement;
        transmission.setClutchPressure(m_clutch);
        const double torqueRecovery = std::clamp((m_shiftElapsed - 0.12) / 0.19, 0.0, 1.0);
        if (m_shiftElapsed >= 0.31) {
            m_shiftElapsed = -1;
            m_cooldown = 0.8;
        }
        return throttle * torqueRecovery;
    }

    const int gear = transmission.getGear();
    const double coupledRpm = wheelRpm * std::abs(transmission.getGearRatio(gear));
    const double upshiftRpm = limit * (braking ? .98 : 0.48 + 0.44 * throttle);
    const double downshiftRpm = limit * (braking ? .55 : throttle > 0.75 ? 0.43 : 0.24);
    int next = gear;
    if (gear>=0 && m_cooldown == 0 && coupledRpm > 900) {
        if (!braking && rpm >= upshiftRpm && gear + 1 < transmission.getGearCount()) next = gear + 1;
        else if (gear > 0 && (braking ? coupledRpm : rpm) < downshiftRpm &&
            wheelRpm * transmission.getGearRatio(gear - 1) < upshiftRpm * 0.85) next = gear - 1;
    }
    if (next != gear) {
        m_nextGear = next;
        m_shiftElapsed = 0;
        m_changedGear = false;
        m_clutch = 0;
        transmission.setClutchPressure(0);
        return 0;
    }

    if (rpm < 700) m_clutch = 0;
    else if (coupledRpm > launchRpm * 0.9) m_clutch = std::min(1.0, m_clutch + dt * 3);
    else if (throttle < 0.01 || vehicle.getBrake() > 0) m_clutch = 0;
    else {
        // Slip the clutch during launch, regulating engine speed instead of
        // abruptly coupling an idling engine to the full stationary mass.
        m_clutch = std::clamp(m_clutch + (rpm - launchRpm) / launchRpm * dt * 2, 0.0, 1.0);
    }
    transmission.setClutchPressure(m_clutch);
    return throttle;
}
