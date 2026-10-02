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
    struct Face { V3 a,b,c;simd_float4 tint; };
    std::vector<Face> faces;
    const V3 camera{4.1f,2.85f,-7.2f};
    V3 forward,right,up;
    sound_ui::UiRect viewport{};
    static constexpr float Pi=3.14159265358979323846f;
    static simd_float4 rgb(unsigned c) {
        return {float((c>>16)&255)/255,float((c>>8)&255)/255,float(c&255)/255,1};
    }
    void triangle(V3 a,V3 b,V3 c,unsigned tint,bool shade=false) {
        auto color=rgb(tint);
        if(shade) {
            const auto normal=simd_normalize(simd_cross(b-a,c-a));
            const float light=.67f+.33f*std::abs(simd_dot(normal,simd_normalize(V3{-2,4,-3})));
            color.x*=light;color.y*=light;color.z*=light;
        }
        faces.push_back({a,b,c,color});
    }
    void quad(V3 a,V3 b,V3 c,V3 d,unsigned color,bool shade=false) {
        triangle(a,b,c,color,shade);triangle(a,c,d,color,shade);
    }
    void box(V3 lo,V3 hi,unsigned color) {
        const V3 a{lo.x,lo.y,lo.z},b{hi.x,lo.y,lo.z},c{hi.x,hi.y,lo.z},d{lo.x,hi.y,lo.z};
        const V3 e{lo.x,lo.y,hi.z},f{hi.x,lo.y,hi.z},g{hi.x,hi.y,hi.z},h{lo.x,hi.y,hi.z};
        quad(a,b,c,d,color,true);quad(f,e,h,g,color,true);
        quad(e,a,d,h,color,true);quad(b,f,g,c,color,true);quad(d,c,g,h,color,true);
    }
    void ground(float x0,float x1,float z0,float z1,float y,unsigned color) {
        quad({x0,y,z0},{x1,y,z0},{x1,y,z1},{x0,y,z1},color);
    }
    template<class Emit> void paint(Emit emit) {
        for(const auto &face:faces) {
            // Clip at the camera before projection, then at the panel edges.
            std::array<V3,6> eye{},near{};int n=3,m=0;
            const std::array<V3,3> world{face.a,face.b,face.c};
            for(int i=0;i<3;++i) {
                const auto p=world[i]-camera;
                eye[i]={simd_dot(p,right),simd_dot(p,up),simd_dot(p,forward)};
            }
            for(int i=0;i<n;++i) {
                auto a=eye[i],b=eye[(i+1)%n];const bool ia=a.z>=.25f,ib=b.z>=.25f;
                if(ia)near[m++]=a;
                if(ia!=ib)near[m++]=a+(b-a)*((.25f-a.z)/(b.z-a.z));
            }
            if(m<3)continue;
            std::array<V3,12> polygon{},clipped{};n=m;
            for(int i=0;i<n;++i)polygon[i]={viewport.x+viewport.w*.5f+near[i].x/near[i].z*viewport.h,
                viewport.y+viewport.h*.48f-near[i].y/near[i].z*viewport.h,1-.25f/near[i].z};
            for(int edge=0;edge<4 && n>=3;++edge) {
                auto inside=[&](V3 p) {switch(edge) {
                    case 0:return p.x-viewport.x;case 1:return viewport.x+viewport.w-p.x;
                    case 2:return p.y-viewport.y;default:return viewport.y+viewport.h-p.y;
                }};
                m=0;
                for(int i=0;i<n;++i) {
                    const auto a=polygon[i],b=polygon[(i+1)%n];const float da=inside(a),db=inside(b);
                    if(da>=0)clipped[m++]=a;
                    if((da>=0)!=(db>=0))clipped[m++]=a+(b-a)*(da/(da-db));
                }
                polygon=clipped;n=m;
            }
            for(int i=1;i+1<n;++i)emit(polygon[0],polygon[i],polygon[i+1],face.tint);
        }
        faces.clear();
    }
    void wheel(float x,float z,float radius,float angle) {
        constexpr int steps=20;
        const float outside=x+(x<1.75f ? -.14f : .14f),inside=x-(x<1.75f ? -.14f : .14f);
        const V3 center{outside,radius,z};
        for(int i=0;i<steps;++i) {
            const float a=2*Pi*i/steps,b=2*Pi*(i+1)/steps;
            const V3 p{outside,radius+radius*std::cos(a),z+radius*std::sin(a)};
            const V3 q{outside,radius+radius*std::cos(b),z+radius*std::sin(b)};
            triangle(center,p,q,0x141A20);
            quad(p,q,{inside,q.y,q.z},{inside,p.y,p.z},0x20262C,true);
            const V3 rimP{outside,radius+radius*.64f*std::cos(a),z+radius*.64f*std::sin(a)};
            const V3 rimQ{outside,radius+radius*.64f*std::cos(b),z+radius*.64f*std::sin(b)};
            triangle(center,rimP,rimQ,0x63727C);
        }
        for(int i=0;i<5;++i) {
            const float a=angle+i*2*Pi/5;
            triangle(center,{outside,radius+radius*.6f*std::cos(a-.15f),z+radius*.6f*std::sin(a-.15f)},
                {outside,radius+radius*.6f*std::cos(a+.15f),z+radius*.6f*std::sin(a+.15f)},0xD2DCE0);
        }
    }
    void car(double distance,float tireRadius,bool braking) {
        constexpr float center=1.75f;
        auto p=[](float x,float y,float z)->V3{return {center+x,y,z};};
        // Flat shadow first; its road plane is separate from the body geometry.
        ground(center-1.05f,center+1.05f,-2.3f,2.25f,.02f,0x272F34);
        box(p(-.9f,.37f,-2.08f),p(.9f,.78f,2.05f),0xCA4B32);
        box(p(-.86f,.3f,-2.10f),p(.86f,.42f,2.07f),0x252C32);
        // Sculpted shoulders, deck, bonnet and a fastback cabin.
        quad(p(-.9f,.78f,-2.08f),p(.9f,.78f,-2.08f),p(.83f,.98f,-.95f),p(-.83f,.98f,-.95f),0xE56A45,true);
        quad(p(-.83f,.98f,.68f),p(.83f,.98f,.68f),p(.87f,.81f,2.05f),p(-.87f,.81f,2.05f),0xE77850,true);
        for(float sign:{-1.f,1.f}) {
            quad(p(sign*.9f,.78f,-2.08f),p(sign*.9f,.78f,2.05f),p(sign*.83f,.98f,.68f),p(sign*.83f,.98f,-.95f),0xD85B3C,true);
            quad(p(sign*.82f,.97f,-.96f),p(sign*.66f,1.42f,-.26f),p(sign*.65f,1.42f,.35f),p(sign*.82f,.97f,.95f),0xA33E2E,true);
            quad(p(sign*.805f,1.015f,-.82f),p(sign*.67f,1.37f,-.23f),p(sign*.66f,1.37f,.31f),p(sign*.795f,1.015f,.8f),0x273F4B);
            // B pillar and small mirror.
            quad(p(sign*.81f,1.015f,-.08f),p(sign*.665f,1.38f,-.08f),p(sign*.665f,1.38f,.015f),p(sign*.81f,1.015f,.015f),0x171F27);
            box(p(sign*.99f,1.0f,.6f),p(sign*.99f+.09f,1.1f,.78f),0xDD6746);
        }
        quad(p(-.66f,1.42f,-.26f),p(.66f,1.42f,-.26f),p(.65f,1.42f,.35f),p(-.65f,1.42f,.35f),0xEC855C,true);
        quad(p(-.78f,1.01f,-.93f),p(.78f,1.01f,-.93f),p(.63f,1.38f,-.25f),p(-.63f,1.38f,-.25f),0x335461);
        quad(p(-.63f,1.38f,.36f),p(.63f,1.38f,.36f),p(.77f,1.01f,.91f),p(-.77f,1.01f,.91f),0x456774);
        // Rear grille, continuous tail lamps, registration panel and exhausts.
        quad(p(-.8f,.58f,-2.09f),p(.8f,.58f,-2.09f),p(.8f,.7f,-2.09f),p(-.8f,.7f,-2.09f),0x1C252B);
        quad(p(-.81f,.70f,-2.095f),p(.81f,.70f,-2.095f),p(.81f,.755f,-2.095f),p(-.81f,.755f,-2.095f),braking ? 0xFF634F : 0xBA352B);
        quad(p(-.21f,.47f,-2.11f),p(.21f,.47f,-2.11f),p(.21f,.57f,-2.11f),p(-.21f,.57f,-2.11f),0xD9D5C4);
        for(float x:{-.69f,.69f})box(p(x-.095f,.3f,-2.19f),p(x+.095f,.39f,-2.03f),0x9CA9AD);
        // A modest rear wing makes the silhouette readable at panel size.
        for(float x:{-.6f,.6f})box(p(x-.035f,.89f,-1.78f),p(x+.035f,1.05f,-1.68f),0x292F35);
        box(p(-.96f,1.05f,-1.88f),p(.96f,1.10f,-1.6f),0xC94B34);
        const float radius=std::clamp(tireRadius,.25f,.43f);
        const float angle=std::fmod(distance/std::max(.1f,tireRadius),2*Pi);
        for(float x:{center-.88f,center+.88f})for(float z:{-1.29f,1.26f})wheel(x,z,radius,angle);
    }
