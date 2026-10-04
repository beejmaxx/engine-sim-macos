#ifndef ENGINE_SIM_DRIVING_CITY_H
#define ENGINE_SIM_DRIVING_CITY_H
#include "driving_geometry.h"
#include "driving_handling.h"
#include <array>
#include <vector>
#include <cstdint>

// One immutable map shared by collisions, rendering, recovery and the minimap.
// Construction happens outside the engine/audio threads. Units are metres.
class DrivingCity {
public:
    static constexpr int GridRadius=3;
    static constexpr double Block=160,Extent=550;
    struct Building {DrivingPoint lo,hi;double height;uint32_t color;bool glass;};
    struct Contact {DrivingPoint normal{};bool hit=false;};
    struct Spawn {DrivingPoint point;double yaw;};
    DrivingCity();
    static double roadHalfWidth(int street) {return street==0 ? 18 : 12;}
    static bool plaza(int x,int z) {return (x==-1 && z==-1)||(x==1 && z==1);}
    static Spawn start() {return {{5,-370},0};}
    static const char *district(DrivingPoint p);
    double roadDistance(DrivingPoint p) const;
    DrivingHandling::Surface tyreSurface(DrivingPoint p,double yaw) const;
    Spawn nearestStreet(DrivingPoint p,double yaw) const;
    bool occupied(DrivingPoint p,double radius=0) const;
    Contact constrain(DrivingPoint &p,double yaw) const;
    const std::vector<Building> &buildings() const {return blocks;}
    static constexpr std::array<DrivingPoint,8> Destinations{{
        {5,-160},{165,-5},{320,165},{5,320},{-165,160},{-320,-5},{-160,-325},{5,-370}
    }};
private:
    std::vector<Building> blocks;
};
const DrivingCity &drivingCity();
#endif
