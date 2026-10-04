// Native renderer-only city geometry. Baked once at startup; physics uses the
// same immutable DrivingCity footprints, while nearby cells supply detail.
static int cityCell(float p) {
    return std::clamp(int(std::floor(p/DrivingCity::Block))+DrivingCity::GridRadius+1,0,CityCells-1);
}
void cacheCity(bool skyline=false) {
    for(const auto &face:faces) {
        const auto center=(face.a+face.b+face.c)/3;
        auto &chunk=cityChunks[cityCell(center.x)*CityCells+cityCell(center.z)];
        (skyline ? chunk.skyline : chunk.detail).push_back(face);
    }
    faces.clear();
}
void cityBuilding(const DrivingCity::Building &b,bool distant=false) {
    using Style=DrivingCity::Style;
    const float x=b.lo.x,z=b.lo.z,X=b.hi.x,Z=b.hi.z,h=b.height;
    const float mx=(x+X)*.5f,mz=(z+Z)*.5f;
    if(b.style==Style::Stadium) {
        if(distant) {box({x,0,z},{X,h,Z},0xAAAFA8);return;}
        const bool longZ=Z-z>X-x;
        const float blockCenter=(std::floor(mx/DrivingCity::Block)+.5f)*DrivingCity::Block;
        for(int row=0;row<7;++row) {
            const float rise=longZ && mx<blockCenter ? 16-row*2.f : 4+row*2.f;
            const float a=longZ ? x+(X-x)*row/7 : z+(Z-z)*row/7;
            const float end=longZ ? x+(X-x)*(row+1)/7 : z+(Z-z)*(row+1)/7;
            const V3 low=longZ ? V3{a,.15f,z} : V3{x,.15f,a};
            const V3 high=longZ ? V3{end,rise,Z} : V3{X,rise,end};
            box(low,high,row%2 ? 0x366E82 : 0xCFB888);
        }
        box({x-1,h+1,z-1},{X+1,h+1.4f,Z+1},0xCCD1C7);
        for(float at=z+3;at<Z;at+=18)box({x+.3f,0,at},{x+.55f,h+1,at+.25f},0x7F9498);
        return;
    }
    const unsigned tint=b.glass ? 0x789CAA : b.color;
    const unsigned kind=b.style==Style::Container || b.style==Style::Warehouse ? 13 : b.style==Style::Masonry ? 12 : 0;
    const V3 a{x,.14f,z},c{X,h,z},e{x,.14f,Z},g{X,h,Z};
    facade(a,{X,.14f,z},c,{x,h,z},tint,b.glass,kind);
    facade({X,.14f,Z},e,{x,h,Z},g,tint,b.glass,kind);
    facade(e,a,{x,h,z},{x,h,Z},tint,b.glass,kind);
    facade({X,.14f,z},{X,.14f,Z},g,c,tint,b.glass,kind);
    if(b.style==Style::House) {
        const float ridge=h+3.4f;
        quad({x-.7f,h,z-.7f},{mx,ridge,z-.7f},{mx,ridge,Z+.7f},{x-.7f,h,Z+.7f},0x685E56,true);
        quad({mx,ridge,z-.7f},{X+.7f,h,z-.7f},{X+.7f,h,Z+.7f},{mx,ridge,Z+.7f},0x685E56,true);
        triangle({x,h,z},{X,h,z},{mx,ridge,z},tint,true);
        triangle({X,h,Z},{x,h,Z},{mx,ridge,Z},tint,true);
    } else {
        box({x-.2f,h,z-.2f},{X+.2f,h+.6f,Z+.2f},b.style==Style::Container ? b.color : 0xB8BAB4);
        if(b.glass && h>90) {
            const float inset=(X-x)*.17f;
            box({x+inset,h+.6f,z+inset},{X-inset,h+8,Z-inset},0x667D87);
            if(h>180) {
                box({mx-5,h+8,mz-5},{mx+5,h+24,mz+5},0x91A6AD);
                box({mx-.35f,h+24,mz-.35f},{mx+.35f,h+53,mz+.35f},0xD7DFD9);
            }
        }
    }
    if(distant)return;
    if(b.style==Style::Container) {
        box({x+.3f,.3f,z-.1f},{x+.42f,h-.2f,z},0xB9BDB3);
        box({X-.42f,.3f,z-.1f},{X-.3f,h-.2f,z},0xB9BDB3);
        return;
    }
    if(b.style==Style::House) {
        box({mx-1.6f,.15f,z-2.2f},{mx+1.6f,2.6f,z-.12f},0xD4CDB9);
        box({mx-.7f,.16f,z-2.25f},{mx+.7f,2.3f,z-2.2f},0x344F59);
        box({x+3,h,z+4},{x+4.4f,h+3,z+6},0xA58B76);
        ground(mx-2,z-9,mx+2,z-2,.016f,0xB3B1A3,11);
        return;
    }
    box({mx-3,h+.6f,mz-2},{mx+3,h+2,mz+2},0x64727B);
    box({x,2.8f,z-.6f},{X,3.3f,z-.05f},b.glass ? 0x475F6D : 0x3C7779);
    // Recessed shop windows, doors and awnings at street level.
    for(float at=x+2;at<X-3;at+=7) {
        box({at,.4f,z-.08f},{std::min(X-1,at+5),2.65f,z},0x243C49);
        box({at+2,.4f,z-.12f},{at+2.08f,2.65f,z-.08f},0xABB5B2);
    }
    if(b.style==Style::Warehouse) {
        for(float at=x+3;at<X-10;at+=15)box({at,.15f,z-.15f},{at+10,5,z},0x667D84);
    }
    const unsigned seed=unsigned(std::abs(x*7+z*13));
    if(b.style==Style::Masonry || b.style==Style::Civic) {
        static constexpr const char *shops[]={"CAFE","MOTORS","MARKET","ARCADE","NOODLES","RECORDS","PORTSIDE"};
        signs.push_back({{x+2,3.48f,z-.65f},{1,0,0},.07f,shops[seed%7],0xF1E8C8});
        box({x,3.3f,z-.65f},{X,5.2f,z-.12f},seed%2 ? 0x355F60 : 0x984B38);
    }
}
void streetLamp(float x,float z,float side,bool alongX) {
    auto p=[&](float cross,float y,float along) {return alongX ? V3{x+along,y,z+cross} : V3{x+cross,y,z+along};};
    const auto a=p(0,.1f,0),b=p(.13f,7.5f,.13f);
    box(a,b,0x52656C);
    const float over=-side*2.6f;
    auto lo=p(std::min(0.f,over),7.35f,0),hi=p(std::max(.13f,over),7.5f,.13f);
    box(lo,hi,0x52656C);
    lo=p(over-.45f,7.2f,-.15f);hi=p(over+.45f,7.38f,.35f);box(lo,hi,0xE5E1C8);
}
void buildCity() {
    using Lot=DrivingCity::Lot;
    constexpr float block=DrivingCity::Block,edge=DrivingCity::Extent;
    const int radius=DrivingCity::GridRadius;
    faces.reserve(80000);cityTrees.reserve(4500);signs.reserve(900);
    // Lane widths, crosswalks and sidewalks all use metre coordinates from the
    // collision map. Wide perimeter/express avenues connect every district.
    for(int i=-radius;i<=radius;++i) {
        const float at=i*block,w=DrivingCity::roadHalfWidth(i);
        for(float s=-edge;s<edge;s+=16) {
            const float end=std::min(edge,s+16);
            ground(at-w,s,at+w,end,.006f,0x343E45,4);
            ground(s,at-w,end,at+w,.006f,0x343E45,4);
            const int cross=int(std::round((s+4)/block));
            if(std::abs(s+4-cross*block)>DrivingCity::roadHalfWidth(cross)+8) {
                for(float offset:{-.25f,.25f}) {
                    ground(at+offset-.055f,s,at+offset+.055f,end,.017f,0xDCC06E);
                    ground(s,at+offset-.055f,end,at+offset+.055f,.017f,0xDCC06E);
                }
                for(float distance=6;distance<w-3;distance+=6)for(float side:{-1.f,1.f}) {
                    const float lane=side*distance;
                    ground(at+lane-.055f,s,at+lane+.055f,s+5,.018f,0xDCE1DD);
                    ground(s,at+lane-.055f,s+5,at+lane+.055f,.018f,0xDCE1DD);
                }
                for(float side:{-1.f,1.f}) {
                    ground(at+side*(w-1)-.06f,s,at+side*(w-1)+.06f,end,.018f,0xCED3CB);
                    ground(s,at+side*(w-1)-.06f,end,at+side*(w-1)+.06f,.018f,0xCED3CB);
                }
            }
            if(int(s+edge)%64==0 && std::abs(s-cross*block)>w+10)for(float side:{-1.f,1.f}) {
                streetLamp(at+side*(w+2),s,side,false);streetLamp(s,at+side*(w+2),side,true);
            }
        }
    }
    cacheCity();
    // Distant road ribbons keep avenues continuous when detail cells fall
    // back to skyline geometry. Markings and props remain nearby-only.
    for(int i=-radius;i<=radius;++i)for(float s=-edge;s<edge;s+=block) {
        const float at=i*block,w=DrivingCity::roadHalfWidth(i),end=std::min(edge,s+block);
        ground(at-w,s,at+w,end,.006f,0x343E45,4);
        ground(s,at-w,end,at+w,.006f,0x343E45,4);
    }
    cacheCity(true);
    for(int x=-radius;x<radius;++x)for(int z=-radius;z<radius;++z) {
        const float x0=x*block+DrivingCity::roadHalfWidth(x),z0=z*block+DrivingCity::roadHalfWidth(z);
        const float x1=(x+1)*block-DrivingCity::roadHalfWidth(x+1),z1=(z+1)*block-DrivingCity::roadHalfWidth(z+1);
        const float mx=(x0+x1)*.5f,mz=(z0+z1)*.5f;
        const unsigned seed=(x+9)*379+(z+9)*179;
        const auto use=DrivingCity::lot(x,z);
        const auto zone=DrivingCity::zone({mx,mz});
        ground(x0,z0,x1,z1,.011f,0xA8AAA0,11);
        if(use==Lot::Plaza || use==Lot::Fuel || use==Lot::Harbor || use==Lot::Stadium) {
            ground(x0+5,z0+5,x1-5,z1-5,.014f,0x555E61,4);
            for(float a=x0+10;a<x1-10;a+=3)ground(a,z0+8,a+.1f,z0+17,.021f,0xC6CDBF);
        } else if(use==Lot::Park || zone==DrivingCity::Zone::Garden || zone==DrivingCity::Zone::Northside || zone==DrivingCity::Zone::Beach) {
            ground(x0+5,z0+5,x1-5,z1-5,.014f,0x586F49,5);
        }
        if(use==Lot::Park) {
            ground(mx-3,z0+4,mx+3,z1-4,.018f,0xB7AD95,11);
            ground(x0+4,mz-3,x1-4,mz+3,.019f,0xB7AD95,11);
            for(int t=0;t<40;++t) {
                const float tx=x0+8+random(seed+t*37)*(x1-x0-16),tz=z0+8+random(seed+t*37+1)*(z1-z0-16);
                if(std::abs(tx-mx)<9 || std::abs(tz-mz)<9)continue;
                cityTrees.push_back({tx,tz,8+random(seed+t*7)*6,.82f+random(seed+t)*.18f});
            }
            for(float side:{-1.f,1.f})for(int n=0;n<4;++n) {
                const float bx=mx+side*5,bz=z0+15+n*23;
                box({bx-1,.4f,bz},{bx+1,.58f,bz+.6f},0x85725B);
                box({bx-1,.58f,bz+.5f},{bx+1,1,bz+.65f},0x85725B);
            }
        } else if(use==Lot::Fuel) {
            box({x0+18,5.5f,z0+20},{x1-18,6.2f,z0+61},0xD4D8CD);
            box({x0+18,5.8f,z0+19.7f},{x1-18,6.3f,z0+20},0xBA4937);
            signs.push_back({{x0+22,5.9f,z0+19.6f},{1,0,0},.033f,"PORTSIDE FUEL",0xFFFFFF});
            for(float px:{x0+25,x1-25})for(float pz:{z0+27,z0+51}) {
                box({px-.15f,.1f,pz-.15f},{px+.15f,5.5f,pz+.15f},0x78898A);
                box({px-1,.1f,pz+1},{px+1,1.7f,pz+2},0xD5D9C9);
                box({px-.5f,.9f,pz+.96f},{px+.5f,1.5f,pz+1},0x233B41);
            }
        } else if(use==Lot::Stadium) {
            ground(x0+24,z0+20,x1-24,z1-26,.024f,0x436D43,5);
            for(float pz=z0+20;pz<z1-26;pz+=10)ground(x0+24,pz,x1-24,std::min(pz+5,z1-26),.025f,0x4B7546,5);
            for(float px:{x0+25,x1-25})ground(px,z0+21,px+.15f,z1-27,.026f,0xE2E5D2);
            for(float pz:{z0+21,mz,z1-27})ground(x0+25,pz,x1-25,pz+.15f,.026f,0xE2E5D2);
            for(float pz:{z0+22,z1-28}) {
                for(float px:{mx-3.65f,mx+3.65f})box({px,.05f,pz},{px+.1f,2.45f,pz+.1f},0xECECE0);
                box({mx-3.65f,2.35f,pz},{mx+3.75f,2.45f,pz+.1f},0xECECE0);
            }
            for(float px:{x0+19,x1-19})for(float pz:{z0+10,z1-10}) {
                box({px,0,pz},{px+.4f,31,pz+.4f},0x70858B);
                box({px-2,30,pz-.3f},{px+2,32,pz+.5f},0xDCE6D8);
            }
            box({x0+28,5,z0+6},{x1-28,8,z0+7},0x28596B);
            signs.push_back({{x0+32,5.6f,z0+5.9f},{1,0,0},.09f,"PORTSIDE STADIUM",0xF8EED1});
        } else if(use==Lot::Harbor && x==8) {
            // Gantry cranes give the container port a silhouette visible from
            // the ring road. The legs stay inside the container yards.
            for(float px:{x0+16,x1-16})box({px,0,mz},{px+2,38,mz+2},0xCDA13E);
            box({x0+14,36,mz-2},{x1+17,40,mz+4},0xCDA13E);
            box({x1-12,17,mz-1},{x1-11.7f,36,mz-.7f},0x4E5C62);
            box({x1-18,24,mz-5},{x1-10,28,mz},0xA9C0C2);
        }
        // Trees along the curb, planted outside all building footprints.
        for(float a=x0+10;a<x1-8;a+=23)for(float pz:{z0+3,z1-3})
            if(use!=Lot::Harbor)cityTrees.push_back({a,pz,6.5f+random(seed+unsigned(a+edge))*2,.94f});
        if(use==Lot::Plaza) {
            // Open central plazas remain usable for donuts and drifting.
            signs.push_back({{x0+8,3.5f,z0+4},{1,0,0},.07f,x<0 ? "MARKET SQUARE" : "CIVIC SQUARE",0xF0ECD8});
            box({x0+6,3.3f,z0+4.1f},{x0+43,5.4f,z0+4.5f},0x41696D);
            for(float px:{x0+6,x0+42})box({px,0,z0+4},{px+.2f,5.3f,z0+4.3f},0x657B7D);
        }
    }
    cacheCity();
    for(const auto &b:drivingCity().buildings())cityBuilding(b);
    // Broad, soft-tinted contact shadows anchor buildings to the streets.
    // They are static world geometry, so no shadow pass competes with audio.
    for(const auto &b:drivingCity().buildings()) {
        const float length=std::min(55.,b.height*.6);
        const V3 a{float(b.lo.x),.023f,float(b.lo.z)},c{float(b.hi.x),.023f,float(b.lo.z)};
        const V3 offset{-length*.34f,0,-length};
        const auto begin=faces.size();quad(a,c,c+offset,a+offset,0x182B38);
        for(size_t i=begin;i<faces.size();++i)faces[i].tint.w=.16f;
    }
    cacheCity();
    // Simple silhouettes keep a continuous skyline beyond the detailed cells.
    // Their faces retain real heights/positions, so there is no sliding backdrop.
    for(const auto &b:drivingCity().buildings())cityBuilding(b,true);
    cacheCity(true);
    for(int x=-radius;x<=radius;++x)for(int z=-radius;z<=radius;++z) {
        const float xx=x*block,zz=z*block,wx=DrivingCity::roadHalfWidth(x),wz=DrivingCity::roadHalfWidth(z);
        for(float side:{-1.f,1.f})for(float stripe=-wx+2;stripe<wx-1;stripe+=2.4f)
            ground(xx+stripe,zz+side*(wz+2),xx+stripe+1.1f,zz+side*(wz+2)+3,.019f,0xD8DED6);
        for(float side:{-1.f,1.f})for(float stripe=-wz+2;stripe<wz-1;stripe+=2.4f)
            ground(xx+side*(wx+2),zz+stripe,xx+side*(wx+2)+3,zz+stripe+1.1f,.019f,0xD8DED6);
        for(float side:{-1.f,1.f}) {
            const float px=xx+side*(wx+2),pz=zz+side*(wz+2);
            box({px,.14f,pz},{px+.15f,5.7f,pz+.15f},0x56666B);
            box({px-.28f,4.25f,pz-.22f},{px+.42f,5.55f,pz+.25f},0x293B40);
            box({px-.12f,4.38f,pz-.24f},{px+.24f,4.65f,pz-.23f},0x59AD82);
            // Small street signs use the same cached font atlas as the HUD.
            box({px-.2f,3.45f,pz-.25f},{px+7.7f,4.12f,pz-.2f},0x356759);
            signs.push_back({{px,3.55f,pz-.26f},{1,0,0},.026f,
                z==0 ? "CENTRAL AVE" : std::abs(z)==6 ? "EXPRESSWAY" : DrivingCity::district({xx,zz}),0xF1EEDD});
        }
        if(x==0 && (z==-6 || z==6)) {
            for(float px:{xx-wx-3,xx+wx+3})box({px,0,zz+40},{px+.3f,9,zz+40.3f},0x778D93);
            box({xx-wx-3,8.7f,zz+40},{xx+wx+3,9,zz+40.3f},0x778D93);
            box({xx-12,6,zz+39.7f},{xx+12,8.7f,zz+40.1f},0x356B5B);
            signs.push_back({{xx-10,6.6f,zz+39.6f},{1,0,0},.07f,z<0 ? "DOWNTOWN / NORTH" : "NORTHSIDE / COAST",0xF0ECDC});
        }
    }
    // Coast promenade and beach: the boundary wall is visible and matches
    // collision limits; there are broad runoffs beyond the perimeter avenues.
    for(float s=-edge;s<edge;s+=16) {
        const float end=std::min(edge,s+16);
        ground(-edge+2,s,-1468,end,.009f,0xD1BE91,5);
        ground(1468,s,edge-2,end,.009f,0x9AABA5,11);
        for(float side:{-1.f,1.f}) {
            const float wall=side*(edge+1);
            box({s,0,wall-.5f},{end,1.1f,wall+.5f},0xADB9B9);
            box({wall-.5f,0,s},{wall+.5f,1.1f,end},0xADB9B9);
        }
        if(int(s+edge)%96==0) {
            cityTrees.push_back({-1480,s,8,.95f});
            streetLamp(1480,s,1,false);
            box({-1528,.1f,s},{-1526,3.8f,s+3},0xCCB894);
            quad({-1530,3.8f,s-1},{-1524,3.8f,s-1},{-1524,3.8f,s+4},{-1530,3.8f,s+4},0x6B9CA4,true);
        }
    }
    cacheCity();faces.reserve(28000);
}
