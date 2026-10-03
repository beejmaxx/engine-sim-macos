#ifndef ENGINE_SIM_SOUND_CAR_METAL_H
#define ENGINE_SIM_SOUND_CAR_METAL_H
#include <fstream>
#include <vector>
#include <simd/simd.h>

// All bodies are uploaded before audio starts. Engine switching selects cached
// GPU buffers; rendering never reads files or touches simulation objects.
class SoundCarMetal {
    struct Uniforms {
        simd_float4 camera,right,up,forward;
        std::array<simd_float4,5> wheels;
        simd_float4 motion,pose,suspension,sun;
    };
    struct Model {
        id<MTLBuffer> vertices=nil,indices=nil,materials=nil;
        std::array<simd_float4,5> wheels{};
        uint32_t indexCount=0;
    };
    std::array<Model,2> models{};
    id<MTLRenderPipelineState> pipeline=nil;
    bool loadModel(id<MTLDevice> device,const std::string &path,Model &model) {
        std::ifstream file(path,std::ios::binary);
        char magic[4];uint32_t vertexCount,materialCount,wheelCount;
        file.read(magic,4);file.read(reinterpret_cast<char *>(&vertexCount),4);
        file.read(reinterpret_cast<char *>(&model.indexCount),4);file.read(reinterpret_cast<char *>(&materialCount),4);file.read(reinterpret_cast<char *>(&wheelCount),4);
        if(!file || std::memcmp(magic,"ESC1",4) || vertexCount>2000000 || model.indexCount>6000000 || materialCount>64 || wheelCount!=5)return false;
        file.read(reinterpret_cast<char *>(model.wheels.data()),sizeof(model.wheels));
        auto buffer=[&](size_t bytes) {
            std::vector<char> data(bytes);file.read(data.data(),bytes);
            return file ? [device newBufferWithBytes:data.data() length:bytes options:MTLResourceStorageModeShared] : nil;
        };
        model.materials=buffer(materialCount*48);model.vertices=buffer(vertexCount*40);model.indices=buffer(model.indexCount*4);
        return model.materials && model.vertices && model.indices;
    }
public:
    bool load(id<MTLDevice> device,id<MTLLibrary> library,const std::string &assets) {
        if(!loadModel(device,assets+"/vehicles/concept/car.mesh",models[0]) ||
            !loadModel(device,assets+"/vehicles/porsche_gt3/car.mesh",models[1]))return false;
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
    void draw(id<MTLRenderCommandEncoder> encoder,const SoundRoadScene &scene,double distance,bool brake,sound_ui::State::CarBody body) {
        const auto &model=models[body==sound_ui::State::CarBody::PorscheGt3 ? 1 : 0];
        const auto &p=scene.carPose();
        Uniforms u{scene.cameraPosition(),scene.cameraRight(),scene.cameraUp(),scene.cameraForward(),model.wheels,
            {0,brake ? 1.f : 0.f,float(p.steer),0},
            {float(p.x),.025f,float(p.z),float(p.yaw)},{float(p.pitch),float(p.roll),0,0},scene.sunDirection()};
        for(size_t i=1;i<u.wheels.size();++i) {
            const double radius=model.wheels[i].w>0 ? model.wheels[i].w : .383759;
            u.wheels[i].w=float(std::fmod(distance/radius,6.28318530718));
        }
        [encoder setRenderPipelineState:pipeline];[encoder setVertexBuffer:model.vertices offset:0 atIndex:0];
        [encoder setVertexBytes:&u length:sizeof(u) atIndex:1];
        [encoder setFragmentBytes:&u length:sizeof(u) atIndex:1];[encoder setFragmentBuffer:model.materials offset:0 atIndex:2];
        [encoder drawIndexedPrimitives:MTLPrimitiveTypeTriangle indexCount:model.indexCount indexType:MTLIndexTypeUInt32 indexBuffer:model.indices indexBufferOffset:0];
    }
};
#endif
