#include "gles_target.h"
#include "solar_renderer.h"
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void check_shadows(void *renderer, const char *resource_root);

static GLuint target_texture, target_framebuffer;

void gles_target_create(void) {
  glGenTextures(1, &target_texture);
  glBindTexture(GL_TEXTURE_2D, target_texture);
  glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, GLES_TARGET_SIZE,
                 GLES_TARGET_SIZE);
  glGenFramebuffers(1, &target_framebuffer);
  glBindFramebuffer(GL_FRAMEBUFFER, target_framebuffer);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                         target_texture, 0);
  assert(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  assert(glGetError() == GL_NO_ERROR);
}

void gles_target_render(void *renderer, SolarCamera camera) {
  if (!solar_renderer_render_texture(renderer, target_texture, GLES_TARGET_SIZE,
                                     GLES_TARGET_SIZE, camera, 0, 0, 0)) {
    fprintf(stderr, "%s\n", solar_renderer_error());
    abort();
  }
}

// Rows are returned top-first, matching the Skia texture origin.
void gles_target_read(unsigned char *pixels) {
  unsigned char rows[GLES_TARGET_SIZE * GLES_TARGET_SIZE * 4];
  glBindFramebuffer(GL_FRAMEBUFFER, target_framebuffer);
  glPixelStorei(GL_PACK_ALIGNMENT, 1);
  glReadPixels(0, 0, GLES_TARGET_SIZE, GLES_TARGET_SIZE, GL_RGBA,
               GL_UNSIGNED_BYTE, rows);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  assert(glGetError() == GL_NO_ERROR);
  for (int y = 0; y < GLES_TARGET_SIZE; y++)
    memcpy(pixels + y * GLES_TARGET_SIZE * 4,
           rows + (GLES_TARGET_SIZE - 1 - y) * GLES_TARGET_SIZE * 4,
           GLES_TARGET_SIZE * 4);
}

static double center_brightness(const unsigned char *pixels) {
  double sum = 0;
  for (int y = 96; y < 160; y++)
    for (int x = 96; x < 160; x++) {
      const unsigned char *p = pixels + (y * GLES_TARGET_SIZE + x) * 4;
      sum += p[0] + p[1] + p[2];
    }
  return sum / (64 * 64 * 3);
}

typedef struct {
  char file[128];
  int id;
} Map;

// Reads the versioned manifest's file/renderer_id pairs in declaration order.
static int read_manifest(const char *root, Map *maps, int capacity) {
  char path[4096];
  snprintf(path, sizeof(path), "%s/notices/textures.json", root);
  FILE *input = fopen(path, "rb");
  assert(input);
  static char text[1 << 16];
  size_t size = fread(text, 1, sizeof(text) - 1, input);
  fclose(input);
  text[size] = 0;
  int count = 0;
  for (char *at = strstr(text, "\"file\": \""); at && count < capacity;
       at = strstr(at, "\"file\": \"")) {
    at += strlen("\"file\": \"");
    char *end = strchr(at, '"');
    assert(end && end - at < (long)sizeof(maps[count].file));
    memcpy(maps[count].file, at, end - at);
    maps[count].file[end - at] = 0;
    char *id = strstr(end, "\"renderer_id\": ");
    assert(id);
    maps[count++].id = atoi(id + strlen("\"renderer_id\": "));
    at = end;
  }
  return count;
}

