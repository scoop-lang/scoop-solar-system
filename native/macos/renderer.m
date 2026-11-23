#include "renderer_internal.h"
#import <Foundation/Foundation.h>
#import <MetalKit/MetalKit.h>
#include <stdio.h>

static _Thread_local char last_error[2048];
@implementation SolarMetalRenderer
@end

int solar_metal_fail(NSString *message) {
  snprintf(last_error, sizeof(last_error), "%s",
           message.UTF8String ?: "Metal operation failed");
  return 0;
}
const char *solar_renderer_error(void) { return last_error; }

static id<MTLRenderPipelineState> pipeline(SolarMetalRenderer *r,
                                           id<MTLLibrary> library,
                                           NSString *vertex, NSString *fragment,
                                           BOOL blend) {
  MTLRenderPipelineDescriptor *d = [MTLRenderPipelineDescriptor new];
  d.vertexFunction = [library newFunctionWithName:vertex];
  d.fragmentFunction = [library newFunctionWithName:fragment];
  d.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
  d.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;
  if (blend) {
    d.colorAttachments[0].blendingEnabled = YES;
    d.colorAttachments[0].sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
    d.colorAttachments[0].destinationRGBBlendFactor =
        MTLBlendFactorOneMinusSourceAlpha;
    d.colorAttachments[0].sourceAlphaBlendFactor = MTLBlendFactorOne;
    d.colorAttachments[0].destinationAlphaBlendFactor =
        MTLBlendFactorOneMinusSourceAlpha;
  }
  NSError *error = nil;
  id<MTLRenderPipelineState> result =
      [r.device newRenderPipelineStateWithDescriptor:d error:&error];
  if (!result)
    solar_metal_fail(error.localizedDescription);
  return result;
}

void *solar_renderer_create(void *device, void *queue) {
  @autoreleasepool {
    SolarMetalRenderer *r = [SolarMetalRenderer new];
    r.device = (__bridge id<MTLDevice>)device;
    r.queue = (__bridge id<MTLCommandQueue>)queue;
    r.textures = [NSMutableDictionary new];
    NSError *error = nil;
    id<MTLLibrary> library =
        [r.device newLibraryWithSource:solar_shader_source()
                               options:nil
                                 error:&error];
    if (!library) {
      solar_metal_fail(error.localizedDescription);
      return NULL;
    }
    r.surface = pipeline(r, library, @"sphere_vertex", @"sphere_fragment", NO);
    r.ring = pipeline(r, library, @"ring_vertex", @"ring_fragment", YES);
    r.line = pipeline(r, library, @"line_vertex", @"line_fragment", YES);
    r.particles =
        pipeline(r, library, @"particle_vertex", @"particle_fragment", YES);
    r.glow = pipeline(r, library, @"glow_vertex", @"glow_fragment", YES);
    if (!r.surface || !r.ring || !r.line || !r.particles || !r.glow)
      return NULL;
    MTLDepthStencilDescriptor *depth = [MTLDepthStencilDescriptor new];
    depth.depthCompareFunction = MTLCompareFunctionGreaterEqual;
    depth.depthWriteEnabled = YES;
    r.depthWrite = [r.device newDepthStencilStateWithDescriptor:depth];
    depth.depthWriteEnabled = NO;
    r.depthRead = [r.device newDepthStencilStateWithDescriptor:depth];
    return (__bridge_retained void *)r;
  }
}

int32_t solar_renderer_load(void *opaque, int32_t body, const char *filename) {
  @autoreleasepool {
    SolarMetalRenderer *r = (__bridge SolarMetalRenderer *)opaque;
    if (r.textures[@(body)])
      return 1;
    MTKTextureLoader *loader =
        [[MTKTextureLoader alloc] initWithDevice:r.device];
    NSError *error = nil;
    NSURL *url =
        [NSURL fileURLWithPath:[NSString stringWithUTF8String:filename]];
    NSDictionary *options = @{
      MTKTextureLoaderOptionSRGB : @(body != -1 && body != -2 && body != -3),
      MTKTextureLoaderOptionGenerateMipmaps : @YES,
      MTKTextureLoaderOptionOrigin : MTKTextureLoaderOriginTopLeft
    };
    id<MTLTexture> texture = [loader newTextureWithContentsOfURL:url
                                                         options:options
                                                           error:&error];
    if (!texture)
      return solar_metal_fail(error.localizedDescription);
    r.textures[@(body)] = texture;
    return 1;
  }
}

