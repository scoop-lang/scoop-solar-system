#include "renderer_internal.h"
#include <math.h>
#include <string.h>

static SolarFloat4 point(SolarVector v) {
  return {(float)v.x, (float)v.y, (float)v.z, 1};
}
static SolarFloat4 direction(SolarVector v, double scale) {
  return {(float)(v.x * scale), (float)(v.y * scale), (float)(v.z * scale), 0};
}
static SolarVector relative(SolarVector v, SolarVector origin) {
  return {v.x - origin.x, v.y - origin.y, v.z - origin.z};
}
static double length(SolarVector v) {
  return sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
}
static SolarVector normalize(SolarVector v) {
  double l = length(v);
  return {v.x / l, v.y / l, v.z / l};
}
static SolarVector cross(SolarVector a, SolarVector b) {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
static SolarMatrix identity() {
  return {{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}};
}
static void column(SolarMatrix *m, int index, SolarFloat4 value) {
  memcpy(&m->m[index * 4], &value, sizeof(value));
}
static SolarMatrix multiply(SolarMatrix a, SolarMatrix b) {
  SolarMatrix result;
  for (int c = 0; c < 4; c++)
    for (int r = 0; r < 4; r++) {
      float sum = 0;
      for (int k = 0; k < 4; k++)
        sum += a.m[k * 4 + r] * b.m[c * 4 + k];
      result.m[c * 4 + r] = sum;
    }
  return result;
}

SolarUniforms solar_uniforms(SolarD3DRenderer *r, SolarCamera camera,
                             float width, float height) {
  SolarUniforms u;
  memset(&u, 0, sizeof(u));
  SolarVector forward = normalize(relative(camera.target, camera.eye));
  SolarVector right = normalize(cross(forward, camera.up));
  SolarVector up = cross(right, forward);
  SolarMatrix view = {{(float)right.x, (float)up.x, (float)-forward.x, 0,
                       (float)right.y, (float)up.y, (float)-forward.y, 0,
                       (float)right.z, (float)up.z, (float)-forward.z, 0, 0, 0,
                       0, 1}};
  float nearPlane =
            fmaxf((float)length(relative(camera.target, camera.eye)) * 0.0001f,
                  0.000001f),
        farPlane = 1200;
  float y = 1 / tanf((float)camera.field_of_view * 0.5f);
  // Reversed [0, 1] depth as in the Metal adapter. D3D12 and the Skia texture
  // share the top-left origin, so clip y is not flipped.
  float aspect = width / height;
  SolarMatrix projection = {{y / aspect, 0, 0, 0, 0, y, 0, 0, 0, 0,
                             nearPlane / (farPlane - nearPlane), -1, 0, 0,
                             nearPlane * farPlane / (farPlane - nearPlane), 0}};
  u.vp = multiply(projection, view);
  u.eye = point(camera.eye);
  u.sun = point(relative({0, 0, 0}, camera.eye));
  u.options.x = (float)r->bodyCount;
  u.options.z = height;
  // WIC always yields RGBA, so the cloud opacity lives in alpha.
  u.options.w = 0;
  u.viewport = {width, height, 0, 0};
  return u;
}

// Copies data into the frame's upload ring and returns its GPU address.
static D3D12_GPU_VIRTUAL_ADDRESS upload(SolarFrame *frame, const void *data,
                                        size_t size) {
  // Constant buffer views require 256-byte alignment.
  size_t offset = (frame->used + 255) & ~(size_t)255;
  if (offset + size > SOLAR_UPLOAD_BYTES)
    return 0;
  memcpy(frame->mapped + offset, data, size);
  frame->used = offset + size;
  return frame->upload->GetGPUVirtualAddress() + offset;
}

static void lighting(SolarD3DRenderer *r, SolarUniforms *u,
                     SolarBody receiver) {
  // Subtract the physical origin in double precision before uploading floats.
  u->light_sun = point(relative(r->sunPosition, receiver.physical_position));
  u->light_sun.w = (float)r->sunRadius;
  u->physical = identity();
  column(&u->physical, 0,
         direction(receiver.axis_x, receiver.physical_radii.x));
  column(&u->physical, 1,
         direction(receiver.axis_y, receiver.physical_radii.y));
  column(&u->physical, 2,
         direction(receiver.axis_z, receiver.physical_radii.z));
  memset(u->ring_size, 0, sizeof(u->ring_size));
  for (int i = 0; i < r->bodyCount; i++) {
    SolarBody body = r->bodies[i];
    u->spheres[i] =
        point(relative(body.physical_position, receiver.physical_position));
    u->spheres[i].w =
        body.body == 10
            ? -1
            : (float)fmax(body.physical_radii.x,
                          fmax(body.physical_radii.y, body.physical_radii.z));
    int ring = body.body == 699 ? 0 : (body.body == 799 ? 1 : -1);
    if (ring >= 0) {
      u->ring_center[ring] = u->spheres[i];
      u->ring_normal[ring] = point(body.axis_z);
      u->ring_size[ring] = {
          (float)(body.physical_radii.x * (ring == 0 ? 1.23 : 1.64)),
          (float)(body.physical_radii.x * (ring == 0 ? 2.4 : 2.0)), 1, 0};
    }
  }
}

static void transform(SolarD3DRenderer *r, SolarUniforms *u, SolarBody body,
                      SolarCamera camera, int index) {
  lighting(r, u, body);
  u->model = identity();
  column(&u->model, 0, direction(body.axis_x, body.radii.x));
  column(&u->model, 1, direction(body.axis_y, body.radii.y));
  column(&u->model, 2, direction(body.axis_z, body.radii.z));
  column(&u->model, 3, point(relative(body.position, camera.eye)));
  u->normal = identity();
  column(&u->normal, 0, direction(body.axis_x, 1 / body.radii.x));
  column(&u->normal, 1, direction(body.axis_y, 1 / body.radii.y));
  column(&u->normal, 2, direction(body.axis_z, 1 / body.radii.z));
  u->material = {(float)body.body, (float)body.roughness, (float)index,
                 (float)body.selected};
  // WIC expands grey maps to RGB, so no channel replication is needed.
  u->tint.z = 0;
}

static void bind(SolarD3DRenderer *r, ID3D12GraphicsCommandList *c, int unit,
                 int32_t body) {
  D3D12_GPU_DESCRIPTOR_HANDLE handle =
      r->maps->GetGPUDescriptorHandleForHeapStart();
  handle.ptr += (UINT64)solar_d3d_texture(r, body) * r->mapStride;
  c->SetGraphicsRootDescriptorTable(unit + 1, handle);
}

static int uniforms(ID3D12GraphicsCommandList *c, SolarFrame *frame,
                    const SolarUniforms *u) {
  D3D12_GPU_VIRTUAL_ADDRESS address = upload(frame, u, sizeof(*u));
  if (!address)
    return 0;
  c->SetGraphicsRootConstantBufferView(0, address);
  return 1;
}

int solar_draw_scene(SolarD3DRenderer *r, SolarFrame *frame, SolarCamera camera,
                     SolarUniforms u, double et, int drawOrbits, int drawBelt) {
  ID3D12GraphicsCommandList *c = r->commands.Get();
  // Every table must be valid before a draw; unused units see the null view.
  for (int unit = 0; unit < 6; unit++)
    bind(r, c, unit, INT32_MIN);
  c->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  c->SetPipelineState(r->pipelines[SOLAR_PARTICLES].Get());
  u.tint.w = 0;
  if (!uniforms(c, frame, &u))
    return 0;
  c->DrawInstanced(1600 * 6, 1, 0, 0);
  if (drawBelt) {
    u.tint.w = 1;
    u.options.y = (float)(et / 86400.0);
    if (!uniforms(c, frame, &u))
      return 0;
    c->DrawInstanced(2200 * 6, 1, 0, 0);
  }
  if (drawOrbits) {
    c->SetPipelineState(r->pipelines[SOLAR_LINE].Get());
    c->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_LINESTRIP);
    if (!uniforms(c, frame, &u))
      return 0;
    for (int i = 0; i < r->orbitCount; i++) {
      SolarOrbitLine *orbit = &r->orbits[i];
      if (orbit->scale <= 0 || orbit->count < 2)
        continue;
      SolarLineVertex vertices[SOLAR_ORBIT_POINTS];
      for (int j = 0; j < orbit->count; j++) {
        SolarVector p = orbit->points[j];
        p = {orbit->center.x + p.x * orbit->scale,
             orbit->center.y + p.y * orbit->scale,
             orbit->center.z + p.z * orbit->scale};
        vertices[j].position = point(relative(p, camera.eye));
        vertices[j].color = orbit->selected
                                ? SolarFloat4{0.33f, 0.80f, 0.90f, 0.75f}
                                : SolarFloat4{0.30f, 0.47f, 0.59f, 0.25f};
      }
      UINT size = (UINT)(sizeof(SolarLineVertex) * orbit->count);
      D3D12_VERTEX_BUFFER_VIEW view = {upload(frame, vertices, size), size,
                                       sizeof(SolarLineVertex)};
      if (!view.BufferLocation)
        return 0;
      c->IASetVertexBuffers(0, 1, &view);
      c->DrawInstanced((UINT)orbit->count, 1, 0, 0);
    }
    c->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  }
  c->SetPipelineState(r->pipelines[SOLAR_SURFACE].Get());
  bind(r, c, 1, -3);
  bind(r, c, 3, -4);
  bind(r, c, 4, -5);
  bind(r, c, 5, -6);
  for (int i = 0; i < r->bodyCount; i++) {
    if (!r->visible[i])
      continue;
    SolarBody body = r->bodies[i];
    transform(r, &u, body, camera, i);
    if (!uniforms(c, frame, &u))
      return 0;
    bind(r, c, 0, body.body);
    bind(r, c, 2, body.body == 301 ? -2 : -1);
    c->DrawInstanced(96 * 48 * 6, 1, 0, 0);
  }
  c->SetPipelineState(r->pipelines[SOLAR_RING].Get());
  for (int i = 0; i < r->bodyCount; i++) {
    SolarBody body = r->bodies[i];
    if (!r->visible[i] || (body.body != 699 && body.body != 799))
      continue;
    transform(r, &u, body, camera, i);
    u.tint = {body.body == 699 ? 1.23f : 1.64f, body.body == 699 ? 2.4f : 2.0f,
              0, 0};
    if (!uniforms(c, frame, &u))
      return 0;
    bind(r, c, 0, body.body == 699 ? -4 : -5);
    c->DrawInstanced(384 * 6, 1, 0, 0);
  }
  c->SetPipelineState(r->pipelines[SOLAR_GLOW].Get());
  float distance =
      sqrtf(u.sun.x * u.sun.x + u.sun.y * u.sun.y + u.sun.z * u.sun.z);
  u.tint.x = fminf(511, 0.18f * 5.0f * u.options.z /
                            (2 * tanf((float)camera.field_of_view * 0.5f) *
                             fmaxf(distance, 0.001f)));
  if (!uniforms(c, frame, &u))
    return 0;
  c->DrawInstanced(6, 1, 0, 0);
  return 1;
}
