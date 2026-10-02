#ifndef ENGINE_SIM_GAME_AUDIO_PROBE_H
#define ENGINE_SIM_GAME_AUDIO_PROBE_H
#include <SDL3/SDL.h>
#include <algorithm>
#include <atomic>
#include <cmath>

// Test-only observation of the software mixer. No output modification,
// allocation, I/O or locks in the callback; never attached during normal play.
struct GameAudioProbe {
    SDL_AudioDeviceID device=0;
    std::atomic<uint64_t> frames{0},silentBlocks{0},clippedSamples{0},invalidSamples{0},maxGapNs{0};
    uint64_t start=0,previous=0;
    static void SDLCALL mix(void *context,const SDL_AudioSpec *format,float *pcm,int bytes) {
        auto &p=*static_cast<GameAudioProbe *>(context);const auto now=SDL_GetTicksNS();
        const int samples=bytes/sizeof(float);float peak=0;uint64_t clipped=0,invalid=0;
        for(int i=0;i<samples;++i) {peak=std::max(peak,std::abs(pcm[i]));clipped+=std::abs(pcm[i])>=.9998f;invalid+=!std::isfinite(pcm[i]);}
        p.frames+=samples/std::max(1,format->channels);p.clippedSamples+=clipped;p.invalidSamples+=invalid;
        if(now-p.start>3000000000ULL && peak<1e-10f)++p.silentBlocks;
        if(p.previous)p.maxGapNs=std::max(p.maxGapNs.load(),now-p.previous);
        p.previous=now;
    }
    bool attach(SDL_AudioDeviceID id) {device=id;start=SDL_GetTicksNS();return SDL_SetAudioPostmixCallback(device,mix,this);}
    void detach() {if(device)SDL_SetAudioPostmixCallback(device,nullptr,nullptr);device=0;}
};
#endif
