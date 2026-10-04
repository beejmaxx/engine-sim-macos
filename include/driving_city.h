#ifndef ENGINE_SIM_DRIVING_CITY_H
#define ENGINE_SIM_DRIVING_CITY_H
#include "driving_geometry.h"
#include "driving_handling.h"
#include <array>
#include <algorithm>
#include <vector>
#include <cstdint>

// One immutable map shared by collisions, rendering, recovery and the minimap.
// Construction happens outside the engine/audio threads. Units are metres.
class DrivingCity {
public:
    static constexpr int GridRadius=9;
    static constexpr double Block=160,Extent=1560;
    enum class Zone { Downtown, OldTown, Garden, University, Northside, Harbor, Industrial, Beach };
    enum class Style { Masonry, Glass, House, Warehouse, Container, Civic, Stadium };
    enum class Lot { Built, Plaza, Park, Stadium, Fuel, Harbor };
    struct Building {DrivingPoint lo,hi;double height;uint32_t color;bool glass;Style style=Style::Masonry;};
    struct Contact {DrivingPoint normal{};bool hit=false;};
    struct Spawn {DrivingPoint point;double yaw;};
    DrivingCity();
    static double roadHalfWidth(int street) {return std::abs(street)==6 || std::abs(street)==GridRadius ? 22 : street%3==0 ? 18 : 12;}
    static bool plaza(int x,int z) {return (x==-1 && z==-1)||(x==1 && z==1);}
    static Lot lot(int x,int z);
    static Zone zone(DrivingPoint p);
    static Spawn start() {return {{5,-370},0};}
    static const char *district(DrivingPoint p);
    double roadDistance(DrivingPoint p) const;
    DrivingHandling::Surface tyreSurface(DrivingPoint p,double yaw) const;
    Spawn nearestStreet(DrivingPoint p,double yaw) const;
    bool occupied(DrivingPoint p,double radius=0) const;
    Contact constrain(DrivingPoint &p,double yaw) const;
    const std::vector<Building> &buildings() const {return blocks;}
    static constexpr std::array<DrivingPoint,12> Destinations{{
        {5,-160},{165,155},{485,485},{965,485},{1435,5},{965,-965},
        {5,-1285},{-795,-165},{-1285,-485},{-965,805},{-475,325},{5,-370}
    }};
    static constexpr std::array<const char *,12> DestinationNames{{
        "MARKET STREET", "CIVIC SQUARE", "FINANCIAL DISTRICT", "EAST QUAYS",
        "HARBOR FRONT", "SOUTH EXPRESSWAY", "WAREHOUSE ROW", "PORTSIDE STADIUM",
        "OCEAN DRIVE", "UNIVERSITY", "GARDEN DISTRICT", "OLD TOWN GARAGE"
    }};
    // Fixed viewpoints for repeatable, offscreen visual/performance checks.
    static constexpr std::array<Spawn,10> GalleryViews{{
        {{5,-370},0},{{5,-50},0},{{325,165},0},{{1125,-220},0},
        {{1435,270},0},{{-320,80},0},{{-1445,-560},0},{{-965,650},0},
        {{-740,-165},0},{{965,-850},0}
    }};
private:
    static constexpr int Cells=2*GridRadius+2;
    static int cell(double v) {return std::clamp(int(std::floor(v/Block))+GridRadius+1,0,Cells-1);}
    std::vector<Building> blocks;
    std::array<std::vector<unsigned>,Cells*Cells> spatial;
};
const DrivingCity &drivingCity();
#endif
