#include <metal_stdlib>
using namespace metal;
// Explicit scalar padding matches the C++ 48-byte vertex stride.
struct PackedVertex { float2 position; float2 uv; float4 color; uint kind; uint p0, p1, p2; };
struct Raster { float4 position [[position]]; float2 uv; float4 color; uint kind [[flat]]; };
vertex Raster sound_vertex(uint id [[vertex_id]], const device PackedVertex *vertices [[buffer(0)]]) {
    PackedVertex v = vertices[id];
    Raster out;
    out.position = float4(v.position.x / 640.0 - 1.0, 1.0 - v.position.y / 400.0, 0, 1);
    out.uv = v.uv; out.color = v.color; out.kind = v.kind;
    return out;
}
fragment float4 sound_fragment(Raster in [[stage_in]], texture2d<float> atlas [[texture(0)]]) {
    constexpr sampler glyphSampler(filter::linear, address::clamp_to_edge);
    float alpha = in.color.a;
    if (in.kind == 1) alpha *= atlas.sample(glyphSampler, in.uv).r;
    else if (in.kind == 2) alpha *= exp(-4.0 * dot(in.uv, in.uv));
    return float4(in.color.rgb, alpha);
}
