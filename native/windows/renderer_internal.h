#ifndef SOLAR_WINDOWS_RENDERER_INTERNAL_H
#define SOLAR_WINDOWS_RENDERER_INTERNAL_H

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "solar_renderer.h"
#include <d3d12.h>
#include <windows.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

enum {
  SOLAR_MAX_BODIES = 32,
  SOLAR_ORBIT_POINTS = 257,
  SOLAR_MAX_TEXTURES = 48,
  SOLAR_PROGRAMS = 5,
  // Frames recorded ahead of the GPU; each owns its allocator and upload ring.
  SOLAR_FRAMES = 3,
  SOLAR_UPLOAD_BYTES = 1 << 20
};
enum { SOLAR_SURFACE, SOLAR_RING, SOLAR_LINE, SOLAR_PARTICLES, SOLAR_GLOW };

// Column-major 4x4 matrices and float4 arrays match the HLSL cbuffer and the
// std140 block of the GLES port; viewport is appended for point expansion.
struct SolarMatrix {
  float m[16];
};
struct SolarFloat4 {
  float x, y, z, w;
};
struct SolarUniforms {
  SolarMatrix vp, model, normal, physical;
  SolarFloat4 sun, material, tint, eye, light_sun;
  SolarFloat4 spheres[SOLAR_MAX_BODIES];
  SolarFloat4 ring_center[2], ring_normal[2], ring_size[2];
  SolarFloat4 options, viewport;
};
struct SolarLineVertex {
  SolarFloat4 position, color;
};
struct SolarOrbitLine {
  SolarVector points[SOLAR_ORBIT_POINTS], center;
  double scale;
  int count, selected;
};
struct SolarTexture {
  int32_t body;
  ComPtr<ID3D12Resource> resource;
};
struct SolarFrame {
  ComPtr<ID3D12CommandAllocator> allocator;
  ComPtr<ID3D12Resource> upload, depth;
  unsigned char *mapped;
  size_t used;
  UINT64 fence;
};

struct SolarD3DRenderer {
  ComPtr<ID3D12Device> device;
  ComPtr<ID3D12CommandQueue> queue;
  ComPtr<ID3D12RootSignature> root;
  ComPtr<ID3D12PipelineState> pipelines[SOLAR_PROGRAMS];
  ComPtr<ID3D12GraphicsCommandList> commands;
  // Target and depth views are CPU-only; map views are shader visible with
  // slot 0 holding a null view for maps that are not loaded.
  ComPtr<ID3D12DescriptorHeap> targets, depths, maps;
  UINT mapStride;
  ComPtr<ID3D12Resource> depth;
  UINT64 depthWidth;
  UINT depthHeight;
  ComPtr<ID3D12Fence> fence;
  UINT64 fenceValue;
  HANDLE event;
  SolarFrame frames[SOLAR_FRAMES];
  int frameIndex;
  SolarBody bodies[SOLAR_MAX_BODIES];
  int visible[SOLAR_MAX_BODIES];
  SolarOrbitLine orbits[SOLAR_MAX_BODIES];
  int bodyCount, orbitCount;
  SolarVector sunPosition;
  double sunRadius;
  SolarTexture textures[SOLAR_MAX_TEXTURES];
  int textureCount;
};

const char *solar_shader_source();
int solar_d3d_fail(const char *message, HRESULT status = S_OK);
int solar_d3d_wait(SolarD3DRenderer *renderer, UINT64 value);
// Returns the shader-visible descriptor slot of a body map, or the null slot.
UINT solar_d3d_texture(SolarD3DRenderer *renderer, int32_t body);
int solar_d3d_load(SolarD3DRenderer *renderer, int32_t body,
                   const char *filename);
SolarUniforms solar_uniforms(SolarD3DRenderer *renderer, SolarCamera camera,
                             float width, float height);
// Records the scene; returns 0 when the frame's upload ring is exhausted.
int solar_draw_scene(SolarD3DRenderer *renderer, SolarFrame *frame,
                     SolarCamera camera, SolarUniforms uniforms, double et,
                     int drawOrbits, int drawBelt);

#endif
