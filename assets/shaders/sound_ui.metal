#include <metal_stdlib>
using namespace metal;
// Explicit scalar padding matches the C++ 48-byte vertex stride.
struct PackedVertex { float2 position; float2 uv; float4 color; uint kind; float depth; uint p1, p2; };
struct Raster { float4 position [[position]]; float2 uv; float4 color; uint kind [[flat]]; };
vertex Raster sound_vertex(uint id [[vertex_id]], const device PackedVertex *vertices [[buffer(0)]]) {
    PackedVertex v = vertices[id];
    Raster out;
    // Dashboard/sky use the far plane, scenery supplies projected depth,
    // and the driving HUD uses the near plane above the separate car draw.
    out.position = float4(v.position.x / 640.0 - 1.0, 1.0 - v.position.y / 400.0, v.depth, 1);
    out.uv = v.uv; out.color = v.color; out.kind = v.kind;
    return out;
}
fragment float4 sound_fragment(Raster in [[stage_in]], texture2d<float> atlas [[texture(0)]],texture2d<float> foliage [[texture(1)]]) {
    constexpr sampler glyphSampler(filter::linear, address::clamp_to_edge);
    float alpha = in.color.a;
    if (in.kind == 1) alpha *= atlas.sample(glyphSampler, in.uv).r;
    else if (in.kind == 2) alpha *= exp(-4.0 * dot(in.uv, in.uv));
    float3 color = in.color.rgb;
    if (in.kind >= 3) {
        float distance = .25 / max(0.00001, 1.0 - in.position.z);
        if (in.kind == 6) {
            constexpr sampler leaves(filter::linear,mip_filter::linear,address::clamp_to_edge);
            float4 sample=foliage.sample(leaves,in.uv*distance);
            if(sample.a<.4)discard_fragment();
            color*=sample.rgb;alpha=1;
        }
        if(in.kind==7 || in.kind==8) {
            float2 wall=in.uv*distance,cell=wall/float2(3.2,3.5);
            float2 f=fract(cell),aa=max(fwidth(cell),float2(.007));
            float2 window=smoothstep(float2(.14),float2(.14)+aa,f)*(1-smoothstep(float2(.86)-aa,float2(.86),f));
            float mask=window.x*window.y*smoothstep(1.25,1.5,wall.y);
            float variation=fract(sin(dot(floor(cell),float2(12.9898,78.233)))*43758.5453);
            float3 glass=mix(float3(.13,.22,.29),float3(.38,.52,.59),clamp(wall.y/65.0,0.0,1.0));
            if(variation>.94)glass=float3(.73,.66,.43);
            if(in.kind==8)color=mix(color*.75,glass,.9*mask);
            else color=mix(color,glass,mask*.92);
        }
        if(in.kind==9) {
            float2 world=in.uv*distance;
            color*=.95+.05*sin(world.x*.3+world.y*.18);
        }
        if (in.kind == 4 || in.kind == 5) {
            // Perspective-correct, metre-scaled road/ground detail. Distance
            // comes from physics, so the surface stays still when stopped.
            float2 world = in.uv * distance;
            float footprint = max(length(dfdx(world)), length(dfdy(world)));
            if (in.kind == 4) {
                float grain = fract(sin(dot(floor(world * 70.0), float2(12.9898, 78.233))) * 43758.5453);
                color *= 1.0 + (grain - .5) * .22 * (1.0 - smoothstep(.015, .07, footprint));
            } else {
                float patches = sin(world.x * .37) * sin(world.y * .19) + .35 * sin(world.x * 1.3 + world.y * .9);
                color *= 1.0 + .08 * patches * (1.0 - smoothstep(.5, 4.0, footprint));
            }
        }
        color = mix(color, float3(137.0, 154.0, 156.0) / 255.0, clamp((distance - 35.0) / 150.0, 0.0, .88));
    }
    return float4(color, alpha);
}

