#include "renderer_internal.h"
#include <math.h>
#include <string.h>

static SolarFloat4 point(SolarVector v) {
  return (SolarFloat4){(float)v.x, (float)v.y, (float)v.z, 1};
}
static SolarFloat4 direction(SolarVector v, double scale) {
  return (SolarFloat4){(float)(v.x * scale), (float)(v.y * scale),
                       (float)(v.z * scale), 0};
}
static SolarVector relative(SolarVector v, SolarVector origin) {
  return (SolarVector){v.x - origin.x, v.y - origin.y, v.z - origin.z};
}
static double length(SolarVector v) {
  return sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
}
static SolarVector normalize(SolarVector v) {
  double l = length(v);
  return (SolarVector){v.x / l, v.y / l, v.z / l};
}
static SolarVector cross(SolarVector a, SolarVector b) {
  return (SolarVector){a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z,
                       a.x * b.y - a.y * b.x};
}
static SolarMatrix identity(void) {
  return (SolarMatrix){{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}};
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

SolarUniforms solar_uniforms(SolarGLRenderer *r, SolarCamera camera,
                             float aspect) {
  SolarUniforms u;
  memset(&u, 0, sizeof(u));
  SolarVector forward = normalize(relative(camera.target, camera.eye));
  SolarVector right = normalize(cross(forward, camera.up));
  SolarVector up = cross(right, forward);
  SolarMatrix view = {{(float)right.x, (float)up.x, (float)-forward.x, 0,
                       (float)right.y, (float)up.y, (float)-forward.y, 0,
                       (float)right.z, (float)up.z, (float)-forward.z, 0, 0, 0,
                       0, 1}};
  float near =
            fmaxf((float)length(relative(camera.target, camera.eye)) * 0.0001f,
                  0.000001f),
        far = 1200;
  float y = 1 / tanf((float)camera.field_of_view * 0.5f);
  // Reversed [0, 1] depth as in the Metal adapter. Clip y is negated because
  // the Skia texture has a top-left origin while GL rasterizes bottom-up.
  SolarMatrix projection = {{y / aspect, 0, 0, 0, 0, -y, 0, 0, 0, 0,
                             near / (far - near), -1, 0, 0,
                             near * far / (far - near), 0}};
  if (!r->clipControl) {
    // z' = 2z - w maps the same depth values into GL's [-1, 1] clip range.
    SolarMatrix remap = {{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 2, 0, 0, 0, -1, 1}};
    projection = multiply(remap, projection);
  }
  u.vp = multiply(projection, view);
  u.eye = point(camera.eye);
  u.sun = point(relative((SolarVector){0, 0, 0}, camera.eye));
  u.options.x = (float)r->bodyCount;
  // GdkPixbuf always yields RGBA, so the cloud opacity lives in alpha.
  u.options.w = 0;
  return u;
}

static void upload(SolarGLRenderer *r, const SolarUniforms *u) {
  glBindBufferBase(GL_UNIFORM_BUFFER, 0, r->uniforms);
  glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(*u), u);
}

static void lighting(SolarGLRenderer *r, SolarUniforms *u, SolarBody receiver) {
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
      u->ring_size[ring] = (SolarFloat4){
          (float)(body.physical_radii.x * (ring == 0 ? 1.23 : 1.64)),
          (float)(body.physical_radii.x * (ring == 0 ? 2.4 : 2.0)), 1, 0};
    }
  }
}

static void transform(SolarGLRenderer *r, SolarUniforms *u, SolarBody body,
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
  u->material = (SolarFloat4){(float)body.body, (float)body.roughness,
                              (float)index, (float)body.selected};
  // GdkPixbuf expands grey maps to RGB, so no channel replication is needed.
  u->tint.z = 0;
}

static void bind(SolarGLRenderer *r, int unit, int32_t body, GLuint sampler) {
  glActiveTexture(GL_TEXTURE0 + unit);
  glBindTexture(GL_TEXTURE_2D, solar_gl_texture(r, body));
  glBindSampler(unit, sampler);
}

