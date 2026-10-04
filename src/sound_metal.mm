#import <Cocoa/Cocoa.h>
#import <Metal/Metal.h>
#import <MetalKit/MetalKit.h>
#import <QuartzCore/CAMetalLayer.h>
#import <CoreVideo/CVDisplayLink.h>
#import <QuartzCore/CATransaction.h>
#import <CoreText/CoreText.h>
#include "sound_metal.h"
#include "sound_session.h"
#include "sound_road_scene.h"
#include "vehicle_visual_motion.h"
#include "driving_game_worker.h"
#include "authored_mesh_library.h"
#include "units.h"
#include <SDL3/SDL.h>
#include <simd/simd.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include "sound_car_metal.h"

using namespace sound_ui;

namespace {
constexpr int AtlasSize = 2048, MaxVertices = 1048576;
constexpr float Pi = 3.14159265358979323846f;
struct Vertex { simd_float2 p, uv; simd_float4 color; uint32_t kind;float depth=1;uint32_t padding[2]{}; };
static_assert(sizeof(Vertex) == 48);
struct Glyph { float u0=0,v0=0,u1=0,v1=0,w=0,h=0,advance=0,left=0,bottom=0; };
struct Font { std::array<Glyph,128> glyphs; float ascent=0; };
simd_float4 color(unsigned rgb, float alpha = 1) {
    return {((rgb>>16)&255)/255.f, ((rgb>>8)&255)/255.f, (rgb&255)/255.f, alpha};
}
constexpr unsigned Ink=0xF3F3F3, Dim=0xB0B7BC, Panel=0x0E1011, Edge=0x777B7D;
constexpr unsigned Red=0xEE4445, Blue=0x77CEE0, Yellow=0xFDBD2E, Orange=0xE98434, Pink=0xEF91BB;

class DrawList {
public:
    std::vector<Vertex> vertices;
    std::array<Font,5> fonts;
    float uiDepth=1;
    size_t roadEnd=0;
    DrawList() { vertices.reserve(262144); }
    void triangle(simd_float2 a, simd_float2 b, simd_float2 c, simd_float4 tint) {
        vertices.push_back({a,{},tint,0,uiDepth}); vertices.push_back({b,{},tint,0,uiDepth}); vertices.push_back({c,{},tint,0,uiDepth});
    }
    void quad(UiRect r, simd_float4 tint, uint32_t kind=0, simd_float2 uv0={}, simd_float2 uv1={}) {
        Vertex a{{r.x,r.y},uv0,tint,kind,uiDepth}, b{{r.x+r.w,r.y},{uv1.x,uv0.y},tint,kind,uiDepth};
        Vertex c{{r.x+r.w,r.y+r.h},uv1,tint,kind,uiDepth}, d{{r.x,r.y+r.h},{uv0.x,uv1.y},tint,kind,uiDepth};
        vertices.insert(vertices.end(), {a,b,c,a,c,d});
    }
    void rounded(UiRect r, float radius, simd_float4 tint) {
        const simd_float2 center{r.x+r.w/2,r.y+r.h/2};
        std::array<simd_float2,36> points;
        for (int corner=0; corner<4; ++corner) {
            const float cx = corner==0 || corner==3 ? r.x+radius : r.x+r.w-radius;
            const float cy = corner<2 ? r.y+radius : r.y+r.h-radius;
            for (int step=0; step<=8; ++step) {
                const float a = (180 + corner*90 + step*90.f/8) * Pi/180;
                points[corner*9+step] = {cx+radius*std::cos(a),cy+radius*std::sin(a)};
            }
        }
        for (int i=0; i<36; ++i) triangle(center,points[i],points[(i+1)%36],tint);
    }
    void line(float x0,float y0,float x1,float y1,float width,simd_float4 tint) {
        const float length=std::hypot(x1-x0,y1-y0);
        if (length < .001f) return;
        const simd_float2 n{(y0-y1)*width/(2*length),(x1-x0)*width/(2*length)};
        const simd_float2 a{x0,y0},b{x1,y1};
        triangle(a+n,b+n,b-n,tint); triangle(a+n,b-n,a-n,tint);
    }
    float textWidth(const char *s, int font=0) const {
        float width=0;
        for (;*s;++s) { const unsigned c=static_cast<unsigned char>(*s); if(c<128) width+=fonts[font].glyphs[c].advance; }
        return width;
    }
    void text(const char *s,float x,float y,int font=0,unsigned rgb=Ink,float alpha=1) {
        const auto &f=fonts[font];
        for (;*s;++s) {
            const unsigned c=static_cast<unsigned char>(*s);
            if(c>=128) continue;
            const auto &g=f.glyphs[c];
            if(g.w) quad({x+g.left,y+f.ascent-g.bottom-g.h,g.w,g.h},color(rgb,alpha),1,{g.u0,g.v1},{g.u1,g.v0});
            x+=g.advance;
        }
    }
    void centered(const char *s,float x,float y,int font=0,unsigned rgb=Ink) { text(s,x-textWidth(s,font)/2,y,font,rgb); }
    void right(const char *s,float x,float y,int font=0,unsigned rgb=Ink) { text(s,x-textWidth(s,font),y,font,rgb); }
    void arc(float cx,float cy,float radius,float start,float end,float width,simd_float4 tint) {
        const int n=std::max(1,int(std::abs(end-start)*radius/3));
        for(int i=0;i<n;++i) {
            const float a=start+(end-start)*i/n,b=start+(end-start)*(i+1)/n;
            line(cx+radius*std::cos(a),cy+radius*std::sin(a),cx+radius*std::cos(b),cy+radius*std::sin(b),width,tint);
        }
    }
    AuthoredMeshLibrary meshes;
    SoundRoadScene roadScene;
    struct Trace { float exhaust=0,pressure=0,volume=0,intake=0,outlet=0,rpm=0; };
    std::array<Trace,256> traces{};
    size_t traceWrite=0,traceCount=0;
    std::array<float,VisualMaxCylinders> firing{};
    void resetEngine() { traces={};traceWrite=traceCount=0;firing.fill(0); }
    void accept(const EngineVisualSnapshot &v) {
        traces[traceWrite]={v.exhaustFlow,v.cylinders[0].pressure/100000.f,v.cylinders[0].volume*1e6f,
            v.cylinders[0].intakeLift*1000,v.cylinders[0].exhaustLift*1000,0};
        traceWrite=(traceWrite+1)%traces.size();traceCount=std::min(traceCount+1,traces.size());
        for(int i=0;i<VisualMaxCylinders;++i)if(v.cylinders[i].fired)firing[i]=1;
    }
    void circle(float x,float y,float radius,simd_float4 tint) {
        const int steps=std::max(16,int(radius*1.3f));
        for(int i=0;i<steps;++i) {
            const float a=i*2*Pi/steps,b=(i+1)*2*Pi/steps;
            triangle({x,y},{x+radius*std::cos(a),y+radius*std::sin(a)},
                {x+radius*std::cos(b),y+radius*std::sin(b)},tint);
        }
    }
    void panel(UiRect r,const char *title=nullptr) {
        line(r.x,r.y,r.x+r.w,r.y,.7,color(Edge));
        line(r.x,r.y,r.x,r.y+r.h,.7,color(Edge));
        line(r.x+r.w,r.y,r.x+r.w,r.y+r.h,.7,color(Edge));
        line(r.x,r.y+r.h,r.x+r.w,r.y+r.h,.7,color(Edge));
        if(title)centered(title,r.x+r.w/2,r.y+5,0);
    }
    void button(Control c,const char *name,const State &s,bool selected=false,bool primary=false) {
        const UiRect r=bounds(c,s.roadView);
        if(s.roadView) {
            rounded(r,5,color(s.pressed==c ? 0x303D48 : 0x131E28,s.pressed==c ? .92f : .65f));
            if(selected || s.focus==c || s.hover==c)line(r.x+7,r.y+r.h-1,r.x+r.w-7,r.y+r.h-1,2,color(selected ? Blue : Ink));
            centered(name,r.x+r.w/2,r.y+(r.h-13)/2,0,selected ? Blue : Ink);return;
        }
        quad(r,color(s.pressed==c ? 0x373E42 : selected ? 0x262B2E : s.hover==c ? 0x202629 : Panel));
        panel(r);
        if(s.focus==c)line(r.x+2,r.y+r.h-2,r.x+r.w-2,r.y+r.h-2,2,color(Blue));
        centered(name,r.x+r.w/2,r.y+(r.h-12)/2,0,primary ? Red : selected ? Blue : Ink);
    }
    void gauge(UiRect r,const char *title,float value,float low,float high,const char *formatted,
            bool bands=false,int font=0) {
        if(!std::isfinite(value))value=low;
        panel(r,title);
        const float radius=std::min(r.w*.39f,(r.h-30)*.47f);
        const float cx=r.x+r.w/2,cy=r.y+20+radius;
        constexpr float start=140*Pi/180,span=260*Pi/180;
        const bool tiny=r.w<80;
        const int ticks=tiny ? 16 : 40;
        if(bands) {
            arc(cx,cy,radius+3,start,start+span*.6f,1.3,color(Blue));
            arc(cx,cy,radius+3,start+span*.6f,start+span*.8f,1.3,color(Orange));
            arc(cx,cy,radius+3,start+span*.8f,start+span,1.3,color(Red));
        }
        for(int i=0;i<=ticks;++i) {
            const float angle=start+span*i/ticks,inner=radius-(i%5==0 ? 5 : 2.5f);
            line(cx+inner*std::cos(angle),cy+inner*std::sin(angle),cx+radius*std::cos(angle),cy+radius*std::sin(angle),.8,color(i%5==0 ? Ink : Dim));
        }
        const float angle=start+span*std::clamp((value-low)/(high-low),0.f,1.f);
        line(cx-2*std::cos(angle),cy-2*std::sin(angle),cx+radius*.75f*std::cos(angle),cy+radius*.75f*std::sin(angle),tiny ? 1.5f : 2,color(Red));
        centered(formatted,cx,r.y+r.h-(font==2 ? 26 : 18),font);
    }
    void sliderTrack(Control c,float value,const State &s) {
        const auto r=bounds(c);const float y=r.y+r.h-3,x=r.x+7,w=r.w-14;
        line(x,y,x+w,y,1,color(Edge));
        line(x,y,x+w*std::clamp(value,0.f,1.f),y,2,color(s.hover==c || s.focus==c ? Ink : Blue));
    }
    void plot(UiRect r,const char *title,int channel,unsigned rgb=Orange) {
        panel(r,title);
        const UiRect area{r.x+5,r.y+21,r.w-10,r.h-28};
        for(int i=1;i<4;++i)line(area.x,area.y+i*area.h/4,area.x+area.w,area.y+i*area.h/4,.5,color(0x282C2D));
        auto sample=[&](size_t i,int c) {
            const auto &t=traces[(traceWrite+traces.size()-traceCount+i)%traces.size()];
            switch(c) { case 0:return t.exhaust;case 1:return t.pressure;case 2:return t.volume;case 3:return t.intake;default:return t.outlet; }
        };
        float low=0,high=channel==1 ? 1 : .001f;
        for(size_t i=0;i<traceCount;++i) {const float v=sample(i,channel);if(std::isfinite(v)) {low=std::min(low,v);high=std::max(high,v);}}
        if(channel==3)for(size_t i=0;i<traceCount;++i)high=std::max(high,sample(i,4));
        const auto point=[&](size_t i,int c) {
            float v=sample(i,c);if(!std::isfinite(v))v=0;
            return simd_float2{area.x+area.w*i/255,area.y+area.h*.95f-(v-low)/(high-low)*area.h*.9f};
        };
        for(int c=channel;c<=(channel==3 ? 4 : channel);++c)for(size_t i=1;i<traceCount;++i) {
            auto a=point(i-1,c),b=point(i,c);line(a.x,a.y,b.x,b.y,1,color(c==4 ? Blue : rgb));
        }
    }
    void pcm(UiRect r,const State &s) {
        panel(r,"AUDIO OUTPUT");const float mid=r.y+20+(r.h-24)/2;
        line(r.x+3,mid,r.x+r.w-3,mid,.5,color(0x323638));
        for(int pass=s.effects ? 1 : 0;pass>=0;--pass)for(size_t i=1;i<s.waveform.size();++i) {
            const float y0=mid-std::clamp(s.waveform[i-1]*2,-1.f,1.f)*(r.h-30)/2;
            const float y1=mid-std::clamp(s.waveform[i]*2,-1.f,1.f)*(r.h-30)/2;
            line(r.x+3+(i-1)*(r.w-6)/511,y0,r.x+3+i*(r.w-6)/511,y1,pass ? 4 : 1,color(Blue,pass ? .10f : 1));
        }
    }
    void pv(UiRect r) {
        panel(r,"PRESSURE / VOLUME");float maxP=1,maxV=1;
        for(const auto &t:traces) { maxP=std::max(maxP,t.pressure);maxV=std::max(maxV,t.volume); }
        for(size_t i=1;i<traceCount;++i) {
            const auto &a=traces[(traceWrite+traces.size()-traceCount+i-1)%traces.size()];
            const auto &b=traces[(traceWrite+traces.size()-traceCount+i)%traces.size()];
            line(r.x+8+a.volume/maxV*(r.w-16),r.y+r.h-8-a.pressure/maxP*(r.h-32),
                r.x+8+b.volume/maxV*(r.w-16),r.y+r.h-8-b.pressure/maxP*(r.h-32),.8,color(Orange,.7));
        }
    }
    static simd_float2 local(const VisualPose &pose,float x,float y) {
        const float c=std::cos(pose.angle),s=std::sin(pose.angle);
        return {pose.x+c*x-s*y,pose.y+s*x+c*y};
    }
    // Mesh vertices stay in their original authored coordinates. Only their
    // body transform, metric scale and final screen projection change.
    void mesh(const char *name,const VisualPose &pose,float scale,simd_float2 offset,
            simd_float2 origin,float pixels,simd_float4 tint) {
        const auto *m=meshes.find(name);if(!m)return;
        const float c=std::cos(pose.angle),s=std::sin(pose.angle);
        const auto base=local(pose,offset.x,offset.y);
        const auto project=[&](unsigned short i) {
            const auto &v=m->vertices[i].Pos;
            return simd_float2{origin.x+(base.x+scale*(c*v.x-s*v.y))*pixels,
                origin.y-(base.y+scale*(s*v.x+c*v.y))*pixels};
        };
        for(size_t i=0;i+2<m->indices.size();i+=3)
            triangle(project(m->indices[i]),project(m->indices[i+1]),project(m->indices[i+2]),tint);
    }
    void engine(const State &s,const EngineVisualLayout &layout,const EngineVisualSnapshot &v) {
        const UiRect box{400,0,460,390};panel(box);
        text("ENGINE CUTAWAY",411,9);char label[80];
        button(RoadView,"ROAD [V]",s);
        std::snprintf(label,sizeof(label),"LAYER %d / %d",s.layer+1,layout.maxLayer+1);right(label,773,14);
        button(LayerBack,"<",s);button(LayerNext,">",s);
        if(!layout.cylinderCount || !v.block) { centered(s.loading ? "LOADING ENGINE..." : "ENGINE UNAVAILABLE",630,185,1);return; }
        float minX=-.07f,maxX=.07f,minY=-.085f,maxY=.1f;
        for(int i=0;i<layout.bankCount;++i) {
            const auto &b=layout.banks[i];const VisualPose p{b.x,b.y,b.angle};
            for(float x:{-b.bore*.85f,b.bore*.85f})for(float y:{0.f,b.deck+b.chamberHeight+b.bore*1.9f}) {
                const auto q=local(p,x,y);minX=std::min(minX,q.x);maxX=std::max(maxX,q.x);minY=std::min(minY,q.y);maxY=std::max(maxY,q.y);
            }
        }
        const float pixels=std::min(426/(maxX-minX),320/(maxY-minY));
        const simd_float2 origin{630-(minX+maxX)*pixels/2,211+(minY+maxY)*pixels/2};
        const auto screen=[&](simd_float2 p) {return simd_float2{origin.x+p.x*pixels,origin.y-p.y*pixels};};
        const auto worldLine=[&](simd_float2 a,simd_float2 b,float width,simd_float4 tint) {
            a=screen(a);b=screen(b);line(a.x,a.y,b.x,b.y,width*pixels,tint);
        };
        // Rear journals fade into the background, as in the original viewer.
        for(int layer=layout.maxLayer;layer>=s.layer;--layer) {
            const float alpha=std::pow(.28f,float(layer-s.layer));
            for(int j=0;j<layout.crankCount;++j) {
                const auto &c=layout.cranks[j];if(layer>=c.journals)continue;
                auto pose=v.cranks[j];pose.angle+=c.journalAngles[layer];
                mesh("Crankshaft",pose,c.radius,{},origin,pixels,color(0xA8A8A8,alpha));
            }
            for(int i=0;i<layout.cylinderCount;++i) {
                const auto &c=layout.cylinders[i];if(c.layer!=layer)continue;
                const auto &b=layout.banks[c.bank];const auto &p=v.cylinders[i];const float radius=layout.cranks[c.crank].radius;
                const auto tint=color(c.bank%2 ? 0xDDDDDD : 0xEEEEEE,alpha);
                mesh("ConnectingRod",p.rod,radius,{0,c.rodBig},origin,pixels,tint);
                worldLine(local(p.rod,0,c.rodBig+radius*.6f),local(p.rod,0,c.rodLittle),radius*.5f,tint);
                mesh("Piston",p.piston,b.bore/2,{0,-c.compression-c.wrist},origin,pixels,color(Ink,alpha));
                auto pin=screen(local(p.piston,0,c.wrist));circle(pin.x,pin.y,b.bore*.075f*pixels,color(Panel,alpha));
            }
        }
        for(int j=0;j<layout.crankCount;++j) {
            mesh("CrankSnout",v.cranks[j],layout.cranks[j].radius,{},origin,pixels,color(0x999999));
            mesh("CrankSnoutThreads",v.cranks[j],layout.cranks[j].radius,{},origin,pixels,color(0x666666));
        }
        for(int j=0;j<layout.bankCount;++j) {
            const auto &b=layout.banks[j];int front=-1;
            for(int i=0;i<layout.cylinderCount;++i) {
                const auto &c=layout.cylinders[i];
                if(c.bank==j && c.layer>=s.layer && (front<0 || c.layer<layout.cylinders[front].layer))front=i;
            }
            if(front<0)continue;
            const auto &c=v.cylinders[front];const VisualPose bank{b.x,b.y,b.angle};const float scale=b.bore/2;
            const float top=b.deck+b.chamberHeight,wall=b.bore*.1f,side=b.bore/2+wall*.75f;
            for(float x:{-side,side})worldLine(local(bank,x,top),local(bank,x,(1-b.displayDepth)*b.deck),wall,color(Pink));
            const auto headOrigin=local(bank,0,top);const VisualPose head{headOrigin.x,headOrigin.y,b.angle};
            if(c.lit && c.flameX>0 && c.flameY>0) {
                const float w=std::min(scale,c.flameX),h=std::min(b.deck*.8f,c.flameY);
                auto a=screen(local(head,-w,0)),bb=screen(local(head,w,0)),cc=screen(local(head,w,-h)),d=screen(local(head,-w,-h));
                triangle(a,bb,cc,color(Orange,.28));triangle(a,cc,d,color(Orange,.28));
            }
            mesh("CylinderHead",head,scale,{},origin,pixels,color(Pink));
            for(int valve=0;valve<2;++valve) {
                const float side=(b.flip ? .5f : -.5f)*(valve ? -1 : 1)*scale;
                const float lift=valve ? c.exhaustLift : c.intakeLift;
                const unsigned tint=valve ? Yellow : Blue;
                mesh("Valve",head,scale,{side,-lift},origin,pixels,color(tint));
                const float roller=.00762f;const auto rollerPoint=screen(local(head,side,1.99f*scale-lift));
                circle(rollerPoint.x,rollerPoint.y,(roller+.001f)*pixels,color(Panel));
                circle(rollerPoint.x,rollerPoint.y,roller*pixels,color(tint));
                circle(rollerPoint.x,rollerPoint.y,roller*.25f*pixels,color(Panel));
                const float base=valve ? b.exhaustBaseRadius : b.intakeBaseRadius;
                const auto camOrigin=local(head,side,1.99f*scale+roller+base);
                const auto center=screen(camOrigin);
                const float angle=b.angle+(valve ? c.exhaustCamAngle : c.intakeCamAngle);
                const auto &profile=valve ? b.exhaustCam : b.intakeCam;
                const VisualPose pose{camOrigin.x,camOrigin.y,angle};
                for(int k=0;k<64;++k) {
                    const int n=(k+1)%64;
                    triangle(center,screen(local(pose,profile[k].x,profile[k].y)),
                        screen(local(pose,profile[n].x,profile[n].y)),color(tint));
                }
                circle(center.x,center.y,roller*.25f*pixels,color(Panel));
            }
        }
        text("[ / ] CHANGE CYLINDER LAYER",411,371,0,Dim);
        right("LIVE PHYSICS",849,371,0,Dim);
    }
    void road(const State &s,const EngineVisualLayout &layout,const EngineVisualSnapshot &v,float rpm,float fps) {
        (void)layout;
        const auto &game=roadScene.carPose();
        const bool city=game.world==DrivingWorld::City;
        const UiRect view{0,0,Width,Height};
        const auto top=color(0x21374E),bottom=color(0xE3B38C);
        // Smooth vertex-color gradient, cached geometry and a single draw.
        quad(view,top);
        const auto begin=vertices.size()-6;
        vertices[begin+2].color=bottom;vertices[begin+4].color=bottom;vertices[begin+5].color=bottom;
        const auto sun=roadScene.sunPosition(view.w,view.h);
        if(sun.visible) {
            const float x=float(sun.x),y=float(sun.y),radius=float(sun.radius),halo=radius*18;
            if(s.effects)quad({x-halo,y-halo,halo*2,halo*2},color(0xF1C58D,.4f),2,{-1,-1},{1,1});
            circle(x,y,radius,color(0xFFE9C4));
        }
        roadScene.draw(view,
            [&](auto a,auto b,auto c,auto tint,unsigned kind) {
                for(auto p:{a,b,c})vertices.push_back({{p.p.x,p.p.y},p.uv,tint,kind,p.p.z});
            });
        roadEnd=vertices.size();uiDepth=0;
        for(int i=0;i<24;++i) {
            quad({0,float(i*5),Width,5},color(0x091321,.45f*(1-i/24.f)));
            quad({0,Height-240+i*10.f,Width,10},color(0x091321,.65f*i/24));
        }
        text("ENGINE SIMULATOR  /  DRIVE",26,24,0,0xD1DDE4);
        text(s.title.data(),24,44,2);text(city ? "PORTSIDE CITY / FREE ROAM" : "ARCADE DRIVE / 3 LAP TIME TRIAL",26,74,0,0xD1DDE4);
        if(s.carBody==State::CarBody::Concept)text("CONCEPT BODY",26,94,0,Dim);
        button(Library,"ENGINES [E]",s);button(RoadView,"DASH [V]",s);
        button(WorldView,s.world==DrivingWorld::City ? "CITY [T]" : "CIRCUIT [T]",s,true);
        char label[96];std::snprintf(label,sizeof(label),"%.0f FPS  /  %s",fps,s.missing ? "AUDIO GAPS" : "AUDIO OK");right(label,1254,68,0,s.missing ? Red : 0xD1DDE4);
        std::snprintf(label,sizeof(label),"%.2f KM",v.vehicleDistance/1000);right(label,1254,89,0,0xD1DDE4);
        rounded({990,116,266,180},8,color(0x0C1721,.68f));
        const auto &course=roadScene.course();const auto &path=course.points();
        DrivingPoint lo=path.front(),hi=lo;
        if(city) {lo={-DrivingCity::Extent,-DrivingCity::Extent};hi={DrivingCity::Extent,DrivingCity::Extent};}
        else for(const auto &p:path) {lo.x=std::min(lo.x,p.x);lo.z=std::min(lo.z,p.z);hi.x=std::max(hi.x,p.x);hi.z=std::max(hi.z,p.z);}
        const double mapScale=std::min(234/(hi.x-lo.x),148/(hi.z-lo.z));
        auto map=[&](DrivingPoint p) {return simd_float2{1123+float((p.x-(lo.x+hi.x)*.5)*mapScale),206-float((p.z-(lo.z+hi.z)*.5)*mapScale)};};
        if(city) {
            for(const auto &building:drivingCity().buildings()) {
                const auto a=map(building.lo),b=map(building.hi);
                quad({a.x,b.y,b.x-a.x,a.y-b.y},color(0xA2B4BD,.38f));
            }
            for(int i=-DrivingCity::GridRadius;i<=DrivingCity::GridRadius;++i) {
                const double at=i*DrivingCity::Block;const float width=i==0 ? 4 : 2;
                const auto a=map({at,-DrivingCity::Extent}),b=map({at,DrivingCity::Extent});
                const auto c=map({-DrivingCity::Extent,at}),d=map({DrivingCity::Extent,at});
                line(a.x,a.y,b.x,b.y,width,color(Ink,.6f));line(c.x,c.y,d.x,d.y,width,color(Ink,.6f));
            }
        } else for(int i=0;i<DrivingCourse::Segments;++i) {const auto a=map(path[i]),b=map(path[i+1]);line(a.x,a.y,b.x,b.y,3,color(Ink,.45f));}
        const auto checkpoint=map(city ? DrivingCity::Destinations[game.cityStops%DrivingCity::Destinations.size()] : course.at(game.nextCheckpoint*course.length()/8).point);
        circle(checkpoint.x,checkpoint.y,5,color(city ? 0x65DAB0 : Blue));const auto position=map({game.x,game.z});
        const float heading=float(game.yaw),c=std::cos(heading),si=std::sin(heading);
        triangle({position.x+si*7,position.y-c*7},{position.x-c*4-si*3,position.y-si*4+c*3},
            {position.x+c*4-si*3,position.y+si*4+c*3},color(game.offroad ? Yellow : Ink));
        auto timeLabel=[&](double seconds) {std::snprintf(label,sizeof(label),"%02d:%05.2f",int(seconds)/60,std::fmod(seconds,60));};
        if(city) {
            text("CITY TOUR",26,114,0,Dim);std::snprintf(label,sizeof(label),"%u",game.cityStops);text(label,26,135,3);
            text("DISTANCE",154,114,0,Dim);std::snprintf(label,sizeof(label),"%.2f KM",game.cityDistance/1000);text(label,154,137,2);
            text(DrivingCity::district({game.x,game.z}),26,178,0,0xD1DDE4);
            if(!game.recovering) {
                rounded({495,111,290,92},8,color(0x0C1721,.82f));
                centered("NEXT CITY SPOT",640,123,1,0x65DAB0);
                std::snprintf(label,sizeof(label),"%.0f M",game.destinationDistance);centered(label,640,148,2);
                centered("Explore or follow the green marker",640,181,0,0xD1DDE4);
            }
        } else {
        text("LAP",26,114,0,Dim);std::snprintf(label,sizeof(label),"%u / 3",std::min(3u,game.laps+1));text(label,26,135,3);
        text("CURRENT",154,114,0,Dim);timeLabel(game.lapSeconds);text(label,154,137,2);
        text("BEST",330,114,0,Dim);if(game.bestLap>0)timeLabel(game.bestLap);else std::snprintf(label,sizeof(label),"--:--.--");text(label,330,137,2);
        std::snprintf(label,sizeof(label),"CHECKPOINT %u / 8",game.nextCheckpoint);text(label,26,178,0,0xD1DDE4);
        }
        if(game.cornerDirection && !game.recovering && !game.finished && !game.wrongWay) {
            const bool slow=game.speed>game.cornerSpeed+1.5;
            const bool brakeNow=slow && game.cornerDeceleration>6;
            const uint32_t cue=brakeNow ? Red : slow ? Yellow : Ink;
            rounded({495,111,290,92},8,color(0x0C1721,.82f));
            centered(brakeNow ? "BRAKE NOW" : game.cornerDirection>0 ? "RIGHT TURN AHEAD" : "LEFT TURN AHEAD",640,123,1,cue);
            std::snprintf(label,sizeof(label),"%.0f MPH  /  %.0f M",game.cornerSpeed/.44704,game.cornerDistance);
            centered(label,640,148,2,cue);
            centered(brakeNow ? "Slow before turning" : slow ? "Lift, then brake for the corner" : "Suggested corner speed",640,181,0,0xD1DDE4);
        }
        if(game.wrongWay || game.recovering || game.finished) {
            rounded({435,165,410,82},8,color(0x0C1721,.85f));
            centered(s.world!=game.world ? "SWITCHING MAP" : game.finished ? "TIME TRIAL COMPLETE" : game.recovering ? "RECOVERING CAR" : "WRONG WAY",640,180,2,game.wrongWay ? Red : Ink);
            if(game.finished) {timeLabel(game.raceSeconds);centered(label,640,211,1);}
            else centered(game.recovering ? (city ? "Returning to a nearby street" : "Returning to the last checkpoint") : "Turn around to continue your lap",640,214,0);
        }
        if(game.impact>.05)quad(view,color(Red,float(game.impact*.13)));
        if(s.loading || !s.ready)centered(s.loading ? "LOADING ENGINE..." : "ENGINE UNAVAILABLE",640,180,2);
        const float cx=1121,cy=667,radius=109,start=140*Pi/180,span=260*Pi/180;
        const float limit=std::ceil(s.redline/1000)*1000,value=std::clamp(rpm/std::max(1000.f,limit),0.f,1.f);
        arc(cx,cy,radius,start,start+span,5,color(Ink,.18f));
        arc(cx,cy,radius,start,start+span*value,5,color(value>.85f ? Red : Ink));
        arc(cx,cy,radius,start+span*.9f,start+span,5,color(Red,.7f));
        const int marks=int(limit/1000);
        for(int i=0;i<=marks;++i) {
            const float a=start+span*i/marks;
            line(cx+(radius-12)*std::cos(a),cy+(radius-12)*std::sin(a),cx+(radius-4)*std::cos(a),cy+(radius-4)*std::sin(a),1.5,color(Ink,.85f));
            std::snprintf(label,sizeof(label),"%d",i);centered(label,cx+(radius-25)*std::cos(a),cy+(radius-25)*std::sin(a)-7,0);
        }
        std::snprintf(label,sizeof(label),"%.0f",float(std::abs(game.speed/.44704)));centered(label,cx,623,4);
        centered("MPH",cx,696,1,0xD1DDE4);
        std::snprintf(label,sizeof(label),"%.0f RPM",std::max(0.f,rpm));centered(label,cx,761,1);
        text("GEAR",936,684,0,0xD1DDE4);
        if(v.gear==-2)std::snprintf(label,sizeof(label),"R");else if(v.gear<0)std::snprintf(label,sizeof(label),"N");else std::snprintf(label,sizeof(label),"%d",v.gear+1);
        centered(label,956,702,3,s.engine.shifting ? Yellow : Ink);
        centered(s.engine.drive ? "AUTO" : "MANUAL",956,747,0,0xD1DDE4);
        text("THROTTLE",24,633,0,0xD1DDE4);
        const auto throttle=bounds(Throttle,true);const float x=throttle.x+7,y=throttle.y+17,w=throttle.w-14;
        line(x,y,x+w,y,3,color(Ink,.2f));line(x,y,x+w*std::sqrt(std::clamp(s.throttle,0.f,1.f)),y,3,color(Ink));
        circle(x+w*std::sqrt(std::clamp(s.throttle,0.f,1.f)),y,4,color(Ink));
        button(Drive,s.engine.drive ? "DRIVE [G]" : "NEUTRAL [G]",s,s.engine.drive);
        text(s.engine.brake>0 ? "BRAKING" : s.engine.shifting ? "SHIFTING" : !s.engine.ignition ? "ENGINE OFF" : s.engine.gear==-2 ? "REVERSE / W TO DRIVE FORWARD" : "W GAS / S BRAKE + REVERSE",172,700,0,s.engine.brake>0 ? Red : Ink);
        button(Start,s.ignitionRequested ? "STOP [X]" : "START [X]",s);
        button(Rev,"GAS [W]",s,s.revHeld);button(Brake,"BRAKE SPACE",s,s.brakeHeld);
        button(Drift,"DRIFT [SHIFT]",s,s.driftHeld);button(Mute,s.muted ? "UNMUTE" : "MUTE [M]",s,s.muted);
        button(RecoverCar,"RECOVER [C]",s);button(RestartRace,city ? "RESET [BKSP]" : "NEW RUN [BKSP]",s);
        if(game.offroad)text(city ? "PAVEMENT / OPEN GROUND" : "ON THE SHOULDER",24,605,0,Yellow);
        if(!game.wrongWay && !game.recovering && !game.finished &&
            (game.driftScore>1 || (game.lastDriftScore>1 && game.time-game.driftEndedAt<2))) {
            const bool active=game.driftScore>1;
            rounded({500,235,280,73},8,color(0x0C1721,.8f));
            centered(active ? "DRIFT" : "DRIFT BANKED",640,244,0,Yellow);
            std::snprintf(label,sizeof(label),"+ %.0f",active ? game.driftScore : game.lastDriftScore);centered(label,640,264,2,Yellow);
        }
        if(game.totalDriftScore>1) {
            std::snprintf(label,sizeof(label),"DRIFT TOTAL %.0f / BEST %.0f",game.totalDriftScore,game.bestDriftScore);text(label,26,203,0,Yellow);
        }
        line(398,668,506,668,2,color(Ink,.25f));circle(452+float(game.steeringInput)*54,668,5,color(Blue));text("STEERING",399,633,0,0xD1DDE4);
    }
    void draw(const State &s,const EngineVisualLayout &layout,const EngineVisualSnapshot &v,
            double dt,float fps,float cpuMs,float gpuMs,float displayedRpm) {
        vertices.clear();uiDepth=1;
        if(s.roadView) { road(s,layout,v,displayedRpm,fps);return; }
        quad({0,0,Width,Height},color(Panel));char b[192];
        for(auto &value:firing)value*=std::exp(-dt/.075);
        panel({0,0,400,80});panel({0,0,62,80});
        // Preserve the community fork's existing artwork and attribution.
        const auto *logo=meshes.find("Logo");
        if(logo) {
            float x0=1e6,y0=1e6,x1=-1e6,y1=-1e6;
            for(const auto &vertex:logo->vertices) {x0=std::min(x0,vertex.Pos.x);y0=std::min(y0,vertex.Pos.y);x1=std::max(x1,vertex.Pos.x);y1=std::max(y1,vertex.Pos.y);}
            const float scale=std::min(42/(x1-x0),62/(y1-y0));
            mesh("Logo",{},1,{}, {31-(x0+x1)*scale/2,40+(y0+y1)*scale/2},scale,color(Ink));
        }
        text("ENGINE SIMULATOR",73,19,2);text("ANGETHEGREAT / COMMUNITY MAC PORT",74,49,0,Dim);
        panel({0,80,400,44});text(s.title.data(),9,88,1);
        std::snprintf(b,sizeof(b),"%.0f CC   %d CYL",layout.displacementLiters*1000,layout.cylinderCount);right(b,391,108,0,Dim);
        panel({0,124,400,36});
        const char *status=s.loading ? "LOADING ENGINE..." : !s.ready ? "AUDIO UNAVAILABLE" : !s.engine.ignition ? "IGNITION OFF - SPACE TO START"
            : s.engine.cranking ? "STARTER ENGAGED" : s.engine.rpm<200 ? "ENGINE STALLED - RESTART" : "IGNITION ON / STARTER DISENGAGED";
        text(status,9,136,0,s.engine.ignition ? Ink : Dim);
        const float values[]={s.muted ? 0 : s.volume,s.exhaust,v.highFrequency*100,v.lowNoise,s.roughness,v.levelerGain};
        const char *labels[]={"VOL.","CONV.","+ HF","~ LF","~ HF","LEVEL"};
        const Control controls[]={Volume,Exhaust,HighFrequency,LowNoise,Roughness,None};
        for(int i=0;i<6;++i) {
            const float value=std::isfinite(values[i]) ? values[i] : 0;
            std::snprintf(b,sizeof(b),i==5 ? "%.1f" : "%.0f%%",i==5 ? value : value*100);
            gauge({i*400.f/6,160,400.f/6,90},labels[i],value,0,i==5 ? 30 : 1,b);
            if(controls[i]!=None)sliderTrack(controls[i],value,s);
        }
        std::snprintf(b,sizeof(b),"%.1f",fps);gauge({0,250,133.33f,119},"FPS",fps,0,120,b,true);
        std::snprintf(b,sizeof(b),"%.2f MS",cpuMs);gauge({133.33f,250,133.34f,119},"CPU FRAME",cpuMs,0,16.67,b,true);
        std::snprintf(b,sizeof(b),"%.2f MS",gpuMs);gauge({266.67f,250,133.33f,119},"GPU FRAME",gpuMs,0,16.67,b);
        std::snprintf(b,sizeof(b),"%.1f MS",s.engine.queuedMs);gauge({0,369,133.33f,119},"AUDIO QUEUE",s.engine.queuedMs,0,100,b,true);
        const float cpu=s.engine.simulatedSeconds>0 ? s.engine.cpuSeconds/s.engine.simulatedSeconds*100 : 0;
        std::snprintf(b,sizeof(b),"%.1f%%",cpu);gauge({133.33f,369,133.34f,119},"AUDIO CPU",cpu,0,100,b,true);
        std::snprintf(b,sizeof(b),"%.0f HZ",v.physicsHz);gauge({266.67f,369,133.33f,119},"FREQUENCY",v.physicsHz,0,10000,b);
        panel({0,488,133.33f,120});text("IGNITION",8,506);text("STARTER",8,546);
        line(96,511,122,511,2,color(s.engine.ignition ? Red : 0x452020));
        line(96,551,122,551,2,color(s.engine.cranking ? Red : 0x452020));
        button(Dyno,v.dyno ? "DYNO: ON" : "DYNO: OFF",s,v.dyno);
        std::snprintf(b,sizeof(b),"%.0f%%",v.clutch*100);gauge({133.33f,488,133.34f,120},"CLUTCH",v.clutch,0,1,b);sliderTrack(Clutch,v.clutch,s);
        panel({266.67f,488,133.33f,120},s.engine.drive ? "AUTOMATIC" : "GEAR");
        if(v.gear==-2)std::snprintf(b,sizeof(b),"R");
        else if(v.gear<0)std::snprintf(b,sizeof(b),"N");
        else std::snprintf(b,sizeof(b),s.engine.drive ? "D%d" : "%d",v.gear+1);
        centered(b,333,513,3,s.engine.shifting ? Yellow : Ink);
        button(GearDown,"-",s);button(GearUp,"+",s);button(Brake,"BRAKE S",s,s.brakeHeld);
        std::snprintf(b,sizeof(b),"%.0f RPM",v.dynoRpm);gauge({0,608,133.33f,120},"DYNO SPEED",v.dynoRpm,0,s.redline,b);sliderTrack(DynoSpeed,(s.dynoRpm-500)/std::max(1.f,s.redline-500),s);
        std::snprintf(b,sizeof(b),"%.0f LB-FT",v.torque/1.35581795f);gauge({133.33f,608,133.34f,120},"TORQUE",v.torque,0,1000,b);
        std::snprintf(b,sizeof(b),"%.0f HP",v.power/745.699872f);gauge({266.67f,608,133.33f,120},"HORSEPOWER",v.power/745.699872f,0,1000,b);
        engine(s,layout,v);
        plot({400,390,460,115},"TOTAL EXHAUST FLOW",0);
        pcm({400,505,153.33f,111.5f},s);plot({553.33f,505,153.34f,111.5f},"CYLINDER PRESSURE",1);
        plot({706.67f,505,153.33f,111.5f},"VALVE LIFT",3);
        plot({400,616.5f,153.33f,111.5f},"CYLINDER VOLUME",2,Blue);pv({553.33f,616.5f,153.34f,111.5f});
        plot({706.67f,616.5f,153.33f,111.5f},"EXHAUST PULSES",0,Blue);
        std::snprintf(b,sizeof(b),"%.0f RPM",std::max(0.f,displayedRpm));gauge({860,0,210,195},"ENGINE SPEED",displayedRpm,0,std::ceil(s.redline/1000)*1000,b,true,2);
        std::snprintf(b,sizeof(b),"%.0f MPH",v.vehicleSpeed/.44704f);gauge({860,195,210,195},"VEHICLE SPEED",v.vehicleSpeed/.44704f,0,200,b,false,2);
        panel({1070,0,210,390},"IGNITION");
        const int cols=layout.cylinderCount<=6 ? 2 : 3,rows=std::max(1,(layout.cylinderCount+cols-1)/cols);
        const float cellW=194.f/cols,cellH=std::min(82.f,330.f/rows),top=36+(330-cellH*rows)/2;
        for(int i=0;i<layout.cylinderCount;++i) {
            const float x=1078+cellW*(i%cols+.5f),y=top+cellH*(i/cols+.5f),radius=std::min(cellW,cellH)*.36f;
            if(s.effects && firing[i]>.02f)quad({x-radius*1.8f,y-radius*1.8f,radius*3.6f,radius*3.6f},color(Orange,firing[i]*.25f),2,{-1,-1},{1,1});
            circle(x,y,radius+3,color(0x383B3D));circle(x,y,radius,color(0x222527));
            circle(x,y,radius,color(0xFFBC63,firing[i]));
            std::snprintf(b,sizeof(b),"%d",i+1);centered(b,x,y-7,1,Ink);
        }
        panel({860,390,210,170},"THROTTLE");
        line(922,425,922,510,1.5,color(Ink));line(1008,425,1008,510,1.5,color(Ink));
        const float plate=v.throttleAngle;
        line(965-39*std::cos(plate),467+39*std::sin(plate),965+39*std::cos(plate),467-39*std::sin(plate),2,color(Ink));circle(965,467,2.5,color(Panel));
        std::snprintf(b,sizeof(b),"%.1f%%",s.engine.throttle*100);centered(b,965,505);
        centered("DRAG TO SET / HOLD R",965,522,0,Dim);
        const auto throttle=bounds(Throttle);
        const float tx=throttle.x+7,tw=throttle.w-14,ty=throttle.y+17;
        line(tx,ty,tx+tw,ty,2,color(Edge));
        const float handle=tx+tw*std::sqrt(std::clamp(s.throttle,0.f,1.f));
        line(tx,ty,handle,ty,3,color(Blue));circle(handle,ty,4,color(Ink));
        const float manifold=(v.manifoldPressure-101325)/3386.389f;
        std::snprintf(b,sizeof(b),"%.1f INHG",manifold);gauge({1070,390,210,113},"MANIFOLD PRESSURE",manifold,-30,10,b,true);
        const float scfm=v.intakeFlow/float(units::scfm);
        std::snprintf(b,sizeof(b),"%.1f",scfm);gauge({1070,503,210,112},"AIR SCFM",scfm,0,1000,b);
        const double ideal=.5*101325*(layout.displacementLiters*.001)/(8.314462618*298.15)*std::max(0.f,displayedRpm)/60;
        const float ve=ideal>.0001 ? std::clamp(float(v.intakeFlow/ideal*100),0.f,999.f) : 0;
        std::snprintf(b,sizeof(b),"%.1f%%",ve);gauge({1070,615,210,113},"VOLUMETRIC EFF.",ve,0,150,b,true);
        std::snprintf(b,sizeof(b),"%.1f",std::isfinite(v.afr) ? v.afr : 0);gauge({860,560,105,84},"IN. AFR",v.afr,0,30,b);
        std::snprintf(b,sizeof(b),"%.1f%%",v.exhaustO2*100);gauge({860,644,105,84},"EX. O2",v.exhaustO2*100,0,25,b);
        panel({965,560,105,168},"FUEL");std::snprintf(b,sizeof(b),"%.3f L",v.fuelLiters);centered(b,1017.5f,618,1);
        std::snprintf(b,sizeof(b),"%.3f GAL",v.fuelLiters/3.785411784f);centered(b,1017.5f,648,0,Dim);
        centered("CONSUMED",1017.5f,682,0,Dim);
        panel({0,728,1280,72});
        button(Start,s.ignitionRequested ? "STOP [SPACE]" : "START [SPACE]",s,false,!s.ignitionRequested);
        button(Rev,"HOLD [R]",s,s.revHeld);button(Idle,"IDLE [I]",s);button(Mute,s.muted ? "UNMUTE" : "MUTE [M]",s,s.muted);
        button(Reset,"RESET SOUND",s);button(Effects,s.effects ? "GLOW ON" : "GLOW OFF",s,s.effects);
        button(Uncapped,s.uncapped ? "UNCAPPED [U]" : "SYNC [U]",s,s.uncapped);
        std::snprintf(b,sizeof(b),"ENGINE LIBRARY [E] / %d",s.engineCount);button(Library,b,s);
        button(Supra,"SUPRA [1]",s,s.preset==0);button(Ls,"LS [2]",s,s.preset==1);
        button(Drive,s.engine.drive ? "DRIVE [A]" : "AUTO DRIVE [A]",s,s.engine.drive);
        text(s.silent ? "SILENT AUTOMATED TEST" : s.output.data(),10,780,0,s.silent ? Orange : Dim);
        text(s.notice.data(),360,780,0,Dim);
        right(s.writeErrors ? "OUTPUT ERROR" : s.missing ? "AUDIO GAPS" : "AUDIO OK",1270,780,0,s.missing || s.writeErrors ? Red : Blue);
    }

};

bool saveTexture(id<MTLBuffer> buffer,int width,int height,int stride,const char *path) {
    NSBitmapImageRep *bitmap=[[NSBitmapImageRep alloc] initWithBitmapDataPlanes:nil pixelsWide:width pixelsHigh:height
        bitsPerSample:8 samplesPerPixel:4 hasAlpha:YES isPlanar:NO colorSpaceName:NSDeviceRGBColorSpace bytesPerRow:width*4 bitsPerPixel:32];
    const auto *source=static_cast<const unsigned char *>(buffer.contents);
    auto *target=bitmap.bitmapData;
    for(int y=0;y<height;++y) for(int x=0;x<width;++x) {
        const auto *p=source+y*stride+x*4; auto *q=target+(y*width+x)*4;
        q[0]=p[2];q[1]=p[1];q[2]=p[0];q[3]=255;
    }
    return [[bitmap representationUsingType:NSBitmapImageFileTypePNG properties:@{}]
        writeToFile:[NSString stringWithUTF8String:path] atomically:YES];
}
}