public:
    SoundRoadScene() {
        faces.reserve(4096);
        forward=simd_normalize(V3{1.75f,.9f,5.0f}-camera);
        right=simd_normalize(simd_cross(V3{0,1,0},forward));up=simd_cross(forward,right);
    }
    template<class Emit> void draw(sound_ui::UiRect area,double distance,float tireRadius,bool braking,Emit emit) {
        viewport=area;faces.clear();
        if(!std::isfinite(distance))distance=0;
        // Distant mountains sit beyond the repeating roadside scenery.
        ground(-500,500,-15,600,-.05f,0x576B61);
        for(int i=-5;i<=5;++i) {
            const float x=i*37.f,height=22+(i*i%5)*7;
            triangle({x-42,0,240},{x,height,240},{x+49,0,240},0x778C8E);
            triangle({x, height,240},{x+11,0,237},{x+49,0,240},0x647C7F);
        }
        paint(emit);
        ground(-4.9f,4.9f,-15,230,0,0x8E9687);
        ground(-4.15f,4.15f,-15,230,.002f,0x414E55);
        paint(emit);
        for(float side:{-1.f,1.f})ground(side*4.02f-.06f,side*4.02f+.06f,-15,230,.007f,0xD5D3BE);
        const float stripePhase=std::fmod(distance,12.0);
        for(int i=-2;i<20;++i)ground(-.065f,.065f,i*12.f-stripePhase,i*12.f+5-stripePhase,.008f,0xE4DCBC);
        // Quiet transverse road texture reinforces motion without motion blur.
        const float seamPhase=std::fmod(distance,30.0);
        for(int i=0;i<8;++i)ground(-4,4,i*30.f-seamPhase,i*30.f+.025f-seamPhase,.006f,0x48555B);
        paint(emit);
        const float postPhase=std::fmod(distance,16.0);
        for(int i=0;i<14;++i)for(float side:{-1.f,1.f}) {
            const float z=i*16.f-postPhase;
            box({side*4.65f-.045f,0,z-.05f},{side*4.65f+.045f,.78f,z+.05f},0xCAD2C6);
            box({side*4.65f-.05f,.57f,z-.06f},{side*4.65f+.05f,.7f,z+.06f},side>0 ? 0xEAD5A4 : 0xD57553);
        }
        const float treePhase=std::fmod(distance,32.0);
        for(int i=0;i<7;++i)for(float side:{-1.f,1.f}) {
            const float z=i*32.f-treePhase+side*7;
            const float x=side*(8.5f+std::fmod(std::floor(distance/32)+i,2.0)*2);
            box({x-.12f,0,z-.12f},{x+.12f,1.8f,z+.12f},0x626B58);
            for(int level=0;level<2;++level) {
                const float base=1.f+level,height=3.3f+level*.7f,radius=1.3f-level*.25f;
                for(int face=0;face<6;++face) {
                    const float a=face*Pi/3,b=(face+1)*Pi/3;
                    triangle({x, height,z},{x+radius*std::cos(a),base,z+radius*std::sin(a)},
                        {x+radius*std::cos(b),base,z+radius*std::sin(b)},0x405C52,true);
                }
            }
        }
        car(distance,tireRadius,braking);paint(emit);
    }
};
#endif