void solar_renderer_begin(void *opaque) {
  SolarMetalRenderer *r = (__bridge SolarMetalRenderer *)opaque;
  r->bodyCount = 0;
  r->orbitCount = 0;
}
void solar_renderer_body(void *opaque, SolarBody body) {
  SolarMetalRenderer *r = (__bridge SolarMetalRenderer *)opaque;
  if (r->bodyCount < SOLAR_MAX_BODIES)
    r->bodies[r->bodyCount++] = body;
}
void solar_renderer_orbit_point(void *opaque, int32_t slot, int32_t index,
                                SolarVector point) {
  SolarMetalRenderer *r = (__bridge SolarMetalRenderer *)opaque;
  if (slot >= 0 && slot < SOLAR_MAX_BODIES && index >= 0 &&
      index < SOLAR_ORBIT_POINTS) {
    r->orbits[slot].points[index] = point;
    if (index == 0)
      r->orbits[slot].count = 1;
    else
      r->orbits[slot].count = index + 1;
  }
}
void solar_renderer_orbit_place(void *opaque, int32_t slot, SolarVector center,
                                double scale, int32_t selected) {
  SolarMetalRenderer *r = (__bridge SolarMetalRenderer *)opaque;
  if (slot >= 0 && slot < SOLAR_MAX_BODIES) {
    r->orbits[slot].center = center;
    r->orbits[slot].scale = scale;
    r->orbits[slot].selected = selected;
    if (slot >= r->orbitCount)
      r->orbitCount = slot + 1;
  }
}

int32_t solar_renderer_render(void *opaque, void *texture, SolarCamera camera,
                              double et, int32_t orbits, int32_t belt) {
  @autoreleasepool {
    SolarMetalRenderer *r = (__bridge SolarMetalRenderer *)opaque;
    id<MTLTexture> target = (__bridge id<MTLTexture>)texture;
    if (!r.depth || r.depth.width != target.width ||
        r.depth.height != target.height) {
      MTLTextureDescriptor *d = [MTLTextureDescriptor
          texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float
                                       width:target.width
                                      height:target.height
                                   mipmapped:NO];
      d.storageMode = MTLStorageModePrivate;
      d.usage = MTLTextureUsageRenderTarget;
      r.depth = [r.device newTextureWithDescriptor:d];
      if (!r.depth)
        return solar_metal_fail(@"Unable to create depth target");
    }
    MTLRenderPassDescriptor *pass =
        [MTLRenderPassDescriptor renderPassDescriptor];
    pass.colorAttachments[0].texture = target;
    pass.colorAttachments[0].loadAction = MTLLoadActionClear;
    pass.colorAttachments[0].storeAction = MTLStoreActionStore;
    pass.colorAttachments[0].clearColor =
        MTLClearColorMake(0.008, 0.014, 0.026, 1);
    pass.depthAttachment.texture = r.depth;
    pass.depthAttachment.loadAction = MTLLoadActionClear;
    pass.depthAttachment.storeAction = MTLStoreActionDontCare;
    pass.depthAttachment.clearDepth = 0;
    id<MTLCommandBuffer> commands = [r.queue commandBuffer];
    id<MTLRenderCommandEncoder> encoder =
        [commands renderCommandEncoderWithDescriptor:pass];
    if (!encoder)
      return solar_metal_fail(@"Unable to create scene encoder");
    SolarUniforms u =
        solar_uniforms(r, camera, (float)target.width / (float)target.height);
    u.options.z = (float)target.height;
    solar_draw_scene(r, encoder, camera, u, et, orbits, belt);
    [encoder endEncoding];
    [commands commit];
    return 1;
  }
}
void solar_renderer_close(void *opaque) {
  @autoreleasepool {
    SolarMetalRenderer *r = (__bridge_transfer SolarMetalRenderer *)opaque;
    id<MTLCommandBuffer> complete = [r.queue commandBuffer];
    [complete commit];
    [complete waitUntilCompleted];
  }
}