struct SoundMetalRenderer::Impl {
    CAMetalLayer *layer=nil;
    id<MTLDevice> device=nil;
    id<MTLCommandQueue> queue=nil;
    id<MTLRenderPipelineState> pipeline=nil;
    id<MTLDepthStencilState> depthState=nil;
    id<MTLTexture> depthTexture=nil;
    id<MTLTexture> atlas=nil, target=nil, foliage=nil;
    std::array<id<MTLBuffer>,3> buffers;
    dispatch_semaphore_t slots=dispatch_semaphore_create(3);
    dispatch_semaphore_t refresh=dispatch_semaphore_create(0);
    dispatch_queue_t captureQueue=dispatch_queue_create("org.openenginesim.capture",
        dispatch_queue_attr_make_with_qos_class(DISPATCH_QUEUE_SERIAL,QOS_CLASS_UTILITY,0));
    dispatch_group_t captureJobs=dispatch_group_create();
    std::atomic<bool> refreshPending{false};
    DrawList draw;
    SoundCarMetal car;
    DrivingGameWorker game;
    std::atomic<unsigned> renderStallMs{0};
    State state;
    EngineVisualSnapshot visual;
    VehicleVisualMotion vehicleMotion;
    std::string assets;
    std::shared_ptr<SoundSession> session;
    uint64_t sessionGeneration=0,seenGeneration=0;
    std::array<float,512> history{};
    size_t waveWrite=0;
    mutable std::mutex stateMutex;
    std::array<char,1024> capturePath{};
    std::atomic<bool> running{false};
    std::thread thread;
    std::string failure;
    bool offscreen=false;
    std::atomic<uint64_t> frames{0},cpuNs{0},gpuNs{0},errors{0};
    std::atomic<uint64_t> audioBlocksSeen{0},visualBlocksSeen{0},animatedFrames{0},peakVertices{0};
    std::atomic<uint64_t> roadFrames{0},movingRoadFrames{0};
    std::atomic<double> roadDistance{0};
    std::atomic<double> sunX{0},sunY{0};
    std::atomic<bool> sunVisible{false};
    std::array<std::atomic<uint64_t>,256> cpuHist{},gpuHist{};
    std::atomic<unsigned> captured{0};

