#ifndef ENGINE_SIM_AUTOMATIC_TRANSMISSION_H
#define ENGINE_SIM_AUTOMATIC_TRANSMISSION_H

class Engine;
class Transmission;
class Vehicle;

// Automatic control of the existing physical clutch and gearbox. Advance only
// on simulation time. No device, UI, allocation, or thread synchronization.
class AutomaticTransmission {
public:
    void setEnabled(bool enabled, Transmission &transmission);
    bool enabled() const { return m_enabled; }
    bool shifting() const { return m_shiftElapsed >= 0; }
    double update(Transmission &transmission, const Engine &engine,
        const Vehicle &vehicle, double throttle, bool cranking, double dt);

private:
    bool m_enabled = false, m_changedGear = false;
    int m_nextGear = 0;
    double m_clutch = 0, m_shiftElapsed = -1, m_cooldown = 0;
};

#endif