int main(int argc, char **argv) {
  assert(argc == 2);
  EGLDisplay display = eglGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA,
                                             EGL_DEFAULT_DISPLAY, NULL);
  assert(display != EGL_NO_DISPLAY && eglInitialize(display, NULL, NULL));
  assert(eglBindAPI(EGL_OPENGL_ES_API));
  const EGLint attributes[] = {EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
                               EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_NONE};
  EGLConfig config;
  EGLint configs = 0;
  assert(eglChooseConfig(display, attributes, &config, 1, &configs) &&
         configs == 1);
  const EGLint version[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
  EGLContext context =
      eglCreateContext(display, config, EGL_NO_CONTEXT, version);
  assert(context != EGL_NO_CONTEXT);
  assert(eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, context));

  void *r = solar_renderer_create(display, context);
  if (!r) {
    fprintf(stderr, "%s\n", solar_renderer_error());
    return 1;
  }
  Map maps[64];
  int count = read_manifest(argv[1], maps, 64);
  assert(count == 37);
  for (int i = 0; i < count; i++) {
    char file[4096];
    snprintf(file, sizeof(file), "%s/textures/%s", argv[1], maps[i].file);
    if (!solar_renderer_load(r, maps[i].id, file)) {
      fprintf(stderr, "%s: %s\n", file, solar_renderer_error());
      return 1;
    }
  }
  gles_target_create();
  static unsigned char pixels[GLES_TARGET_SIZE * GLES_TARGET_SIZE * 4],
      repeated[GLES_TARGET_SIZE * GLES_TARGET_SIZE * 4];
  unsigned rendered = 0;
  for (int i = 0; i < count; i++) {
    int id = maps[i].id;
    if (id < 0)
      continue;
    SolarBody sun = {.axis_x = {1, 0, 0},
                     .axis_y = {0, 1, 0},
                     .axis_z = {0, 0, 1},
                     .radii = {.45, .45, .45},
                     .physical_radii = {696340, 696340, 696340},
                     .body = 10};
    SolarBody body = {.position = {3, 0, 0},
                      .axis_x = {1, 0, 0},
                      .axis_y = {0, 1, 0},
                      .axis_z = {0, 0, 1},
                      .radii = {.45, .45, .45},
                      .physical_position = {3 * 149597870.7, 0, 0},
                      .physical_radii = {6371, 6371, 6371},
                      .body = id,
                      .roughness = .8};
    if (id == 10)
      body = sun;
    SolarCamera camera = {
        {body.position.x - 2.5, -.6, .65}, body.position, {0, 0, 1}, .72};
    solar_renderer_begin(r);
    if (id != 10)
      solar_renderer_body(r, sun, 0);
    solar_renderer_body(r, body, 1);
    gles_target_render(r, camera);
    gles_target_read(pixels);
    double brightness = center_brightness(pixels);
    if (brightness <= 8) {
      fprintf(stderr, "Body %d rendered unexpectedly dark: %.1f\n", id,
              brightness);
      return 1;
    }
    if (id == 401 || id == 402 || (id >= 701 && id <= 705)) {
      const unsigned char *center = pixels + (128 * GLES_TARGET_SIZE + 128) * 4;
      assert(abs((int)center[0] - center[1]) <= 2);
      assert(abs((int)center[1] - center[2]) <= 2);
    }
    if (id == 399) {
      double day = brightness;
      gles_target_render(r, camera);
      gles_target_read(repeated);
      assert(memcmp(pixels, repeated, sizeof(pixels)) == 0);
      camera.eye.x = 5.5;
      gles_target_render(r, camera);
      gles_target_read(repeated);
      double night = center_brightness(repeated);
      assert(day > night * 1.5);
      printf("Earth daylight %.1f, night side %.1f; identical frames match "
             "byte for byte\n",
             day, night);
    }
    rendered++;
  }
  assert(rendered == 31);
  check_shadows(r, argv[1]);
  solar_renderer_close(r);
  GLint extensions = 0, clip_control = 0;
  glGetIntegerv(GL_NUM_EXTENSIONS, &extensions);
  for (GLint i = 0; i < extensions; i++)
    clip_control |= strcmp((const char *)glGetStringi(GL_EXTENSIONS, i),
                           "GL_EXT_clip_control") == 0;
  printf("GLES: five programs, 37 maps and %u body frames passed on %s "
         "(depth: %s)\n",
         rendered, (const char *)glGetString(GL_RENDERER),
         clip_control ? "EXT_clip_control" : "projection remap");
  eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
  eglDestroyContext(display, context);
  eglTerminate(display);
  return 0;
}