struct CarVertex { packed_float3 p,n;float2 uv;uint material,wheel; };
struct CarUniforms { float4 camera,right,up,forward;float4 wheels[5];float4 motion,pose,suspension,sun; };
struct CarMaterial { float4 base,surface,emission; };
struct CarRaster { float4 position [[position]];float3 world,normal;uint material [[flat]]; };
vertex CarRaster car_vertex(uint id [[vertex_id]],const device CarVertex *mesh [[buffer(0)]],constant CarUniforms &u [[buffer(1)]]) {
    CarVertex v=mesh[id];float3 p=v.p,n=v.n;
    uint wheel=v.wheel&7;
    if(wheel>0 && wheel<5) {
        float3 center=u.wheels[wheel].xyz;
        p-=center;
        if((v.wheel&8)==0) {
            float angle=u.wheels[wheel].w,c=cos(angle),s=sin(angle);
            p.yz=float2(c*p.y-s*p.z,s*p.y+c*p.z);
            n.yz=float2(c*n.y-s*n.z,s*n.y+c*n.z);
        }
        if(wheel<=2) {
            float c=cos(u.motion.z),s=sin(u.motion.z);
            p.xz=float2(c*p.x+s*p.z,-s*p.x+c*p.z);n.xz=float2(c*n.x+s*n.z,-s*n.x+c*n.z);
        }
        p+=center;
    }
    float c=cos(u.suspension.x),s=sin(u.suspension.x);
    p.yz=float2(c*p.y-s*p.z,s*p.y+c*p.z);n.yz=float2(c*n.y-s*n.z,s*n.y+c*n.z);
    c=cos(u.suspension.y);s=sin(u.suspension.y);
    p.xy=float2(c*p.x-s*p.y,s*p.x+c*p.y);n.xy=float2(c*n.x-s*n.y,s*n.x+c*n.y);
    c=cos(u.pose.w);s=sin(u.pose.w);
    p.xz=float2(c*p.x+s*p.z,-s*p.x+c*p.z);n.xz=float2(c*n.x+s*n.z,-s*n.x+c*n.z);
    p+=u.pose.xyz;
    float3 relative=p-u.camera.xyz;
    float3 eye=float3(dot(relative,u.right.xyz),dot(relative,u.up.xyz),dot(relative,u.forward.xyz));
    CarRaster out;out.position=float4(eye.x*1.25,eye.y*2+.04*eye.z,eye.z-.25,eye.z);
    out.world=p;out.normal=n;out.material=v.material;return out;
}
float3 car_environment(float3 direction,float roughness) {
    float sky=smoothstep(-.04,.35,direction.y);
    float3 ground=float3(.11,.14,.09);
    float3 horizon=float3(.72,.81,.87),zenith=float3(.16,.30,.48);
    float3 light=mix(horizon,zenith,smoothstep(.1,.8,direction.y));
    float clouds=pow(max(0.0,sin(direction.x*7+direction.z*3)*.5+.5),6.0)*(1-smoothstep(.15,.65,direction.y));
    light=mix(light,float3(.95,.95,.91),clouds*(1-roughness)*.7);
    return mix(ground,light,sky);
}
fragment float4 car_fragment(CarRaster in [[stage_in]],constant CarUniforms &u [[buffer(1)]],
        const device CarMaterial *materials [[buffer(2)]]) {
    CarMaterial m=materials[in.material];float3 n=normalize(in.normal),v=normalize(u.camera.xyz-in.world);
    if(dot(n,v)<0)n=-n;
    float3 l=u.sun.xyz,h=normalize(l+v);
    float nv=max(.001,dot(n,v)),nl=max(.0,dot(n,l)),nh=max(.0,dot(n,h)),vh=max(.0,dot(v,h));
    float metallic=m.surface.x,rough=clamp(m.surface.y,.06,1.0),a=rough*rough,a2=a*a;
    float3 f0=mix(float3(.04),m.base.rgb,metallic),f=f0+(1-f0)*pow(1-vh,5.0);
    float denominator=nh*nh*(a2-1)+1;
    float distribution=a2/(M_PI_F*denominator*denominator);
    float k=(rough+1)*(rough+1)/8,geometry=nv/(nv*(1-k)+k)*nl/(nl*(1-k)+k);
    float3 specular=distribution*geometry*f/max(.001,4*nv*max(.001,nl));
    float3 reflected=car_environment(reflect(-v,n),rough);
    float3 fresnel=f0+(1-f0)*pow(1-nv,5.0);
    float3 light=(m.base.rgb*(1-metallic)/M_PI_F+specular)*nl*float3(2.5,2.3,2.0);
    light+=m.base.rgb*(1-metallic)*(.18+.28*max(0.0,n.y));
    light+=reflected*fresnel*(1-rough*.5);
    light+=m.surface.z*reflected*(.04+.96*pow(1-nv,5.0));
    light+=m.emission.rgb;
    if(m.emission.w>.5)light+=float3(1,.025,.006)*(u.motion.y>0 ? 4.0 : .35);
    if(m.surface.w>.5)light=reflected*(.065+.45*pow(1-nv,5.0));
    // Simple filmic exposure, then encode for the dashboard's unorm target.
    light=(light*(2.51*light+.03))/(light*(2.43*light+.59)+.14);
    return float4(pow(clamp(light,0.0,1.0),float3(1.0/2.2)),m.surface.w>.5 ? 1.0 : m.base.a);
}
