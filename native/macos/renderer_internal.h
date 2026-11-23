#include "solar_renderer.h"
#import <Metal/Metal.h>
#include <simd/simd.h>

enum { SOLAR_MAX_BODIES = 32, SOLAR_ORBIT_POINTS = 257 };
typedef struct {
  matrix_float4x4 vp, model, normal;
  vector_float4 sun, material, tint, eye;
  vector_float4 spheres[SOLAR_MAX_BODIES];
  vector_float4 ring_center[2], ring_normal[2], ring_size[2];
  vector_float4 options;
} SolarUniforms;
typedef struct {
  vector_float4 position, color;
} SolarLineVertex;
typedef struct {
  SolarVector points[SOLAR_ORBIT_POINTS], center;
  double scale;
  int count, selected;
} SolarOrbitLine;

@interface SolarMetalRenderer : NSObject {
@public
  SolarBody bodies[SOLAR_MAX_BODIES];
  SolarOrbitLine orbits[SOLAR_MAX_BODIES];
  int bodyCount, orbitCount;
}
@property(nonatomic, strong) id<MTLDevice> device;
@property(nonatomic, strong) id<MTLCommandQueue> queue;
@property(nonatomic, strong) id<MTLRenderPipelineState> surface, ring, line,
    particles, glow;
@property(nonatomic, strong) id<MTLDepthStencilState> depthWrite, depthRead;
@property(nonatomic, strong) id<MTLTexture> depth;
@property(nonatomic, strong)
    NSMutableDictionary<NSNumber *, id<MTLTexture>> *textures;
@end

NSString *solar_shader_source(void);
int solar_metal_fail(NSString *message);
SolarUniforms solar_uniforms(SolarMetalRenderer *renderer, SolarCamera camera,
                             float aspect);
void solar_draw_scene(SolarMetalRenderer *renderer,
                      id<MTLRenderCommandEncoder> encoder, SolarCamera camera,
                      SolarUniforms uniforms, double et, int drawOrbits,
                      int drawBelt);