    bool makeAtlas() {
        std::vector<unsigned char> pixels(AtlasSize*AtlasSize,0);
        CGContextRef context=CGBitmapContextCreate(pixels.data(),AtlasSize,AtlasSize,8,AtlasSize,nullptr,kCGImageAlphaOnly);
        if(!context) return false;
        CGContextSetGrayFillColor(context,1,1);
        CGContextSetShouldAntialias(context,true);
        int x=2,y=2,row=0;
        const float sizes[]={11.5f,13,20,30,64};
        for(int fontIndex=0;fontIndex<5;++fontIndex) {
            // Rasterize native, legible text once into the existing Retina
            // atlas. Frames still draw cached glyph quads in one Metal batch.
            NSFont *native=[NSFont monospacedSystemFontOfSize:sizes[fontIndex]*2
                weight:fontIndex<2 ? NSFontWeightMedium : NSFontWeightSemibold];
            CTFontRef font=(__bridge CTFontRef)native;
            auto &info=draw.fonts[fontIndex]; info.ascent=CTFontGetAscent(font)/2;
            for(UniChar c=32;c<127;++c) {
                CGGlyph glyph=0; CTFontGetGlyphsForCharacters(font,&c,&glyph,1);
                CGSize advance{}; CTFontGetAdvancesForGlyphs(font,kCTFontOrientationHorizontal,&glyph,&advance,1);
                auto &g=info.glyphs[c]; g.advance=advance.width/2;
                if(c==' ') continue;
                CGRect box=CTFontGetBoundingRectsForGlyphs(font,kCTFontOrientationHorizontal,&glyph,nullptr,1);
                box=CGRectIntegral(CGRectInset(box,-2,-2));
                int w=box.size.width,h=box.size.height;
                if(x+w+2>=AtlasSize) { x=2; y+=row+2; row=0; }
                if(y+h+2>=AtlasSize) { CGContextRelease(context);return false; }
                CGPoint p{double(x)-box.origin.x,double(y)-box.origin.y};
                CTFontDrawGlyphs(font,&glyph,&p,1,context);
                g.u0=x/float(AtlasSize);g.v0=y/float(AtlasSize);g.u1=(x+w)/float(AtlasSize);g.v1=(y+h)/float(AtlasSize);
                g.w=w/2.f;g.h=h/2.f;g.left=box.origin.x/2;g.bottom=box.origin.y/2;
                x+=w+2;row=std::max(row,h);
            }
        }
        CGContextRelease(context);
        // Quartz's origin is at the bottom; Metal texture rows start at the top.
        // Store rows in the glyph coordinates used above and reverse each
        // glyph's V coordinates when drawing it into our top-origin interface.
        for(int row=0;row<AtlasSize/2;++row)
            std::swap_ranges(pixels.begin()+row*AtlasSize,pixels.begin()+(row+1)*AtlasSize,
                             pixels.begin()+(AtlasSize-1-row)*AtlasSize);
        MTLTextureDescriptor *desc=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatR8Unorm width:AtlasSize height:AtlasSize mipmapped:NO];
        desc.storageMode=MTLStorageModeShared;desc.usage=MTLTextureUsageShaderRead;
        atlas=[device newTextureWithDescriptor:desc];
        [atlas replaceRegion:MTLRegionMake2D(0,0,AtlasSize,AtlasSize) mipmapLevel:0 withBytes:pixels.data() bytesPerRow:AtlasSize];
        return atlas!=nil;
    }

    unsigned slot=0;
    uint64_t lastFrame=0,rateStart=0,rateFrames=0,rateCpu=0,rateGpu=0;
    float fps=0,cpu=0,gpu=0,rpm=0;

    void renderFrame(id<CAMetalDrawable> drawable) {
        @autoreleasepool {
        State current;std::array<char,1024> capture{};
        std::shared_ptr<SoundSession> audio;
        uint64_t generation;
        { std::lock_guard<std::mutex> lock(stateMutex);current=state;audio=session;generation=sessionGeneration; }
        if(!running || (!current.active && !offscreen))return;
        dispatch_semaphore_wait(slots,DISPATCH_TIME_FOREVER);
        id<MTLTexture> texture=offscreen ? target : drawable.texture;
        if(!texture) { dispatch_semaphore_signal(slots);return; }
        if(depthTexture.width!=texture.width || depthTexture.height!=texture.height) {
            auto descriptor=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float
                width:texture.width height:texture.height mipmapped:NO];
            descriptor.usage=MTLTextureUsageRenderTarget;descriptor.storageMode=MTLStorageModePrivate;
            depthTexture=[device newTextureWithDescriptor:descriptor];
        }
        if(!depthTexture) { ++errors;dispatch_semaphore_signal(slots);return; }
        { std::lock_guard<std::mutex> lock(stateMutex);capture=capturePath;capturePath[0]=0; }
            const uint64_t begin=SDL_GetTicksNS();
            if(generation!=seenGeneration) { history.fill(0);waveWrite=0;rpm=0;visual={};vehicleMotion.reset();draw.resetEngine();visualBlocksSeen=0;seenGeneration=generation; }
            // Read published atomics and the visual queue here, independently
            // of AppKit timers, menus, dragging, or a stalled control thread.
            if(audio) {
                current.engine=audio->snapshot();audioBlocksSeen=current.engine.blocks;
                EngineVisualSnapshot next;
                while(audio->readEngineVisualization(next)) {
                    if(visual.block && std::hypot(next.cylinders[0].piston.x-visual.cylinders[0].piston.x,
                        next.cylinders[0].piston.y-visual.cylinders[0].piston.y)>.000001)++animatedFrames;
                    visual=next;draw.accept(next);vehicleMotion.accept(next.simulatedSeconds,next.vehicleDistance,next.vehicleSpeed);
                }
                visualBlocksSeen=visual.block;
                const auto statistics=audio->statistics();
                current.missing=statistics.silenceFrames;current.writeErrors=statistics.writeErrors;
                SdlAudioOutput::VisualSamples samples;
                while(audio->readVisualization(samples))for(int i=0;i<samples.count;++i) {
                    history[waveWrite]=samples.samples[i];waveWrite=(waveWrite+1)%history.size();
                }
            }
            for(size_t i=0;i<history.size();++i)current.waveform[i]=history[(waveWrite+i)%history.size()];
            const double dt=std::min(.1,(begin-lastFrame)/1e9);lastFrame=begin;
            rpm+=(current.engine.rpm-rpm)*(1-std::exp(-dt/0.035));
            const auto count=frames.load();
            if(begin-rateStart>=500000000) {
                fps=(count-rateFrames)*1e9/(begin-rateStart);
                const auto c=cpuNs.load(),g=gpuNs.load();
                cpu=(c-rateCpu)/1e6/std::max(uint64_t(1),count-rateFrames);
                gpu=(g-rateGpu)/1e6/std::max(uint64_t(1),count-rateFrames);
                rateStart=begin;rateFrames=count;rateCpu=c;rateGpu=g;
            }
            const EngineVisualLayout empty;
            auto presented=visual;const auto movement=vehicleMotion.advance(dt);
            presented.vehicleDistance=movement.distance;presented.vehicleSpeed=movement.speed;
            draw.roadScene.update(game.presented(),dt);
            const auto sun=draw.roadScene.sunPosition(Width,Height);
            sunX=sun.x;sunY=sun.y;sunVisible=sun.visible;
            draw.draw(current,audio ? audio->visualLayout() : empty,presented,dt,fps,cpu,gpu,rpm);
            if(current.roadView) {
                ++roadFrames;if(presented.vehicleDistance>roadDistance.load()+.00001)++movingRoadFrames;
            }
            roadDistance=presented.vehicleDistance;
            peakVertices=std::max(peakVertices.load(),uint64_t(draw.vertices.size()));
            if(draw.vertices.size()>MaxVertices) { ++errors;dispatch_semaphore_signal(slots);return; }
            const size_t bytes=draw.vertices.size()*sizeof(Vertex);
            if(buffers[slot].length<bytes) {
                size_t capacity=131072;while(capacity<draw.vertices.size())capacity*=2;
                // Geometry growth happens only on the renderer. Submitted Metal
                // commands retain their old buffers until the GPU completes.
                buffers[slot]=[device newBufferWithLength:capacity*sizeof(Vertex) options:MTLResourceStorageModeShared];
            }
            if(!buffers[slot]) {++errors;dispatch_semaphore_signal(slots);return;}
            std::memcpy(buffers[slot].contents,draw.vertices.data(),draw.vertices.size()*sizeof(Vertex));
            id<MTLCommandBuffer> command=[queue commandBuffer];
            MTLRenderPassDescriptor *pass=[MTLRenderPassDescriptor renderPassDescriptor];
            pass.colorAttachments[0].texture=texture;
            pass.colorAttachments[0].loadAction=MTLLoadActionClear;
            pass.colorAttachments[0].storeAction=MTLStoreActionStore;
            pass.colorAttachments[0].clearColor=MTLClearColorMake(0.043,0.063,0.080,1);
            pass.depthAttachment.texture=depthTexture;pass.depthAttachment.clearDepth=1;
            pass.depthAttachment.loadAction=MTLLoadActionClear;pass.depthAttachment.storeAction=MTLStoreActionDontCare;
            id<MTLRenderCommandEncoder> encoder=[command renderCommandEncoderWithDescriptor:pass];
            [encoder setRenderPipelineState:pipeline];
            [encoder setDepthStencilState:depthState];
            [encoder setVertexBuffer:buffers[slot] offset:0 atIndex:0];
            [encoder setFragmentTexture:atlas atIndex:0];
            [encoder setFragmentTexture:foliage atIndex:1];
            if(current.roadView) {
                [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:draw.roadEnd];
                car.draw(encoder,draw.roadScene,draw.roadScene.carPose().wheelDistance,current.engine.brake>0,current.carBody);
                [encoder setRenderPipelineState:pipeline];[encoder setVertexBuffer:buffers[slot] offset:0 atIndex:0];
                [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:draw.roadEnd vertexCount:draw.vertices.size()-draw.roadEnd];
            } else [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:draw.vertices.size()];
            [encoder endEncoding];
            id<MTLBuffer> readback=nil;
            const int width=int(texture.width),height=int(texture.height),stride=((width*4+255)/256)*256;
            if(capture[0]) {
                readback=[device newBufferWithLength:stride*height options:MTLResourceStorageModeShared];
                id<MTLBlitCommandEncoder> blit=[command blitCommandEncoder];
                [blit copyFromTexture:texture sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0,0,0)
                    sourceSize:MTLSizeMake(width,height,1) toBuffer:readback destinationOffset:0
                    destinationBytesPerRow:stride destinationBytesPerImage:stride*height];
                [blit endEncoding];
            }
            if(drawable)[command presentDrawable:drawable];
            const uint64_t elapsed=SDL_GetTicksNS()-begin;
            cpuNs+=elapsed; ++cpuHist[std::min<uint64_t>(255,elapsed/50000)];
            Impl *owner=this;
            [command addCompletedHandler:^(id<MTLCommandBuffer> completed) {
                const uint64_t gpuTime=std::max(0.0,completed.GPUEndTime-completed.GPUStartTime)*1e9;
                owner->gpuNs+=gpuTime; ++owner->gpuHist[std::min<uint64_t>(255,gpuTime/50000)];
                if(completed.status==MTLCommandBufferStatusError) ++owner->errors;
                ++owner->frames; dispatch_semaphore_signal(owner->slots);
            }];
            if(capture[0]) {
                const std::string path(capture.data());
                dispatch_group_enter(captureJobs);
                [command addCompletedHandler:^(id<MTLCommandBuffer> completed) {
                    dispatch_async(owner->captureQueue, ^{
                        @autoreleasepool {
                        if(completed.status!=MTLCommandBufferStatusError && saveTexture(readback,width,height,stride,path.c_str())) ++owner->captured;
                        else ++owner->errors;
                        dispatch_group_leave(owner->captureJobs);
                        }
                    });
                }];
            }
            [command commit];
            if(drawable)[CATransaction flush];
            slot=(slot+1)%3;
        }
    }

    static CVReturn displayTick(CVDisplayLinkRef,const CVTimeStamp *,const CVTimeStamp *,
            CVOptionFlags,CVOptionFlags *,void *context) {
        auto *owner=static_cast<Impl *>(context);
        // Coalesce ticks: a hidden/slow renderer must never build a backlog.
        if(!owner->refreshPending.exchange(true))dispatch_semaphore_signal(owner->refresh);
        return kCVReturnSuccess;
    }
    void run() {
        @autoreleasepool {
        pthread_set_qos_class_self_np(QOS_CLASS_USER_INITIATED,0);
        lastFrame=rateStart=SDL_GetTicksNS();
        CVDisplayLinkRef clock=nullptr;
        if(!offscreen) {
            if(CVDisplayLinkCreateWithActiveCGDisplays(&clock)!=kCVReturnSuccess ||
               CVDisplayLinkSetOutputCallback(clock,displayTick,this)!=kCVReturnSuccess ||
               CVDisplayLinkStart(clock)!=kCVReturnSuccess) {
                ++errors;running=false;
                if(clock)CVDisplayLinkRelease(clock);
                return;
            }
        }
        bool previousUncapped=false;
        uint32_t currentDisplay=0;
        auto deadline=std::chrono::steady_clock::now();
        while(running) { @autoreleasepool {
            State current;
            { std::lock_guard<std::mutex> lock(stateMutex);current=state; }
            if(clock && current.displayId && current.displayId!=currentDisplay) {
                CVDisplayLinkSetCurrentCGDisplay(clock,current.displayId);currentDisplay=current.displayId;
            }
            if(current.uncapped!=previousUncapped) {
                layer.displaySyncEnabled=!current.uncapped;previousUncapped=current.uncapped;
            }
            if(!current.active && !offscreen) { std::this_thread::sleep_for(std::chrono::milliseconds(30));continue; }
            if(!offscreen && !current.uncapped) {
                if(dispatch_semaphore_wait(refresh,dispatch_time(DISPATCH_TIME_NOW,100000000)))continue;
                refreshPending=false;
                if(!running)break;
            }
            const auto stalled=renderStallMs.exchange(0);if(stalled)std::this_thread::sleep_for(std::chrono::milliseconds(stalled));
            renderFrame(offscreen ? nil : [layer nextDrawable]);
            if(offscreen && !current.uncapped) {
                deadline+=std::chrono::nanoseconds(16666667);
                const auto currentTime=std::chrono::steady_clock::now();
                if(deadline<currentTime)deadline=currentTime;
                std::this_thread::sleep_until(deadline);
            } else deadline=std::chrono::steady_clock::now();
        }}
        if(clock) { CVDisplayLinkStop(clock);CVDisplayLinkRelease(clock); }
        for(int i=0;i<3;++i)dispatch_semaphore_wait(slots,DISPATCH_TIME_FOREVER);
        for(int i=0;i<3;++i)dispatch_semaphore_signal(slots);
        }
    }

};

