#ifndef ENGINE_SIM_SOUND_CAR_METAL_H
#define ENGINE_SIM_SOUND_CAR_METAL_H
#include <fstream>
#include <vector>
#include <simd/simd.h>

// A static GPU mesh: imported once, transformed/lit on Metal. Rendering never
// expands the 213k authored triangles on the CPU or touches simulation objects.
class SoundCarMetal {
    struct Uniforms {
        simd_float4 camera,right,up,forward;
        std::array<simd_float4,5> wheels;
        simd_float4 motion,pose,suspension,sun;
    };
    id<MTLBuffer> vertices=nil,indices=nil,materials=nil;
    id<MTLRenderPipelineState> pipeline=nil;
    std::array<simd_float4,5> wheels{};
    uint32_t indexCount=0;
public:
    bool load(id<MTLDevice> device,id<MTLLibrary> library,const std::string &assets) {
        std::ifstream file(assets+"/vehicles/concept/car.mesh",std::ios::binary);
        char magic[4];uint32_t vertexCount,materialCount,wheelCount;
        file.read(magic,4);file.read(reinterpret_cast<char *>(&vertexCount),4);
        file.read(reinterpret_cast<char *>(&indexCount),4);file.read(reinterpret_cast<char *>(&materialCount),4);file.read(reinterpret_cast<char *>(&wheelCount),4);
        if(!file || std::memcmp(magic,"ESC1",4) || vertexCount>2000000 || indexCount>6000000 || materialCount>64 || wheelCount!=5)return false;
        file.read(reinterpret_cast<char *>(wheels.data()),sizeof(wheels));
        auto buffer=[&](size_t bytes) {
            std::vector<char> data(bytes);file.read(data.data(),bytes);
            return file ? [device newBufferWithBytes:data.data() length:bytes options:MTLResourceStorageModeShared] : nil;
        };
        materials=buffer(materialCount*48);vertices=buffer(vertexCount*40);indices=buffer(indexCount*4);
        if(!materials || !vertices || !indices)return false;
        MTLRenderPipelineDescriptor *descriptor=[MTLRenderPipelineDescriptor new];
        descriptor.vertexFunction=[library newFunctionWithName:@"car_vertex"];
        descriptor.fragmentFunction=[library newFunctionWithName:@"car_fragment"];
        descriptor.depthAttachmentPixelFormat=MTLPixelFormatDepth32Float;
        auto color=descriptor.colorAttachments[0];color.pixelFormat=MTLPixelFormatBGRA8Unorm;
        color.blendingEnabled=YES;color.sourceRGBBlendFactor=MTLBlendFactorSourceAlpha;
        color.destinationRGBBlendFactor=MTLBlendFactorOneMinusSourceAlpha;
        color.sourceAlphaBlendFactor=MTLBlendFactorOne;color.destinationAlphaBlendFactor=MTLBlendFactorOneMinusSourceAlpha;
        NSError *error=nil;pipeline=[device newRenderPipelineStateWithDescriptor:descriptor error:&error];
        return pipeline!=nil;
    }
    void draw(id<MTLRenderCommandEncoder> encoder,const SoundRoadScene &scene,double distance,bool brake) {
        const auto &p=scene.carPose();
        Uniforms u{scene.cameraPosition(),scene.cameraRight(),scene.cameraUp(),scene.cameraForward(),wheels,
            {float(std::fmod(distance/.383759,6.28318530718)),brake ? 1.f : 0.f,float(p.steer),0},
            {float(p.x),.025f,float(p.z),float(p.yaw)},{float(p.pitch),float(p.roll),0,0},scene.sunDirection()};
        [encoder setRenderPipelineState:pipeline];[encoder setVertexBuffer:vertices offset:0 atIndex:0];
        [encoder setVertexBytes:&u length:sizeof(u) atIndex:1];
        [encoder setFragmentBytes:&u length:sizeof(u) atIndex:1];[encoder setFragmentBuffer:materials offset:0 atIndex:2];
        [encoder drawIndexedPrimitives:MTLPrimitiveTypeTriangle indexCount:indexCount indexType:MTLIndexTypeUInt32 indexBuffer:indices indexBufferOffset:0];
    }
};
#endif
