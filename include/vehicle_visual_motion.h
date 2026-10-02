#ifndef ENGINE_SIM_VEHICLE_VISUAL_MOTION_H
#define ENGINE_SIM_VEHICLE_VISUAL_MOTION_H
#include <algorithm>
#include <array>
#include <cmath>

// Present a short, interpolated history on the display clock. Physics runs in
// 5 ms audio blocks; displaying only the newest block makes uniform motion
// alternate between three and four steps per 60 Hz frame.
class VehicleVisualMotion {
public:
    struct Sample { double time=0,distance=0,speed=0; };
    void reset() { count=0;clock=-1; }
    void accept(double time,double distance,double speed) {
        if(!std::isfinite(time) || !std::isfinite(distance) || !std::isfinite(speed))return;
        if(count && time<=history[count-1].time)reset();
        if(count==history.size()) { std::move(history.begin()+1,history.end(),history.begin());--count; }
        history[count++]={time,distance,speed};
    }
    Sample advance(double dt) {
        if(!count)return {};
        const auto &latest=history[count-1];
        if(clock<0 || clock<history[0].time || latest.time-clock>.15)clock=std::max(history[0].time,latest.time-.025);
        else clock=std::min(latest.time,clock+std::clamp(dt,0.0,.1));
        for(unsigned i=1;i<count;++i)if(history[i].time>=clock) {
            const auto &a=history[i-1],&b=history[i];const double f=std::clamp((clock-a.time)/(b.time-a.time),0.0,1.0);
            return {clock,a.distance+(b.distance-a.distance)*f,a.speed+(b.speed-a.speed)*f};
        }
        return latest;
    }
private:
    std::array<Sample,32> history{};
    unsigned count=0;
    double clock=-1;
};
#endif
