#ifndef ATG_ENGINE_SIM_VEHICLE_H
#define ATG_ENGINE_SIM_VEHICLE_H

#include "scs.h"

class Vehicle {
    public:
        // Deliberately strong arcade brakes. Force still opposes the actual
        // vehicle motion, so speed, gearbox load and engine sound stay linked.
        static constexpr double MaximumBrakeDeceleration = 9.81 * 2.6;
        struct Parameters {
            double mass;
            double dragCoefficient;
            double crossSectionArea;
            double diffRatio;
            double tireRadius;
            double rollingResistance;
        };

    public:
        Vehicle();
        ~Vehicle();

        void initialize(const Parameters &params);
        void update(double dt);
        void addToSystem(atg_scs::RigidBodySystem *system, atg_scs::RigidBody *rotatingMass);
        inline double getMass() const { return m_mass; }
        inline double getRollingResistance() const { return m_rollingResistance; }
        inline double getDragCoefficient() const { return m_dragCoefficient; }
        inline double getCrossSectionArea() const { return m_crossSectionArea; }
        inline double getDiffRatio() const { return m_diffRatio; }
        inline double getTireRadius() const { return m_tireRadius; }
        double getSpeed() const;
        double getSignedSpeed() const { return m_travelDirection * getSpeed(); }
        int getTravelDirection() const { return m_travelDirection; }
        void setTravelDirection(int direction) { m_travelDirection = direction < 0 ? -1 : 1; }
        inline double getTravelledDistance() const { return m_travelledDistance; }
        double getSignedTravelledDistance() const { return m_signedTravelledDistance; }
        inline void resetTravelledDistance() { m_travelledDistance = m_signedTravelledDistance = 0; }
        double linearForceToVirtualTorque(double force) const;
        void setBrake(double pressure);
        double getBrake() const { return m_brake; }
        void setRoadDeceleration(double value);
        double getRoadDeceleration() const { return m_roadDeceleration; }

    protected:
        atg_scs::RigidBody *m_rotatingMass;

        double m_mass;
        double m_dragCoefficient;
        double m_crossSectionArea;
        double m_diffRatio;
        double m_tireRadius;
        double m_travelledDistance;
        double m_signedTravelledDistance = 0;
        int m_travelDirection = 1;
        double m_rollingResistance;
        double m_brake = 0;
        double m_roadDeceleration = 0;
};

#endif /* ATG_ENGINE_SIM_VEHICLE_H */