SoundMetalRenderer::SoundMetalRenderer():m_impl(std::make_unique<Impl>()) {}
SoundMetalRenderer::~SoundMetalRenderer() { stop(); }
bool SoundMetalRenderer::start(CAMetalLayer *layer,bool offscreen,const char *assets) {
    auto &p=*m_impl;
    p.layer=layer;p.offscreen=offscreen;p.assets=assets;
    if(!p.draw.meshes.load(p.assets+"/authored_meshes.obj")) {p.failure="Cannot load original engine meshes";return false;}
    p.device=MTLCreateSystemDefaultDevice();
    if(!p.device) { p.failure="Metal is unavailable";return false; }
    layer.device=p.device;layer.pixelFormat=MTLPixelFormatBGRA8Unorm;layer.framebufferOnly=NO;
    layer.maximumDrawableCount=3;layer.displaySyncEnabled=YES;layer.presentsWithTransaction=NO;layer.opaque=YES;
    p.queue=[p.device newCommandQueue];
    MTKTextureLoader *textures=[[MTKTextureLoader alloc] initWithDevice:p.device];
    NSError *foliageError=nil;
    p.foliage=[textures newTextureWithContentsOfURL:[NSURL fileURLWithPath:[NSString stringWithUTF8String:(p.assets+"/scenery/pine-grove.png").c_str()]]
        options:@{MTKTextureLoaderOptionSRGB:@NO,MTKTextureLoaderOptionGenerateMipmaps:@YES,MTKTextureLoaderOptionOrigin:MTKTextureLoaderOriginTopLeft} error:&foliageError];
    if(!p.foliage) {p.failure="Cannot load the forest texture";return false;}
    NSURL *url=[NSBundle.mainBundle URLForResource:@"sound_ui" withExtension:@"metallib" subdirectory:@"assets/shaders"];
    if(!url) url=[NSURL fileURLWithPath:@ENGINE_SIM_SOUND_METALLIB];
    NSError *error=nil;
    id<MTLLibrary> library=[p.device newLibraryWithURL:url error:&error];
    if(!library) { p.failure=error.localizedDescription.UTF8String;return false; }
    if(!p.car.load(p.device,library,p.assets)) {p.failure="Cannot load the driving car mesh or Metal pipeline";return false;}
    MTLRenderPipelineDescriptor *desc=[MTLRenderPipelineDescriptor new];
    desc.vertexFunction=[library newFunctionWithName:@"sound_vertex"];
    desc.fragmentFunction=[library newFunctionWithName:@"sound_fragment"];
    desc.depthAttachmentPixelFormat=MTLPixelFormatDepth32Float;
    MTLDepthStencilDescriptor *depth=[MTLDepthStencilDescriptor new];
    depth.depthCompareFunction=MTLCompareFunctionLessEqual;depth.depthWriteEnabled=YES;
    p.depthState=[p.device newDepthStencilStateWithDescriptor:depth];
    auto attachment=desc.colorAttachments[0];
    attachment.pixelFormat=MTLPixelFormatBGRA8Unorm;
    attachment.blendingEnabled=YES;
    attachment.sourceRGBBlendFactor=MTLBlendFactorSourceAlpha;attachment.destinationRGBBlendFactor=MTLBlendFactorOneMinusSourceAlpha;
    attachment.sourceAlphaBlendFactor=MTLBlendFactorOne;attachment.destinationAlphaBlendFactor=MTLBlendFactorOneMinusSourceAlpha;
    p.pipeline=[p.device newRenderPipelineStateWithDescriptor:desc error:&error];
    if(!p.pipeline || !p.makeAtlas()) { p.failure="Could not create the Metal pipeline or font atlas";return false; }
    for(auto &buffer:p.buffers) buffer=[p.device newBufferWithLength:131072*sizeof(Vertex) options:MTLResourceStorageModeShared];
    if(offscreen) {
        MTLTextureDescriptor *target=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm width:Width*2 height:Height*2 mipmapped:NO];
        target.usage=MTLTextureUsageRenderTarget;target.storageMode=MTLStorageModePrivate;
        p.target=[p.device newTextureWithDescriptor:target];
    }
    p.game.start();p.running=true;p.thread=std::thread([&p]{p.run();});return true;
}
void SoundMetalRenderer::stop() {
    m_impl->game.stop();
    m_impl->running=false;dispatch_semaphore_signal(m_impl->refresh);
    if(m_impl->thread.joinable())m_impl->thread.join();
    dispatch_group_wait(m_impl->captureJobs,DISPATCH_TIME_FOREVER);
    m_impl->session.reset();
}
void SoundMetalRenderer::update(const State &state) {
    m_impl->game.controls(state.steering,state.roadView && state.ready,state.testPilot,state.testKeyboard,state.driftHeld,state.world);
    std::lock_guard<std::mutex> lock(m_impl->stateMutex);m_impl->state=state;
}
void SoundMetalRenderer::recoverCar() {m_impl->game.recover();}
void SoundMetalRenderer::restartRace() {m_impl->game.restart();}
void SoundMetalRenderer::stallRendering(unsigned ms) {m_impl->renderStallMs=std::min(ms,3000u);}
void SoundMetalRenderer::connectSession(std::shared_ptr<SoundSession> session) {
    m_impl->game.connect(session);
    std::lock_guard<std::mutex> lock(m_impl->stateMutex);
    m_impl->session=std::move(session);++m_impl->sessionGeneration;
}
Metrics SoundMetalRenderer::metrics() const {
    const auto &p=*m_impl; Metrics result;result.game=p.game.snapshot();result.gameCpuMs=p.game.cpuMilliseconds();result.gameTargetSpeed=p.game.targetSpeed();
    result.frames=p.frames.load();result.cpuNs=p.cpuNs.load();result.gpuNs=p.gpuNs.load();result.errors=p.errors.load();
    result.audioBlocksSeen=p.audioBlocksSeen.load();result.visualBlocksSeen=p.visualBlocksSeen.load();result.animatedFrames=p.animatedFrames.load();result.peakVertices=p.peakVertices.load();
    result.roadFrames=p.roadFrames.load();result.movingRoadFrames=p.movingRoadFrames.load();result.roadDistance=p.roadDistance.load();
    result.sunX=p.sunX.load();result.sunY=p.sunY.load();result.sunVisible=p.sunVisible.load();
    for(int i=0;i<256;++i){result.cpuHistogram[i]=p.cpuHist[i].load();result.gpuHistogram[i]=p.gpuHist[i].load();}
    return result;
}
void SoundMetalRenderer::capture(const char *path) { std::lock_guard<std::mutex> lock(m_impl->stateMutex);std::snprintf(m_impl->capturePath.data(),m_impl->capturePath.size(),"%s",path); }
unsigned SoundMetalRenderer::captures() const { return m_impl->captured.load(); }
const char *SoundMetalRenderer::error() const { return m_impl->failure.c_str(); }
