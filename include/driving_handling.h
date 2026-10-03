#ifndef ENGINE_SIM_DRIVING_HANDLING_H
#define ENGINE_SIM_DRIVING_HANDLING_H

// The engine owns longitudinal speed. This small chassis owns lateral tyre
// forces and yaw inertia on the game worker, never the audio thread.
class DrivingHandling {
public:
    enum class Input { Analog, Keyboard };
    // Fraction of each axle's tyre contact patches on grass (0 = tarmac).
    struct Surface {double front=0,rear=0;};
    struct State {double input=0,steer=0,yawRate=0,lateralSpeed=0,lateralG=0;};
    static constexpr double Wheelbase=2.45, FrontAxle=1.45, RearAxle=1.0;
    static constexpr double Gravity=9.81, RoadGrip=1.12, DirtGrip=.48;
    static double maximumCurvature(double speed);
    void reset() {state={};}
    void advance(double dt,double speed,double steering,Input input,Surface surface);
    void impact(double speed,double sideslip);
    const State &snapshot() const {return state;}
private:
    void step(double dt,double speed,double steering,Input input,Surface surface);
    State state{};
};
#endif
