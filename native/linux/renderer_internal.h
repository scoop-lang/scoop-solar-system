#include "solar_renderer.h"
#include <EGL/egl.h>
#include <GLES3/gl3.h>

enum {
  SOLAR_MAX_BODIES = 32,
  SOLAR_ORBIT_POINTS = 257,
  SOLAR_MAX_TEXTURES = 48,
  SOLAR_PROGRAMS = 5
};
enum { SOLAR_SURFACE, SOLAR_RING, SOLAR_LINE, SOLAR_PARTICLES, SOLAR_GLOW };

// Column-major 4x4 matrices and vec4 arrays match the std140 Uniforms block.
typedef struct {
  float m[16];
} SolarMatrix;
typedef struct {
  float x, y, z, w;
} SolarFloat4;
typedef struct {
  SolarMatrix vp, model, normal, physical;
  SolarFloat4 sun, material, tint, eye, light_sun;
  SolarFloat4 spheres[SOLAR_MAX_BODIES];
  SolarFloat4 ring_center[2], ring_normal[2], ring_size[2];
  SolarFloat4 options;
} SolarUniforms;
typedef struct {
  SolarFloat4 position, color;
} SolarLineVertex;
typedef struct {
  SolarVector points[SOLAR_ORBIT_POINTS], center;
  double scale;
  int count, selected;
} SolarOrbitLine;
typedef struct {
  int32_t body;
  GLuint name;
} SolarTexture;

typedef struct {
  EGLDisplay display;
  EGLContext context;
  SolarBody bodies[SOLAR_MAX_BODIES];
  int visible[SOLAR_MAX_BODIES];
  SolarOrbitLine orbits[SOLAR_MAX_BODIES];
  int bodyCount, orbitCount;
  SolarVector sunPosition;
  double sunRadius;
  GLuint programs[SOLAR_PROGRAMS];
  GLuint uniforms, lines, vertexArray, lineArray, framebuffer, depth;
  GLuint mapSampler, ringSampler;
  GLsizei depthWidth, depthHeight;
  // Without EXT_clip_control the [0, 1] reversed depth is remapped to GL's
  // [-1, 1] clip range in the projection.
  void (*clipControl)(GLenum origin, GLenum depth);
  SolarTexture textures[SOLAR_MAX_TEXTURES];
  int textureCount;
} SolarGLRenderer;

const char *solar_shader_source(void);
int solar_gl_fail(const char *message);
GLuint solar_gl_texture(SolarGLRenderer *renderer, int32_t body);
SolarUniforms solar_uniforms(SolarGLRenderer *renderer, SolarCamera camera,
                             float aspect);
void solar_draw_scene(SolarGLRenderer *renderer, SolarCamera camera,
                      SolarUniforms uniforms, double et, int drawOrbits,
                      int drawBelt);
