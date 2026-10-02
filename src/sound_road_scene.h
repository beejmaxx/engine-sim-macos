#ifndef ENGINE_SIM_SOUND_ROAD_SCENE_H
#define ENGINE_SIM_SOUND_ROAD_SCENE_H

#include "sound_metal.h"
#include <simd/simd.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

// Renderer-owned procedural scenery. Positions are metres, +Z is forward.
// All motion comes from the published vehicle distance, never a second clock.
// The generic coupe is deliberately independent of engine/manufacturer assets.
class SoundRoadScene {
    using V3=simd_float3;
    struct Face { V3 a,b,c;simd_float4 tint;unsigned kind;std::array<simd_float2,3> uv{}; };
    struct ClipVertex { V3 p;simd_float2 uv; };
    std::vector<Face> faces;
    double textureDistance=0;
    const V3 camera{2.15f,2.4f,-7.6f};
    V3 forward,right,up;
    sound_ui::UiRect viewport{};
    static constexpr float Pi=3.14159265358979323846f;
    static simd_float4 rgb(unsigned c) {
        return {float((c>>16)&255)/255,float((c>>8)&255)/255,float(c&255)/255,1};
    }
    void triangle(V3 a,V3 b,V3 c,unsigned tint,bool shade=false,unsigned kind=3) {
        auto color=rgb(tint);
        if(shade) {
            auto normal=simd_normalize(simd_cross(b-a,c-a));
            const auto view=simd_normalize(camera-(a+b+c)/3);
            if(simd_dot(normal,view)<0)normal=-normal;
            const auto light=simd_normalize(V3{-2,4,-3});
            const float diffuse=.38f+.62f*std::max(0.f,simd_dot(normal,light));
            const float highlight=.42f*std::pow(std::max(0.f,simd_dot(normal,simd_normalize(light+view))),42.f);
            color=simd_min(simd_float4{1,1,1,1},color*diffuse+simd_float4{highlight,highlight*.94f,highlight*.85f,0});
            color.w=1;
        }
        faces.push_back({a,b,c,color,kind});
    }
    void quad(V3 a,V3 b,V3 c,V3 d,unsigned color,bool shade=false,unsigned kind=3) {
        triangle(a,b,c,color,shade,kind);triangle(a,c,d,color,shade,kind);
    }
    void box(V3 lo,V3 hi,unsigned color) {
        const V3 a{lo.x,lo.y,lo.z},b{hi.x,lo.y,lo.z},c{hi.x,hi.y,lo.z},d{lo.x,hi.y,lo.z};
        const V3 e{lo.x,lo.y,hi.z},f{hi.x,lo.y,hi.z},g{hi.x,hi.y,hi.z},h{lo.x,hi.y,hi.z};
        quad(a,b,c,d,color,true);quad(f,e,h,g,color,true);
        quad(e,a,d,h,color,true);quad(b,f,g,c,color,true);quad(d,c,g,h,color,true);
    }
    void ground(float x0,float x1,float z0,float z1,float y,unsigned color,unsigned kind=3) {
        quad({x0,y,z0},{x1,y,z0},{x1,y,z1},{x0,y,z1},color,false,kind);
    }
    template<class Emit> void paint(Emit emit) {
        for(const auto &face:faces) {
            // Clip at the camera before projection, then at the panel edges.
            std::array<V3,6> eye{},near{};int n=3,m=0;
            std::array<simd_float2,6> nearUv{};
            const std::array<V3,3> world{face.a,face.b,face.c};
            for(int i=0;i<3;++i) {
                const auto p=world[i]-camera;
                eye[i]={simd_dot(p,right),simd_dot(p,up),simd_dot(p,forward)};
            }
            for(int i=0;i<n;++i) {
                auto a=eye[i],b=eye[(i+1)%n];const bool ia=a.z>=.25f,ib=b.z>=.25f;
                if(ia) {near[m]=a;nearUv[m++]=face.uv[i];}
                if(ia!=ib) {
                    const float t=(.25f-a.z)/(b.z-a.z);near[m]=a+(b-a)*t;
                    nearUv[m++]=face.uv[i]+(face.uv[(i+1)%n]-face.uv[i])*t;
                }
            }
            if(m<3)continue;
            std::array<ClipVertex,12> polygon{},clipped{};n=m;
            for(int i=0;i<n;++i) {
                const auto world=camera+right*near[i].x+up*near[i].y+forward*near[i].z;
                polygon[i]={{viewport.x+viewport.w*.5f+near[i].x/near[i].z*viewport.h,
                    viewport.y+viewport.h*.48f-near[i].y/near[i].z*viewport.h,1-.25f/near[i].z},
                    face.kind==6 ? nearUv[i]/near[i].z : simd_float2{world.x/near[i].z,float(world.z+textureDistance)/near[i].z}};
            }
            for(int edge=0;edge<4 && n>=3;++edge) {
                auto inside=[&](V3 p) {switch(edge) {
                    case 0:return p.x-viewport.x;case 1:return viewport.x+viewport.w-p.x;
                    case 2:return p.y-viewport.y;default:return viewport.y+viewport.h-p.y;
                }};
                m=0;
                for(int i=0;i<n;++i) {
                    const auto a=polygon[i],b=polygon[(i+1)%n];const float da=inside(a.p),db=inside(b.p);
                    if(da>=0)clipped[m++]=a;
                    if((da>=0)!=(db>=0)) {
                        const float t=da/(da-db);clipped[m++]={a.p+(b.p-a.p)*t,a.uv+(b.uv-a.uv)*t};
                    }
                }
                polygon=clipped;n=m;
            }
            for(int i=1;i+1<n;++i)emit(polygon[0],polygon[i],polygon[i+1],face.tint,face.kind);
        }
        faces.clear();
    }
    void carShadow() {
        for(int ring=3;ring>=0;--ring)for(int i=0;i<40;++i) {
            const float a=i*Pi/20,b=(i+1)*Pi/20,w=1.05f+ring*.12f,h=2.05f+ring*.12f;
            triangle({1.75f,.016f,.2f},{1.75f+w*std::cos(a),.016f,.2f+h*std::sin(a)},
                {1.75f+w*std::cos(b),.016f,.2f+h*std::sin(b)},0x101D25);
            faces.back().tint.w=.12f;
        }
    }
    static float random(uint32_t value) {
        value^=value>>16;value*=0x7feb352d;value^=value>>15;value*=0x846ca68b;value^=value>>16;
        return (value&0xffffff)/float(0x1000000);
    }
    void grove(float x,float z,float height,float shade) {
        const V3 across=simd_normalize(V3{z-camera.z,0,camera.x-x})*(height*1.0524f/2);
        const V3 center{x,-.04f,z},rise{0,height,0};
        const auto a=center-across,b=center+across,c=b+rise,d=a+rise;
        const simd_float4 tint{shade,shade,shade,1};
        faces.push_back({a,b,c,tint,6,{{{0,1},{1,1},{1,0}}}});
        faces.push_back({a,c,d,tint,6,{{{0,1},{1,0},{0,0}}}});
    }

public:
    SoundRoadScene() {
        faces.reserve(12000);
        forward=simd_normalize(V3{1.75f,.85f,8.5f}-camera);
        right=simd_normalize(simd_cross(V3{0,1,0},forward));up=simd_cross(forward,right);
    }
    simd_float4 cameraPosition() const {return {camera.x,camera.y,camera.z,0};}
    simd_float4 cameraRight() const {return {right.x,right.y,right.z,0};}
    simd_float4 cameraUp() const {return {up.x,up.y,up.z,0};}
    simd_float4 cameraForward() const {return {forward.x,forward.y,forward.z,0};}
    template<class Emit> void draw(sound_ui::UiRect area,double distance,float tireRadius,bool braking,Emit emit) {
        (void)tireRadius;(void)braking;
        viewport=area;faces.clear();
        if(!std::isfinite(distance))distance=0;
        textureDistance=distance;
        // Distant mountains sit beyond the repeating roadside scenery.
        ground(-500,500,-15,600,-.05f,0x56634C,5);
        for(int layer=0;layer<3;++layer)for(int i=-60;i<60;++i) {
            const float z=240+layer*100,x=i*9.f;
            auto ridge=[&](float p) {return 17+layer*9+10*std::sin(p*.024f+layer)+5*std::sin(p*.061f+2*layer)+2*std::cos(p*.14f);};
            quad({x,-4,z},{x,ridge(x),z},{x+9,ridge(x+9),z},{x+9,-4,z},layer==0 ? 0x506674 : layer==1 ? 0x6B8090 : 0x8E9FAC);
        }
        paint(emit);
        ground(-4.9f,4.9f,-15,230,0,0x8E9687);
        ground(-3.5f,3.5f,-15,230,.002f,0x414A50,4);
        paint(emit);
        for(float side:{-1.f,1.f})ground(side*3.36f-.06f,side*3.36f+.06f,-15,230,.007f,0xD5D3BE);
        const float stripePhase=std::fmod(distance,12.0);
        for(int i=-2;i<20;++i)ground(-.065f,.065f,i*12.f-stripePhase,i*12.f+5-stripePhase,.008f,0xE4DCBC);
        // Quiet transverse road texture reinforces motion without motion blur.
        const float seamPhase=std::fmod(distance,30.0);
        for(int i=0;i<8;++i)ground(-3.3f,3.3f,i*30.f-seamPhase,i*30.f+.025f-seamPhase,.006f,0x48555B);
        paint(emit);
        const float postPhase=std::fmod(distance,16.0);
        for(int i=0;i<14;++i)for(float side:{-1.f,1.f}) {
            const float z=i*16.f-postPhase;
            box({side*4.15f-.045f,0,z-.05f},{side*4.15f+.045f,.78f,z+.05f},0xCAD2C6);
            box({side*4.15f-.05f,.57f,z-.06f},{side*4.15f+.05f,.7f,z+.06f},side>0 ? 0xEAD5A4 : 0xD57553);
        }
        // Persistent world cells: a tree keeps its identity, size and position
        // as the camera passes it. No modulo reset or screen-space scrolling.
        const auto cell=int64_t(std::floor(distance/24));
        for(int row=0;row<3;++row)for(int i=-1;i<15;++i)for(int side:{-1,1}) {
            const uint32_t seed=uint32_t(cell+i)*37+row*1367+(side+1)*7187;
            const float z=float((cell+i)*24.0-distance)+random(seed)*12;
            if(z<camera.z-2)continue;
            const float x=side*(9.5f+row*10+random(seed+1)*6);
            grove(x,z,9+random(seed+2)*5,.82f+random(seed+3)*.18f);
        }
        carShadow();paint(emit);
    }
};
#endif
