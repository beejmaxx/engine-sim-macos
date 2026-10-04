#ifndef ENGINE_SIM_SOUND_CAR_METAL_H
#define ENGINE_SIM_SOUND_CAR_METAL_H
#include <fstream>
#include <vector>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <simd/simd.h>

// All bodies are uploaded before audio starts. Engine switching selects cached
// GPU buffers; rendering never reads files or touches simulation objects.
class SoundCarMetal {
    struct Material { simd_float4 base,surface,emission,texture; };
    static_assert(sizeof(Material)==64);
    struct Uniforms {
        simd_float4 camera,right,up,forward;
        std::array<simd_float4,5> wheels;
        simd_float4 motion,pose,suspension,sun;
    };
    struct Model {
        id<MTLBuffer> vertices=nil,indices=nil,materials=nil;
        id<MTLTexture> textures=nil;
        std::array<simd_float4,5> wheels{};
        uint32_t indexCount=0;
    };
    std::array<Model,sound_ui::CarModels.size()> models{};
    id<MTLRenderPipelineState> pipeline=nil;
    bool loadTextures(id<MTLDevice> device,const std::string &directory,int count,Model &model) {
        if(count==0) {
            auto descriptor=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm_sRGB width:1 height:1 mipmapped:NO];
            descriptor.textureType=MTLTextureType2DArray;descriptor.arrayLength=1;
            model.textures=[device newTextureWithDescriptor:descriptor];
            const uint32_t white=0xffffffff;
            [model.textures replaceRegion:MTLRegionMake2D(0,0,1,1) mipmapLevel:0 slice:0 withBytes:&white bytesPerRow:4 bytesPerImage:4];
            return model.textures!=nil;
        }
        MTKTextureLoader *loader=[[MTKTextureLoader alloc] initWithDevice:device];
        std::vector<id<MTLTexture>> sources;
        for(int i=0;i<count;++i) {
            const auto path=directory+"/texture_"+std::to_string(i)+".png";
            NSError *error=nil;
            id<MTLTexture> texture=[loader newTextureWithContentsOfURL:[NSURL fileURLWithPath:[NSString stringWithUTF8String:path.c_str()]]
                options:@{MTKTextureLoaderOptionSRGB:@YES,MTKTextureLoaderOptionGenerateMipmaps:@NO,
                          MTKTextureLoaderOptionOrigin:MTKTextureLoaderOriginTopLeft} error:&error];
            if(!texture || texture.width!=512 || texture.height!=512 ||
                (!sources.empty() && texture.pixelFormat!=sources[0].pixelFormat))return false;
            sources.push_back(texture);
        }
        auto descriptor=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:sources[0].pixelFormat width:512 height:512 mipmapped:YES];
        descriptor.textureType=MTLTextureType2DArray;descriptor.arrayLength=count;
        descriptor.storageMode=MTLStorageModePrivate;descriptor.usage=MTLTextureUsageShaderRead;
        model.textures=[device newTextureWithDescriptor:descriptor];
        if(!model.textures)return false;
        auto queue=[device newCommandQueue];auto command=[queue commandBuffer];auto blit=[command blitCommandEncoder];
        for(int i=0;i<count;++i)[blit copyFromTexture:sources[i] sourceSlice:0 sourceLevel:0
            sourceOrigin:MTLOriginMake(0,0,0) sourceSize:MTLSizeMake(512,512,1)
            toTexture:model.textures destinationSlice:i destinationLevel:0 destinationOrigin:MTLOriginMake(0,0,0)];
        [blit generateMipmapsForTexture:model.textures];[blit endEncoding];[command commit];
        // Startup only: finish the upload before the engine/audio session exists.
        [command waitUntilCompleted];return command.status==MTLCommandBufferStatusCompleted;
    }
    bool loadModel(id<MTLDevice> device,const std::string &directory,Model &model) {
        std::ifstream file(directory+"/car.mesh",std::ios::binary);
        char magic[4];uint32_t vertexCount,materialCount,wheelCount;
        file.read(magic,4);file.read(reinterpret_cast<char *>(&vertexCount),4);
        file.read(reinterpret_cast<char *>(&model.indexCount),4);file.read(reinterpret_cast<char *>(&materialCount),4);file.read(reinterpret_cast<char *>(&wheelCount),4);
        if(!file)return false;
        const bool textured=std::memcmp(magic,"ESC2",4)==0;
        if((!textured && std::memcmp(magic,"ESC1",4)) || !vertexCount || vertexCount>2000000 ||
            !model.indexCount || model.indexCount>6000000 || model.indexCount%3 || !materialCount || materialCount>64 || wheelCount!=5)return false;
        file.read(reinterpret_cast<char *>(model.wheels.data()),sizeof(model.wheels));
        std::vector<Material> materials(materialCount);int textureCount=0;
        for(auto &m:materials) {
            m.texture={-1,0,0,0};
            file.read(reinterpret_cast<char *>(&m),textured ? 64 : 48);
            if(!file || !std::isfinite(m.texture.x) || m.texture.x< -1 || m.texture.x>31 || std::floor(m.texture.x)!=m.texture.x)return false;
            textureCount=std::max(textureCount,int(m.texture.x)+1);
        }
        model.materials=[device newBufferWithBytes:materials.data() length:materials.size()*sizeof(Material) options:MTLResourceStorageModeShared];
        auto buffer=[&](size_t bytes) {
            std::vector<char> data(bytes);file.read(data.data(),bytes);
            return file ? [device newBufferWithBytes:data.data() length:bytes options:MTLResourceStorageModeShared] : nil;
        };
        model.vertices=buffer(vertexCount*40);model.indices=buffer(model.indexCount*4);
        return model.materials && model.vertices && model.indices && loadTextures(device,directory,textureCount,model);
    }
public:
    bool load(id<MTLDevice> device,id<MTLLibrary> library,const std::string &assets) {
        for(size_t i=0;i<models.size();++i)
            if(!loadModel(device,assets+"/vehicles/"+sound_ui::CarModels[i].directory,models[i]))return false;
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
        const auto index=size_t(body);const auto &model=models[index<models.size() ? index : 0];
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
        [encoder setFragmentTexture:model.textures atIndex:0];
        [encoder drawIndexedPrimitives:MTLPrimitiveTypeTriangle indexCount:model.indexCount indexType:MTLIndexTypeUInt32 indexBuffer:model.indices indexBufferOffset:0];
    }
};
#endif
