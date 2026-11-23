#include "renderer_internal.h"
#include <math.h>

static vector_float3 vector(SolarVector v) {
  return (vector_float3){v.x, v.y, v.z};
}
static vector_float3 relative(SolarVector v, SolarVector origin) {
  return (vector_float3){v.x - origin.x, v.y - origin.y, v.z - origin.z};
}
static vector_float4 point(vector_float3 p) {
  return (vector_float4){p.x, p.y, p.z, 1};
}

SolarUniforms solar_uniforms(SolarMetalRenderer *r, SolarCamera camera,
                             float aspect) {
  SolarUniforms u = {0};
  vector_float3 forward = simd_normalize(relative(camera.target, camera.eye));
  vector_float3 right = simd_normalize(simd_cross(forward, vector(camera.up)));
  vector_float3 up = simd_cross(right, forward);
  matrix_float4x4 view = {{{right.x, up.x, -forward.x, 0},
                           {right.y, up.y, -forward.y, 0},
                           {right.z, up.z, -forward.z, 0},
                           {0, 0, 0, 1}}};
  float near = fmaxf(simd_length(relative(camera.target, camera.eye)) * 0.0001f,
                     0.000001f),
        far = 1200;
  float y = 1 / tanf(camera.field_of_view * 0.5);
  matrix_float4x4 projection = {{{y / aspect, 0, 0, 0},
                                 {0, y, 0, 0},
                                 {0, 0, near / (far - near), -1},
                                 {0, 0, near * far / (far - near), 0}}};
  u.vp = simd_mul(projection, view);
  u.eye = point(vector(camera.eye));
  u.sun = point(relative((SolarVector){0, 0, 0}, camera.eye));
  u.options.x = r->bodyCount;
  // A luminance/alpha PNG is an RG texture; its opacity lives in green.
  u.options.w = r.textures[@(-3)].pixelFormat == MTLPixelFormatRG8Unorm ? 1 : 0;
  for (int i = 0; i < r->bodyCount; i++) {
    SolarBody body = r->bodies[i];
    u.spheres[i] = point(relative(body.position, camera.eye));
    u.spheres[i].w = body.body == 10
                         ? -1
                         : fmax(body.radii.x, fmax(body.radii.y, body.radii.z));
    int ring = body.body == 699 ? 0 : (body.body == 799 ? 1 : -1);
    if (ring >= 0) {
      u.ring_center[ring] = u.spheres[i];
      u.ring_normal[ring] = point(vector(body.axis_z));
      u.ring_size[ring] =
          (vector_float4){body.radii.x * (ring == 0 ? 1.23 : 1.64),
                          body.radii.x * (ring == 0 ? 2.4 : 2.0), 1, 0};
    }
  }
  return u;
}
static void uniforms(id<MTLRenderCommandEncoder> encoder, SolarUniforms *u) {
  [encoder setVertexBytes:u length:sizeof(*u) atIndex:0];
  [encoder setFragmentBytes:u length:sizeof(*u) atIndex:0];
}
static void transform(SolarMetalRenderer *r, SolarUniforms *u, SolarBody body,
                      SolarCamera camera, int index) {
  u->model = matrix_identity_float4x4;
  u->model.columns[0] = point(vector(body.axis_x) * body.radii.x);
  u->model.columns[0].w = 0;
  u->model.columns[1] = point(vector(body.axis_y) * body.radii.y);
  u->model.columns[1].w = 0;
  u->model.columns[2] = point(vector(body.axis_z) * body.radii.z);
  u->model.columns[2].w = 0;
  u->model.columns[3] = point(relative(body.position, camera.eye));
  u->normal = matrix_identity_float4x4;
  u->normal.columns[0] = point(vector(body.axis_x) / body.radii.x);
  u->normal.columns[0].w = 0;
  u->normal.columns[1] = point(vector(body.axis_y) / body.radii.y);
  u->normal.columns[1].w = 0;
  u->normal.columns[2] = point(vector(body.axis_z) / body.radii.z);
  u->normal.columns[2].w = 0;
  u->material =
      (vector_float4){body.body, body.roughness, index, body.selected};
  MTLPixelFormat format = r.textures[@(body.body)].pixelFormat;
  u->tint.z =
      format == MTLPixelFormatR8Unorm || format == MTLPixelFormatR8Unorm_sRGB
          ? 1
          : 0;
}
void solar_draw_scene(SolarMetalRenderer *r, id<MTLRenderCommandEncoder> e,
                      SolarCamera camera, SolarUniforms u, double et,
                      int drawOrbits, int drawBelt) {
  [e setCullMode:MTLCullModeNone];
  [e setDepthStencilState:r.depthRead];
  [e setRenderPipelineState:r.particles];
  u.tint.w = 0;
  uniforms(e, &u);
  [e drawPrimitives:MTLPrimitiveTypePoint vertexStart:0 vertexCount:1600];
  if (drawBelt) {
    u.tint.w = 1;
    u.options.y = et / 86400.0;
    uniforms(e, &u);
    [e drawPrimitives:MTLPrimitiveTypePoint vertexStart:0 vertexCount:2200];
  }
  if (drawOrbits) {
    [e setRenderPipelineState:r.line];
    for (int i = 0; i < r->orbitCount; i++) {
      SolarOrbitLine *orbit = &r->orbits[i];
      if (orbit->scale <= 0 || orbit->count < 2)
        continue;
      SolarLineVertex vertices[SOLAR_ORBIT_POINTS];
      for (int j = 0; j < orbit->count; j++) {
        SolarVector p = orbit->points[j];
        p = (SolarVector){orbit->center.x + p.x * orbit->scale,
                          orbit->center.y + p.y * orbit->scale,
                          orbit->center.z + p.z * orbit->scale};
        vertices[j].position = point(relative(p, camera.eye));
        vertices[j].color = orbit->selected
                                ? (vector_float4){0.33, 0.80, 0.90, 0.75}
                                : (vector_float4){0.30, 0.47, 0.59, 0.25};
      }
      id<MTLBuffer> buffer =
          [r.device newBufferWithBytes:vertices
                                length:sizeof(SolarLineVertex) * orbit->count
                               options:MTLResourceStorageModeShared];
      [e setVertexBuffer:buffer offset:0 atIndex:1];
      uniforms(e, &u);
      [e drawPrimitives:MTLPrimitiveTypeLineStrip
            vertexStart:0
            vertexCount:orbit->count];
    }
  }
  [e setDepthStencilState:r.depthWrite];
  [e setRenderPipelineState:r.surface];
  [e setFragmentTexture:r.textures[@(-3)] atIndex:1];
  [e setFragmentTexture:r.textures[@(-4)] atIndex:3];
  [e setFragmentTexture:r.textures[@(-5)] atIndex:4];
  [e setFragmentTexture:r.textures[@(-6)] atIndex:5];
  for (int i = 0; i < r->bodyCount; i++) {
    SolarBody body = r->bodies[i];
    transform(r, &u, body, camera, i);
    uniforms(e, &u);
    [e setFragmentTexture:r.textures[@(body.body)] atIndex:0];
    [e setFragmentTexture:r.textures[@(body.body == 301 ? -2 : -1)] atIndex:2];
    [e drawPrimitives:MTLPrimitiveTypeTriangle
          vertexStart:0
          vertexCount:96 * 48 * 6];
  }
  [e setDepthStencilState:r.depthRead];
  [e setRenderPipelineState:r.ring];
  for (int i = 0; i < r->bodyCount; i++) {
    SolarBody body = r->bodies[i];
    if (body.body != 699 && body.body != 799)
      continue;
    transform(r, &u, body, camera, i);
    u.tint = (vector_float4){body.body == 699 ? 1.23 : 1.64,
                             body.body == 699 ? 2.4 : 2.0, 0, 0};
    uniforms(e, &u);
    [e setFragmentTexture:r.textures[@(body.body == 699 ? -4 : -5)] atIndex:0];
    [e drawPrimitives:MTLPrimitiveTypeTriangle
          vertexStart:0
          vertexCount:384 * 6];
  }
  [e setRenderPipelineState:r.glow];
  float distance = simd_length(u.sun.xyz);
  u.tint.x = fminf(
      511, 0.18f * 5.0f * u.options.z /
               (2 * tanf(camera.field_of_view * 0.5) * fmaxf(distance, 0.001)));
  uniforms(e, &u);
  [e drawPrimitives:MTLPrimitiveTypePoint vertexStart:0 vertexCount:1];
}
