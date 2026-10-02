#include <metal_stdlib>
using namespace metal;
// Explicit scalar padding matches the C++ 48-byte vertex stride.
struct PackedVertex { float2 position; float2 uv; float4 color; uint kind; float depth; uint p1, p2; };
struct Raster { float4 position [[position]]; float2 uv; float4 color; uint kind [[flat]]; };
vertex Raster sound_vertex(uint id [[vertex_id]], const device PackedVertex *vertices [[buffer(0)]]) {
    PackedVertex v = vertices[id];
    Raster out;
    // The dashboard stays at the far plane; the clipped road viewport supplies
    // projected depth. Both share one batch without sorting car surfaces.
    out.position = float4(v.position.x / 640.0 - 1.0, 1.0 - v.position.y / 400.0, v.kind == 3 ? v.depth : 1.0, 1);
    out.uv = v.uv; out.color = v.color; out.kind = v.kind;
    return out;
}
fragment float4 sound_fragment(Raster in [[stage_in]], texture2d<float> atlas [[texture(0)]]) {
    constexpr sampler glyphSampler(filter::linear, address::clamp_to_edge);
    float alpha = in.color.a;
    if (in.kind == 1) alpha *= atlas.sample(glyphSampler, in.uv).r;
    else if (in.kind == 2) alpha *= exp(-4.0 * dot(in.uv, in.uv));
    float3 color = in.color.rgb;
    if (in.kind == 3) {
        float distance = .25 / max(0.00001, 1.0 - in.position.z);
        color = mix(color, float3(137.0, 154.0, 156.0) / 255.0, clamp((distance - 35.0) / 150.0, 0.0, .88));
    }
    return float4(color, alpha);
}