static void blend(int enabled) {
  if (enabled) {
    glEnable(GL_BLEND);
    glBlendEquation(GL_FUNC_ADD);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE,
                        GL_ONE_MINUS_SRC_ALPHA);
  } else {
    glDisable(GL_BLEND);
  }
}

static void depth(int write) {
  glEnable(GL_DEPTH_TEST);
  glDepthFunc(GL_GEQUAL);
  glDepthMask(write ? GL_TRUE : GL_FALSE);
}

void solar_draw_scene(SolarGLRenderer *r, SolarCamera camera, SolarUniforms u,
                      double et, int drawOrbits, int drawBelt) {
  glBindVertexArray(r->vertexArray);
  depth(0);
  blend(1);
  glUseProgram(r->programs[SOLAR_PARTICLES]);
  u.tint.w = 0;
  upload(r, &u);
  glDrawArrays(GL_POINTS, 0, 1600);
  if (drawBelt) {
    u.tint.w = 1;
    u.options.y = (float)(et / 86400.0);
    upload(r, &u);
    glDrawArrays(GL_POINTS, 0, 2200);
  }
  if (drawOrbits) {
    glUseProgram(r->programs[SOLAR_LINE]);
    glBindVertexArray(r->lineArray);
    glBindBuffer(GL_ARRAY_BUFFER, r->lines);
    upload(r, &u);
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
                                ? (SolarFloat4){0.33f, 0.80f, 0.90f, 0.75f}
                                : (SolarFloat4){0.30f, 0.47f, 0.59f, 0.25f};
      }
      glBufferSubData(GL_ARRAY_BUFFER, 0,
                      sizeof(SolarLineVertex) * orbit->count, vertices);
      glDrawArrays(GL_LINE_STRIP, 0, orbit->count);
    }
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(r->vertexArray);
  }
  depth(1);
  blend(0);
  glUseProgram(r->programs[SOLAR_SURFACE]);
  bind(r, 1, -3, r->mapSampler);
  bind(r, 3, -4, r->ringSampler);
  bind(r, 4, -5, r->ringSampler);
  bind(r, 5, -6, r->mapSampler);
  for (int i = 0; i < r->bodyCount; i++) {
    if (!r->visible[i])
      continue;
    SolarBody body = r->bodies[i];
    transform(r, &u, body, camera, i);
    upload(r, &u);
    bind(r, 0, body.body, r->mapSampler);
    bind(r, 2, body.body == 301 ? -2 : -1, r->mapSampler);
    glDrawArrays(GL_TRIANGLES, 0, 96 * 48 * 6);
  }
  depth(0);
  blend(1);
  glUseProgram(r->programs[SOLAR_RING]);
  for (int i = 0; i < r->bodyCount; i++) {
    SolarBody body = r->bodies[i];
    if (!r->visible[i] || (body.body != 699 && body.body != 799))
      continue;
    transform(r, &u, body, camera, i);
    u.tint = (SolarFloat4){body.body == 699 ? 1.23f : 1.64f,
                           body.body == 699 ? 2.4f : 2.0f, 0, 0};
    upload(r, &u);
    bind(r, 0, body.body == 699 ? -4 : -5, r->ringSampler);
    glDrawArrays(GL_TRIANGLES, 0, 384 * 6);
  }
  glUseProgram(r->programs[SOLAR_GLOW]);
  float distance =
      sqrtf(u.sun.x * u.sun.x + u.sun.y * u.sun.y + u.sun.z * u.sun.z);
  u.tint.x = fminf(511, 0.18f * 5.0f * u.options.z /
                            (2 * tanf((float)camera.field_of_view * 0.5f) *
                             fmaxf(distance, 0.001f)));
  upload(r, &u);
  glDrawArrays(GL_POINTS, 0, 1);

  // Return the borrowed context to default bindings for Skia.
  for (int unit = 5; unit >= 0; unit--) {
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D, 0);
    glBindSampler(unit, 0);
  }
  glBindBufferBase(GL_UNIFORM_BUFFER, 0, 0);
  glBindBuffer(GL_UNIFORM_BUFFER, 0);
  glBindVertexArray(0);
  glUseProgram(0);
  glDisable(GL_DEPTH_TEST);
  glDepthMask(GL_TRUE);
  blend(0);
}
