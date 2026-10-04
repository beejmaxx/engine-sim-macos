#ifndef ENGINE_SIM_DRIVING_GEOMETRY_H
#define ENGINE_SIM_DRIVING_GEOMETRY_H
#include <cmath>

struct DrivingPoint {
    double x=0,z=0;
    DrivingPoint operator+(DrivingPoint p) const {return {x+p.x,z+p.z};}
    DrivingPoint operator-(DrivingPoint p) const {return {x-p.x,z-p.z};}
    DrivingPoint operator*(double s) const {return {x*s,z*s};}
};
inline double drivingDot(DrivingPoint a,DrivingPoint b) {return a.x*b.x+a.z*b.z;}
inline double drivingLength(DrivingPoint p) {return std::hypot(p.x,p.z);}
inline double drivingAngle(double a) {return std::remainder(a,6.283185307179586);}
enum class DrivingWorld { Circuit, City };
#endif
