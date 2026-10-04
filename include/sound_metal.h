#ifndef ENGINE_SIM_SOUND_METAL_H
#define ENGINE_SIM_SOUND_METAL_H
#include "audio_engine_runner.h"
#include "driving_game.h"
#include <array>
#include <cstdint>
#include <memory>

namespace sound_ui {
constexpr float Width = 1280, Height = 800;
enum Control { None = -1, Supra, Ls, Start, Rev, Idle, Throttle, Volume, Exhaust,
    Roughness, Mute, Reset, Effects, Uncapped, Library, HighFrequency, LowNoise, Dyno, DynoSpeed, Clutch, GearDown, GearUp, LayerBack, LayerNext, Drive, Brake, RoadView, RecoverCar, RestartRace, Drift, WorldView, ControlCount };
struct UiRect {
    float x, y, w, h;
    bool contains(float px, float py) const { return px >= x && py >= y && px < x+w && py < y+h; }
};
// Shared by the GPU draw list, hit testing and programmatic tests.
inline UiRect bounds(Control control,bool roadView=false) {
    if(roadView) {
        switch(control) {
        case Library:return {992,24,136,32};
        case RoadView:return {1140,24,116,32};
        case WorldView:return {824,24,156,32};
        case Drive:return {24,691,132,32};
        case Start:return {24,741,116,35};
        case Rev:return {150,741,90,35};
        case Brake:return {250,741,100,35};
        case Drift:return {360,741,140,35};
        case Mute:return {510,741,84,35};
        case Throttle:return {24,651,340,25};
        case RecoverCar:return {604,741,108,35};
        case RestartRace:return {722,741,150,35};
        default:return {};
        }
    }
    switch (control) {
    case Supra: return {966, 741, 80, 27};
    case Ls: return {1054, 741, 65, 27};
    case Start: return {10, 741, 105, 27};
    case Rev: return {123, 741, 82, 27};
    case Idle: return {213, 741, 82, 27};
    case Throttle: return {874, 525, 182, 25};
    case Volume: return {0, 160, 66.66f, 90};
    case Exhaust: return {66.66f, 160, 66.67f, 90};
    case HighFrequency: return {133.33f, 160, 66.67f, 90};
    case LowNoise: return {200, 160, 66.67f, 90};
    case Roughness: return {266.67f, 160, 66.66f, 90};
    case Mute: return {303, 741, 82, 27};
    case Reset: return {393, 741, 110, 27};
    case Effects: return {511, 741, 82, 27};
    case Uncapped: return {601, 741, 120, 27};
    case Library: return {729, 741, 229, 27};
    case Dyno: return {8, 579, 114, 25};
    case DynoSpeed: return {0, 608, 133.33f, 120};
    case Clutch: return {133.33f, 488, 133.34f, 120};
    case GearDown: return {272, 568, 26, 26};
    case GearUp: return {370, 568, 26, 26};
    case Brake: return {302, 568, 64, 26};
    case Drive: return {1127, 741, 143, 27};
    case RoadView: return {581, 9, 113, 24};
    case LayerBack: return {786, 9, 26, 24};
    case LayerNext: return {821, 9, 26, 24};
    default: return {};
    }
}
inline bool isSlider(Control c) { return (c >= Throttle && c <= Roughness) || c==HighFrequency || c==LowNoise || c==DynoSpeed || c==Clutch; }
inline Control hit(float x, float y,bool roadView=false) {
    for (int i = 0; i < ControlCount; ++i) if (bounds(Control(i),roadView).contains(x,y)) return Control(i);
    return None;
}
struct State {
    AudioEngineRunner::Snapshot engine{};
    std::array<float, 512> waveform{};
    std::array<char, 160> output{}, notice{};
    std::array<char, 80> title{};
    int engineCount = 0;
    uint32_t displayId = 0;
    float throttle = 0, volume = .35, exhaust = 1, roughness = .23, redline = 6000;
    std::uint64_t missing = 0, writeErrors = 0;
    int preset = 0, layer = 0;
    enum class CarBody { Concept, PorscheGt3 };
    CarBody carBody=CarBody::Concept;
    float highFrequency=0, lowNoise=0, dynoRpm=1000, clutch=0;
    bool dyno=false, revHeld=false, drive=false, brakeHeld=false, roadView=false,driftHeld=false;
    float steering=0;
    DrivingWorld world=DrivingWorld::City;
    bool testPilot=false,testKeyboard=false;
    Control hover = None, focus = None, pressed = None;
    bool ready = false, loading = true, ignitionRequested = false, muted = false;
    bool effects = true, uncapped = false, silent = false, active = true, automated = false;
};
struct Metrics {
    std::uint64_t frames = 0, cpuNs = 0, gpuNs = 0, errors = 0;
    std::uint64_t audioBlocksSeen = 0, visualBlocksSeen = 0, animatedFrames = 0, peakVertices = 0;
    std::uint64_t roadFrames = 0, movingRoadFrames = 0;
    double roadDistance = 0;
    DrivingSnapshot game{};
    double gameCpuMs=0,gameTargetSpeed=0;
    double sunX=0,sunY=0;
    bool sunVisible=false;
    std::array<std::uint64_t, 256> cpuHistogram{}, gpuHistogram{};
};
}

#ifdef __OBJC__
@class CAMetalLayer;
class SoundSession;
class SoundMetalRenderer {
public:
    SoundMetalRenderer();
    ~SoundMetalRenderer();
    bool start(CAMetalLayer *layer, bool offscreen, const char *assets);
    void stop();
    void update(const sound_ui::State &state);
    void connectSession(std::shared_ptr<SoundSession> session);
    void recoverCar();
    void restartRace();
    void stallRendering(unsigned milliseconds);
    sound_ui::Metrics metrics() const;
    void capture(const char *path);
    unsigned captures() const;
    const char *error() const;
private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
#endif
#endif
