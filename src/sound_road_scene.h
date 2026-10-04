#ifndef ENGINE_SIM_SOUND_ROAD_SCENE_H
#define ENGINE_SIM_SOUND_ROAD_SCENE_H
#include "sound_metal.h"
#include "driving_game.h"
#include "driving_camera.h"
#include <simd/simd.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

// Immutable world geometry plus a renderer-owned chase camera. Gameplay lives
// on its own worker; all scenery, road markings and textures stay in metres.
class SoundRoadScene {
    using V3=simd_float3;
    struct Face {V3 a,b,c;simd_float4 tint;unsigned kind;std::array<simd_float2,3> uv{};};
    struct ClipVertex {V3 p;simd_float2 uv;};
    struct Tree {float x,z,height,shade;};
    struct Skid {V3 a,b,c,d;double time;};
    std::array<Skid,512> skids{};
    size_t skidWrite=0,skidCount=0;
    DrivingSnapshot skidPose{};
    std::vector<Face> faces,world,cityWorld;
    std::vector<Tree> trees,cityTrees;
    DrivingCourse track;
    DrivingSnapshot vehicle{};
    V3 camera{},forward{0,0,1},right{1,0,0},up{0,1,0};
    DrivingCamera chase;
    sound_ui::UiRect viewport{};
    static constexpr float Pi=3.14159265358979323846f;
    static simd_float4 rgb(unsigned c) {return {float((c>>16)&255)/255,float((c>>8)&255)/255,float(c&255)/255,1};}
    void triangle(V3 a,V3 b,V3 c,unsigned tint,bool shade=false,unsigned kind=3) {
        auto color=rgb(tint);
        if(shade) {
            auto normal=simd_normalize(simd_cross(b-a,c-a));
            const float diffuse=.48f+.52f*std::abs(simd_dot(normal,sunDirection().xyz));
            color*=diffuse;color.w=1;
        }
        faces.push_back({a,b,c,color,kind});
    }
    void quad(V3 a,V3 b,V3 c,V3 d,unsigned color,bool shade=false,unsigned kind=3) {
        triangle(a,b,c,color,shade,kind);triangle(a,c,d,color,shade,kind);
    }
    void box(V3 lo,V3 hi,unsigned color) {
        const V3 a{lo.x,lo.y,lo.z},b{hi.x,lo.y,lo.z},c{hi.x,hi.y,lo.z},d{lo.x,hi.y,lo.z};
        const V3 e{lo.x,lo.y,hi.z},f{hi.x,lo.y,hi.z},g{hi.x,hi.y,hi.z},h{lo.x,hi.y,hi.z};
        quad(a,b,c,d,color,true);quad(f,e,h,g,color,true);quad(e,a,d,h,color,true);
        quad(b,f,g,c,color,true);quad(d,c,g,h,color,true);
    }
    void ground(float x0,float z0,float x1,float z1,float y,unsigned color,unsigned kind=3) {
        quad({x0,y,z0},{x1,y,z0},{x1,y,z1},{x0,y,z1},color,false,kind);
    }
    void facade(V3 a,V3 b,V3 c,V3 d,unsigned color,bool glass) {
        quad(a,b,c,d,color,true,glass ? 8 : 7);
        const bool alongX=std::abs(b.x-a.x)>std::abs(b.z-a.z);
        for(size_t i=faces.size()-2;i<faces.size();++i) {
            auto &face=faces[i];const V3 corners[]={face.a,face.b,face.c};
            for(int k=0;k<3;++k)face.uv[k]={alongX ? corners[k].x : corners[k].z,corners[k].y};
        }
    }
    void buildCity() {
        constexpr float block=DrivingCity::Block,edge=DrivingCity::Extent;
        // Short road patches make culling local even on a kilometre-long avenue.
        for(int i=-DrivingCity::GridRadius;i<=DrivingCity::GridRadius;++i) {
            const float at=i*block,w=DrivingCity::roadHalfWidth(i);
            for(float s=-edge;s<edge;s+=16) {
                const float end=std::min(edge,s+16);
                ground(at-w,s,at+w,end,.006f,0x353E47,4);
                ground(s,at-w,end,at+w,.006f,0x353E47,4);
                const int cross=int(std::round((s+4)/block));
                if(std::abs(s+4-cross*block)>DrivingCity::roadHalfWidth(cross)+8) {
                    for(float offset:{-.25f,.25f}) {
                        ground(at+offset-.045f,s,at+offset+.045f,s+8,.017f,0xDAC479);
                        ground(s,at+offset-.045f,s+8,at+offset+.045f,.017f,0xDAC479);
                    }
                    for(float lane:{-6.f,6.f}) {
                        ground(at+lane-.055f,s,at+lane+.055f,s+5,.018f,0xDCE1DD);
                        ground(s,at+lane-.055f,s+5,at+lane+.055f,.018f,0xDCE1DD);
                    }
                }
            }
        }
        for(int x=-3;x<3;++x)for(int z=-3;z<3;++z) {
            const float x0=x*block+DrivingCity::roadHalfWidth(x),z0=z*block+DrivingCity::roadHalfWidth(z);
            const float x1=(x+1)*block-DrivingCity::roadHalfWidth(x+1),z1=(z+1)*block-DrivingCity::roadHalfWidth(z+1);
            if(DrivingCity::plaza(x,z)) {
                ground(x0,z0,x1,z1,.009f,0x626B70,4);
                for(float a=x0+15;a<x1-15;a+=4) {
                    ground(a,z0+12,a+.12f,z0+22,.018f,0xCDD2CC);
                    ground(a,z1-22,a+.12f,z1-12,.018f,0xCDD2CC);
                }
            } else box({x0,.0f,z0},{x1,.14f,z1},0xAEB3B0);
            for(auto p:{V3{x0+3,0,z0+3},V3{x1-3,0,z1-3}})
                cityTrees.push_back({p.x,p.z,6.5f,.92f});
        }
        for(const auto &b:drivingCity().buildings()) {
            const float x=b.lo.x,z=b.lo.z,X=b.hi.x,Z=b.hi.z,h=b.height;
            const unsigned tint=b.glass ? 0x79929E : b.color;
            const V3 a{x,.14f,z},c{X,h,z},e{x,.14f,Z},g{X,h,Z};
            // World-space facade coordinates give cached buildings metre-sized
            // windows without thousands of extra window polygons or textures.
            facade(a,{X,.14f,z},c,{x,h,z},tint,b.glass);
            facade({X,.14f,Z},e,{x,h,Z},g,tint,b.glass);
            facade(e,a,{x,h,z},{x,h,Z},tint,b.glass);
            facade({X,.14f,z},{X,.14f,Z},g,c,tint,b.glass);
            box({x-.2f,h,z-.2f},{X+.2f,h+.5f,Z+.2f},0xCED1CC);
            const float mx=(x+X)*.5f,mz=(z+Z)*.5f;
            box({mx-4,h+.5f,mz-2},{mx+4,h+1.5f,mz+2},0x65747C);
            box({x,2.8f,z-.45f},{X,3.15f,z},b.glass ? 0x546D7B : 0x5D7778);
            box({x,.15f,z-.12f},{X,1.1f,z},0x6E777D);
        }
        for(int x=-3;x<=3;++x)for(int z=-3;z<=3;++z) {
            const float xx=x*block,zz=z*block,wx=DrivingCity::roadHalfWidth(x),wz=DrivingCity::roadHalfWidth(z);
            for(float side:{-1.f,1.f})for(float stripe=-wx+2;stripe<wx-1;stripe+=2.4f)
                ground(xx+stripe,zz+side*(wz+2),xx+stripe+1.1f,zz+side*(wz+2)+3,.019f,0xD8DED6);
            for(float side:{-1.f,1.f})for(float stripe=-wz+2;stripe<wz-1;stripe+=2.4f)
                ground(xx+side*(wx+2),zz+stripe,xx+side*(wx+2)+3,zz+stripe+1.1f,.019f,0xD8DED6);
            const float px=xx+wx+2,pz=zz+wz+2;
            box({px,.14f,pz},{px+.12f,6.8f,pz+.12f},0x677982);
            box({px-2.2f,6.7f,pz},{px+.12f,6.86f,pz+.12f},0x677982);
            box({px-2.2f,6.55f,pz-.2f},{px-1.1f,6.75f,pz+.3f},0xDAE0C9);
        }
        // Visible sea wall matches the playable boundary, leaving a broad
        // promenade outside the outer streets for recovering from a fast turn.
        for(float s=-edge;s<edge;s+=16)for(float side:{-1.f,1.f}) {
            const float wall=side*(edge+1),end=std::min(edge,s+16);
            box({s,0,wall-.5f},{end,1.1f,wall+.5f},0xADB9B9);
            box({wall-.5f,0,s},{wall+.5f,1.1f,end},0xADB9B9);
        }
        cityWorld=std::move(faces);faces.clear();faces.reserve(14000);
    }
    static V3 point(DrivingPoint p,float height=0) {return {float(p.x),height,float(p.z)};}
    void ribbon(double s0,double s1,double offset0,double offset1,float height,unsigned color,unsigned kind=3) {
        const auto a=track.at(s0),b=track.at(s1);
        quad(point(a.point+a.right*offset0,height),point(a.point+a.right*offset1,height),
            point(b.point+b.right*offset1,height),point(b.point+b.right*offset0,height),color,false,kind);
    }
    static float random(uint32_t v) {v^=v>>16;v*=0x7feb352d;v^=v>>15;v*=0x846ca68b;v^=v>>16;return (v&0xffffff)/float(0x1000000);}
    void buildWorld() {
        faces.reserve(14000);
        constexpr double road=DrivingCourse::HalfWidth,barrier=DrivingCourse::BarrierWidth;
        for(double s=0;s<track.length();s+=4) {
            const double end=std::min(track.length(),s+4);
            ribbon(s,end,-barrier+1,barrier-1,0,0x807D68);
            ribbon(s,end,-road,road,.006f,0x343B40,4);
            for(double side:{-1.,1.}) {
                ribbon(s,end,side*road,side*(road+.45),.012f,int(s/4)%2 ? 0xD6D8D1 : 0xAB4039);
                ribbon(s,end,side*(road-.15),side*(road-.08),.014f,0xE0E0D3);
                const auto a=track.at(s),b=track.at(end);
                for(float height:{.42f,.76f})quad(point(a.point+a.right*(side*barrier),height),
                    point(b.point+b.right*(side*barrier),height),point(b.point+b.right*(side*barrier),height+.17f),
                    point(a.point+a.right*(side*barrier),height+.17f),0x9CA9AF,true);
                if(int(s/4)%2==0) {
                    const auto p=a.point+a.right*(side*barrier);
                    box(point(p-DrivingPoint{.05,.05}),point(p+DrivingPoint{.05,.05},.98f),0x707C82);
                }
            }
        }
        // Finish stripe and eight numbered checkpoints are fixed world gates.
        for(int z=0;z<6;++z)for(int x=0;x<int(road*4);++x)
            ribbon(z*.5,z*.5+.5,-road+x*.5,-road+.5+x*.5,.022f,(x+z)%2 ? 0x192027 : 0xE9EBE2);
        for(int gate=0;gate<8;++gate) {
            const auto p=track.at(gate*track.length()/8);
            for(double side:{-1.,1.}) {
                const auto q=p.point+p.right*(side*(barrier-.6));
                box(point(q-DrivingPoint{.12,.12}),point(q+DrivingPoint{.12,.12},4.2f),0x687B84);
                for(int h=0;h<5;++h) {
                    const auto a=q+p.right*(-.35),b=q+p.right*.35;
                    quad(point(a,2.5f+h*.24f),point(b,2.5f+h*.24f),point(b,2.74f+h*.24f),point(a,2.74f+h*.24f),
                        gate==0 ? (h%2 ? 0x172027 : 0xE7E9DF) : 0x61B6D0);
                }
            }
        }
        world=std::move(faces);faces.clear();faces.reserve(14000);
        for(int i=0;i<DrivingCourse::Segments;i+=3)for(int row=0;row<3;++row)for(int side:{-1,1}) {
            const uint32_t seed=i*47+row*1367+(side+1)*7187;
            const auto location=track.at(track.length()*i/DrivingCourse::Segments+random(seed)*6);
            const auto p=location.point+location.right*(side*(DrivingCourse::BarrierWidth+7.5+row*15+random(seed+1)*8));
            if(std::abs(track.nearest(p).lateral)<DrivingCourse::BarrierWidth+3.5)continue;
            trees.push_back({float(p.x),float(p.z),10+random(seed+2)*5,.82f+random(seed+3)*.18f});
        }
    }
    template<class Emit> void paint(Emit emit) {
        for(const auto &face:faces) {
            std::array<V3,6> eye{},near{};int n=3,m=0;
            std::array<simd_float2,6> nearUv{};const std::array<V3,3> world{face.a,face.b,face.c};
            for(int i=0;i<3;++i) {const auto p=world[i]-camera;eye[i]={simd_dot(p,right),simd_dot(p,up),simd_dot(p,forward)};}
            for(int i=0;i<n;++i) {
                auto a=eye[i],b=eye[(i+1)%n];const bool ia=a.z>=.25f,ib=b.z>=.25f;
                if(ia) {near[m]=a;nearUv[m++]=face.uv[i];}
                if(ia!=ib) {const float t=(.25f-a.z)/(b.z-a.z);near[m]=a+(b-a)*t;nearUv[m++]=face.uv[i]+(face.uv[(i+1)%n]-face.uv[i])*t;}
            }
            if(m<3)continue;
            std::array<ClipVertex,12> polygon{},clipped{};n=m;
            for(int i=0;i<n;++i) {
                const auto p=camera+right*near[i].x+up*near[i].y+forward*near[i].z;
                polygon[i]={{viewport.x+viewport.w*.5f+near[i].x/near[i].z*viewport.h,
                    viewport.y+viewport.h*.48f-near[i].y/near[i].z*viewport.h,1-.25f/near[i].z},
                    face.kind>=6 && face.kind<=8 ? nearUv[i]/near[i].z : simd_float2{p.x/near[i].z,p.z/near[i].z}};
            }
            for(int edge=0;edge<4 && n>=3;++edge) {
                auto inside=[&](V3 p) {switch(edge) {case 0:return p.x-viewport.x;case 1:return viewport.x+viewport.w-p.x;
                    case 2:return p.y-viewport.y;default:return viewport.y+viewport.h-p.y;}};
                m=0;for(int i=0;i<n;++i) {
                    const auto a=polygon[i],b=polygon[(i+1)%n];const float da=inside(a.p),db=inside(b.p);
                    if(da>=0)clipped[m++]=a;
                    if((da>=0)!=(db>=0)) {const float t=da/(da-db);clipped[m++]={a.p+(b.p-a.p)*t,a.uv+(b.uv-a.uv)*t};}
                }
                polygon=clipped;n=m;
            }
            for(int i=1;i+1<n;++i)emit(polygon[0],polygon[i],polygon[i+1],face.tint,face.kind);
        }
        faces.clear();
    }
    void shadow() {
        const V3 f{float(std::sin(vehicle.yaw)),0,float(std::cos(vehicle.yaw))},r{f.z,0,-f.x};
        const V3 center{float(vehicle.x),.026f,float(vehicle.z)};
        for(int ring=3;ring>=0;--ring)for(int i=0;i<40;++i) {
            const float a=i*Pi/20,b=(i+1)*Pi/20,w=1.08f+ring*.13f,h=2.08f+ring*.13f;
            triangle(center,center+r*(w*std::cos(a))+f*(h*std::sin(a)),center+r*(w*std::cos(b))+f*(h*std::sin(b)),0x101D25);
            faces.back().tint.w=.13f;
        }
    }
public:
    SoundRoadScene() {buildWorld();buildCity();}
    const DrivingCourse &course() const {return track;}
    const DrivingSnapshot &carPose() const {return vehicle;}
    void update(const DrivingSnapshot &pose,double dt) {
        if(pose.time<vehicle.time || pose.recoveries!=vehicle.recoveries || pose.world!=vehicle.world) {
            skidCount=skidWrite=0;skidPose=pose;
        }
        const double moved=std::hypot(pose.x-skidPose.x,pose.z-skidPose.z);
        if(moved>.45) {
            if(moved<3 && std::abs(pose.driftAngle)>.1 && pose.speed>7 && !pose.offroad && pose.recoveries==skidPose.recoveries) {
                auto tyre=[](const DrivingSnapshot &p,float side,float width) {
                    const V3 f{float(std::sin(p.yaw)),0,float(std::cos(p.yaw))},r{f.z,0,-f.x};
                    return V3{float(p.x),.023f,float(p.z)}-f*float(DrivingHandling::RearAxle)+r*(side*.78f+width);
                };
                for(float side:{-1.f,1.f}) {
                    skids[skidWrite]={tyre(skidPose,side,-.11f),tyre(skidPose,side,.11f),tyre(pose,side,.11f),tyre(pose,side,-.11f),pose.time};
                    skidWrite=(skidWrite+1)%skids.size();skidCount=std::min(skids.size(),skidCount+1);
                }
            }
            skidPose=pose;
        }
        vehicle=pose;chase.update(pose,dt);
        auto vector=[](DrivingCamera::Vector p) {return V3{float(p.x),float(p.y),float(p.z)};};
        camera=vector(chase.position);forward=vector(chase.forward);right=vector(chase.right);up=vector(chase.up);
    }
    DrivingCamera::Projection sunPosition(double width,double height) const {return chase.sun(width,height);}
    static simd_float4 sunDirection() {
        constexpr auto d=DrivingCamera::SunDirection;
        const auto p=simd_normalize(V3{float(d.x),float(d.y),float(d.z)});
        return {p.x,p.y,p.z,0};
    }
    simd_float4 cameraPosition() const {return {camera.x,camera.y,camera.z,0};}
    simd_float4 cameraRight() const {return {right.x,right.y,right.z,0};}
    simd_float4 cameraUp() const {return {up.x,up.y,up.z,0};}
    simd_float4 cameraForward() const {return {forward.x,forward.y,forward.z,0};}
    template<class Emit> void draw(sound_ui::UiRect area,Emit emit) {
        viewport=area;faces.clear();
        const bool city=vehicle.world==DrivingWorld::City;
        if(city) {
            ground(-1800,-1800,1800,1800,-.25f,0x477E8E,9);paint(emit);
            ground(-DrivingCity::Extent,-DrivingCity::Extent,DrivingCity::Extent,DrivingCity::Extent,0,0x677669,5);paint(emit);
        } else {
        quad({-950,-.05f,-950},{950,-.05f,-950},{950,-.05f,950},{-950,-.05f,950},0x53634A,false,5);paint(emit);
        // World-space distant hills rotate naturally with the chase camera.
        for(int i=0;i<120;++i) {
            const float a=i*Pi/60,b=(i+1)*Pi/60,r=1600;
            auto ridge=[](float angle) {return 70+28*std::sin(angle*3)+18*std::sin(angle*7)+8*std::cos(angle*13);};
            quad({r*std::sin(a),-8,r*std::cos(a)},{r*std::sin(a),ridge(a),r*std::cos(a)},
                {r*std::sin(b),ridge(b),r*std::cos(b)},{r*std::sin(b),-8,r*std::cos(b)},0x687D87);
        }
        paint(emit);
        }
        for(const auto &face:city ? cityWorld : world) {
            const auto center=(face.a+face.b+face.c)/3;
            const float radius=city ? 390 : 240;
            // Large building faces can straddle the camera even with their
            // centre behind it. The clipper, not centre culling, rejects them.
            if(simd_length_squared(center-camera)>radius*radius || simd_dot(center-camera,forward)<(city ? -160 : -12))continue;
            faces.push_back(face);
        }
        paint(emit);
        for(const auto &t:city ? cityTrees : trees) {
            const V3 center{t.x,-.25f,t.z};
            if(simd_length_squared(center-camera)>250*250 || simd_dot(center-camera,forward)<-15)continue;
            const V3 across=simd_normalize(V3{center.z-camera.z,0,camera.x-center.x})*(t.height*1.0524f/2),rise{0,t.height,0};
            const auto a=center-across,b=center+across,c=b+rise,d=a+rise;const simd_float4 tint{t.shade,t.shade,t.shade,1};
            faces.push_back({a,b,c,tint,6,{{{0,1},{1,1},{1,0}}}});faces.push_back({a,c,d,tint,6,{{{0,1},{1,0},{0,0}}}});
        }
        if(city) {
            const auto target=DrivingCity::Destinations[vehicle.cityStops%DrivingCity::Destinations.size()];
            for(int i=0;i<48;++i) {
                const float a=i*Pi/24,b=(i+1)*Pi/24;
                const V3 inner{float(target.x),.025f,float(target.z)};
                quad(inner+V3{9*std::sin(a),0,9*std::cos(a)},inner+V3{10*std::sin(a),0,10*std::cos(a)},
                    inner+V3{10*std::sin(b),0,10*std::cos(b)},inner+V3{9*std::sin(b),0,9*std::cos(b)},0x65DAB0);
            }
            const V3 p{float(target.x),0,float(target.z)};
            const float pulse=1+float(std::sin(vehicle.time*3))*.15f;
            for(float side:{-1.f,1.f})box(p+V3{side*10-.18f,0,-.18f},p+V3{side*10+.18f,4*pulse,.18f},0x65DAB0);
        }
        for(size_t i=0;i<skidCount;++i) {
            const auto &s=skids[i];const auto center=(s.a+s.c)*.5f;
            if(vehicle.time-s.time>25 || simd_length_squared(center-camera)>160*160)continue;
            quad(s.a,s.b,s.c,s.d,0x151C20);
        }
        shadow();paint(emit);
    }
};
#endif
