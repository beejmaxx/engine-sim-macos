#ifndef ENGINE_SIM_DRIVING_HANDLING_H
#define ENGINE_SIM_DRIVING_HANDLING_H

// Arcade chassis: input directly requests a bounded turn rate, with fast
// centring and no persistent tyre slide. The engine still owns forward speed.
// Runs on the game worker, never the audio thread.
class DrivingHandling {
public:
    enum class Input { Analog, Keyboard };
    // Fraction of each axle's tyre contact patches on grass (0 = tarmac).
    struct Surface {double front=0,rear=0;};
    struct State {double input=0,steer=0,yawRate=0,lateralSpeed=0,lateralG=0,driftAngle=0;};
    static constexpr double Wheelbase=2.45, FrontAxle=1.45, RearAxle=1.0;
    static constexpr double Gravity=9.81, RoadControl=1.0, DirtControl=.85;
    static double maximumCurvature(double speed);
    static double cornerSpeed(double curvature);
    void reset() {state={};}
    void advance(double dt,double speed,double steering,Input input,Surface surface,bool drift=false);
    void impact(double speed,double sideslip);
    const State &snapshot() const {return state;}
private:
    void step(double dt,double speed,double steering,Input input,Surface surface,bool drift);
    State state{};
};
#endif
