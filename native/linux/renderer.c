#include "renderer_internal.h"
// GLib headers come from plain include directories rather than -isystem.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-value"
#include <gdk-pixbuf/gdk-pixbuf.h>
#pragma clang diagnostic pop
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef GL_LOWER_LEFT_EXT
#define GL_LOWER_LEFT_EXT 0x8CA1
#endif
#ifndef GL_NEGATIVE_ONE_TO_ONE_EXT
#define GL_NEGATIVE_ONE_TO_ONE_EXT 0x935E
#endif
#ifndef GL_ZERO_TO_ONE_EXT
#define GL_ZERO_TO_ONE_EXT 0x935F
#endif

static _Thread_local char last_error[2048];

int solar_gl_fail(const char *message) {
  snprintf(last_error, sizeof(last_error), "%s",
           message ? message : "GLES operation failed");
  return 0;
}
const char *solar_renderer_error(void) { return last_error; }

static int current(SolarGLRenderer *r) {
  if (eglGetCurrentContext() == r->context)
    return 1;
  return solar_gl_fail("The GLES renderer was used outside its EGL context");
}

static GLuint compile(GLenum stage, const char *defines) {
  const char *sources[] = {"#version 300 es\n", defines, solar_shader_source()};
  GLuint shader = glCreateShader(stage);
  glShaderSource(shader, 3, sources, NULL);
  glCompileShader(shader);
  GLint ok = 0;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    char log[1536] = {0};
    glGetShaderInfoLog(shader, sizeof(log), NULL, log);
    snprintf(last_error, sizeof(last_error), "Shader %s failed: %s", defines,
             log);
    glDeleteShader(shader);
    return 0;
  }
  return shader;
}

static GLuint program(const char *name) {
  char vertex[64], fragment[64];
  snprintf(vertex, sizeof(vertex), "#define VERTEX\n#define %s\n", name);
  snprintf(fragment, sizeof(fragment), "#define FRAGMENT\n#define %s\n", name);
  GLuint vs = compile(GL_VERTEX_SHADER, vertex);
  if (!vs)
    return 0;
  GLuint fs = compile(GL_FRAGMENT_SHADER, fragment);
  if (!fs) {
    glDeleteShader(vs);
    return 0;
  }
  GLuint result = glCreateProgram();
  glAttachShader(result, vs);
  glAttachShader(result, fs);
  glLinkProgram(result);
  glDeleteShader(vs);
  glDeleteShader(fs);
  GLint ok = 0;
  glGetProgramiv(result, GL_LINK_STATUS, &ok);
  if (!ok) {
    char log[1536] = {0};
    glGetProgramInfoLog(result, sizeof(log), NULL, log);
    snprintf(last_error, sizeof(last_error), "Program %s failed: %s", name,
             log);
    glDeleteProgram(result);
    return 0;
  }
  glUniformBlockBinding(result, glGetUniformBlockIndex(result, "Uniforms"), 0);
  glUseProgram(result);
  const char *samplers[] = {"colorMap",   "cloudMap",   "normalMap",
                            "saturnRing", "uranusRing", "nightMap"};
  for (int unit = 0; unit < 6; unit++) {
    GLint location = glGetUniformLocation(result, samplers[unit]);
    if (location >= 0)
      glUniform1i(location, unit);
  }
  glUseProgram(0);
  return result;
}

static int extension(const char *name) {
  GLint count = 0;
  glGetIntegerv(GL_NUM_EXTENSIONS, &count);
  for (GLint i = 0; i < count; i++) {
    const char *value = (const char *)glGetStringi(GL_EXTENSIONS, (GLuint)i);
    if (value && strcmp(value, name) == 0)
      return 1;
  }
  return 0;
}

static GLuint sampler(GLenum wrap) {
  GLuint result = 0;
  glGenSamplers(1, &result);
  glSamplerParameteri(result, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
  glSamplerParameteri(result, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glSamplerParameteri(result, GL_TEXTURE_WRAP_S, wrap);
  glSamplerParameteri(result, GL_TEXTURE_WRAP_T, wrap);
  return result;
}

static void release(SolarGLRenderer *r) {
  for (int i = 0; i < SOLAR_PROGRAMS; i++)
    glDeleteProgram(r->programs[i]);
  for (int i = 0; i < r->textureCount; i++)
    glDeleteTextures(1, &r->textures[i].name);
  glDeleteBuffers(1, &r->uniforms);
  glDeleteBuffers(1, &r->lines);
  glDeleteVertexArrays(1, &r->vertexArray);
  glDeleteVertexArrays(1, &r->lineArray);
  glDeleteFramebuffers(1, &r->framebuffer);
  glDeleteRenderbuffers(1, &r->depth);
  glDeleteSamplers(1, &r->mapSampler);
  glDeleteSamplers(1, &r->ringSampler);
  free(r);
}

void *solar_renderer_create(void *display, void *context) {
  SolarGLRenderer *r = calloc(1, sizeof(SolarGLRenderer));
  if (!r) {
    solar_gl_fail("Unable to allocate the GLES renderer");
    return NULL;
  }
  r->display = display;
  r->context = context;
  if (!current(r)) {
    free(r);
    return NULL;
  }
  const char *names[SOLAR_PROGRAMS] = {"SPHERE", "RING", "LINE", "PARTICLE",
                                       "GLOW"};
  for (int i = 0; i < SOLAR_PROGRAMS; i++)
    if (!(r->programs[i] = program(names[i]))) {
      release(r);
      return NULL;
    }
  glGenBuffers(1, &r->uniforms);
  glBindBuffer(GL_UNIFORM_BUFFER, r->uniforms);
  glBufferData(GL_UNIFORM_BUFFER, sizeof(SolarUniforms), NULL, GL_STREAM_DRAW);
  glBindBuffer(GL_UNIFORM_BUFFER, 0);
  glGenVertexArrays(1, &r->vertexArray);
  glGenVertexArrays(1, &r->lineArray);
  glGenBuffers(1, &r->lines);
  glBindVertexArray(r->lineArray);
  glBindBuffer(GL_ARRAY_BUFFER, r->lines);
  glBufferData(GL_ARRAY_BUFFER, sizeof(SolarLineVertex) * SOLAR_ORBIT_POINTS,
               NULL, GL_STREAM_DRAW);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, sizeof(SolarLineVertex),
                        (const void *)0);
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(SolarLineVertex),
                        (const void *)sizeof(SolarFloat4));
  glBindVertexArray(0);
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  glGenFramebuffers(1, &r->framebuffer);
  glGenRenderbuffers(1, &r->depth);
  r->mapSampler = sampler(GL_REPEAT);
  r->ringSampler = sampler(GL_CLAMP_TO_EDGE);
  if (extension("GL_EXT_clip_control"))
    r->clipControl =
        (void (*)(GLenum, GLenum))eglGetProcAddress("glClipControlEXT");
  if (glGetError() != GL_NO_ERROR) {
    solar_gl_fail("Unable to create GLES renderer objects");
    release(r);
    return NULL;
  }
  return r;
}

GLuint solar_gl_texture(SolarGLRenderer *r, int32_t body) {
  for (int i = 0; i < r->textureCount; i++)
    if (r->textures[i].body == body)
      return r->textures[i].name;
  return 0;
}

int32_t solar_renderer_load(void *opaque, int32_t body, const char *filename) {
  SolarGLRenderer *r = opaque;
  if (!current(r))
    return 0;
  if (solar_gl_texture(r, body))
    return 1;
  if (r->textureCount >= SOLAR_MAX_TEXTURES)
    return solar_gl_fail("Too many renderer textures");
  GError *error = NULL;
  GdkPixbuf *image = gdk_pixbuf_new_from_file(filename, &error);
  if (!image) {
    snprintf(last_error, sizeof(last_error), "%s: %s", filename,
             error ? error->message : "unable to decode image");
    g_clear_error(&error);
    return 0;
  }
  GLint limit = 0;
  glGetIntegerv(GL_MAX_TEXTURE_SIZE, &limit);
  int width = gdk_pixbuf_get_width(image),
      height = gdk_pixbuf_get_height(image);
  if (limit > 0 && (width > limit || height > limit)) {
    // Keep the map's aspect ratio when the device limit is below its source.
    double factor = (double)limit / (width > height ? width : height);
    int scaledWidth = (int)(width * factor),
        scaledHeight = (int)(height * factor);
    GdkPixbuf *scaled = gdk_pixbuf_scale_simple(
        image, scaledWidth, scaledHeight, GDK_INTERP_BILINEAR);
    g_object_unref(image);
    if (!scaled)
      return solar_gl_fail("Unable to fit a map within GL_MAX_TEXTURE_SIZE");
    image = scaled;
    width = scaledWidth;
    height = scaledHeight;
  }
  int channels = gdk_pixbuf_get_n_channels(image),
      stride = gdk_pixbuf_get_rowstride(image);
  const guint8 *source = gdk_pixbuf_read_pixels(image);
  // GdkPixbuf expands grey and grey-alpha files to RGB(A); alpha stays linear.
  unsigned char *pixels = malloc((size_t)width * height * 4);
  if (!pixels) {
    g_object_unref(image);
    return solar_gl_fail("Unable to allocate map pixels");
  }
  for (int y = 0; y < height; y++)
    for (int x = 0; x < width; x++) {
      const guint8 *in = source + (size_t)y * stride + (size_t)x * channels;
      unsigned char *out = pixels + ((size_t)y * width + x) * 4;
      out[0] = in[0];
      out[1] = in[1];
      out[2] = in[2];
      out[3] = channels == 4 ? in[3] : 255;
    }
  g_object_unref(image);
  int levels = 1;
  for (int size = width > height ? width : height; size > 1; size >>= 1)
    levels++;
  // Normal maps and the cloud mask are linear data; colour maps are sRGB.
  GLenum format =
      body != -1 && body != -2 && body != -3 ? GL_SRGB8_ALPHA8 : GL_RGBA8;
  GLuint name = 0;
  glGenTextures(1, &name);
  glBindTexture(GL_TEXTURE_2D, name);
  glTexStorage2D(GL_TEXTURE_2D, levels, format, width, height);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
  glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGBA,
                  GL_UNSIGNED_BYTE, pixels);
  glGenerateMipmap(GL_TEXTURE_2D);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
  glBindTexture(GL_TEXTURE_2D, 0);
  free(pixels);
  if (glGetError() != GL_NO_ERROR) {
    glDeleteTextures(1, &name);
    snprintf(last_error, sizeof(last_error), "%s: unable to upload GL texture",
             filename);
    return 0;
  }
  r->textures[r->textureCount++] = (SolarTexture){body, name};
  return 1;
}

void solar_renderer_begin(void *opaque) {
  SolarGLRenderer *r = opaque;
  r->bodyCount = 0;
  r->orbitCount = 0;
  r->sunPosition = (SolarVector){0, 0, 0};
  r->sunRadius = 0;
}
void solar_renderer_body(void *opaque, SolarBody body, int32_t visible) {
  SolarGLRenderer *r = opaque;
  if (r->bodyCount < SOLAR_MAX_BODIES) {
    r->visible[r->bodyCount] = visible;
    r->bodies[r->bodyCount++] = body;
    if (body.body == 10) {
      r->sunPosition = body.physical_position;
      r->sunRadius = body.physical_radii.x;
    }
  }
}
void solar_renderer_orbit_point(void *opaque, int32_t slot, int32_t index,
                                SolarVector point) {
  SolarGLRenderer *r = opaque;
  if (slot >= 0 && slot < SOLAR_MAX_BODIES && index >= 0 &&
      index < SOLAR_ORBIT_POINTS) {
    r->orbits[slot].points[index] = point;
    r->orbits[slot].count = index + 1;
  }
}
void solar_renderer_orbit_place(void *opaque, int32_t slot, SolarVector center,
                                double scale, int32_t selected) {
  SolarGLRenderer *r = opaque;
  if (slot >= 0 && slot < SOLAR_MAX_BODIES) {
    r->orbits[slot].center = center;
    r->orbits[slot].scale = scale;
    r->orbits[slot].selected = selected;
    if (slot >= r->orbitCount)
      r->orbitCount = slot + 1;
  }
}

int32_t solar_renderer_render_texture(void *opaque, uint32_t texture,
                                      int32_t width, int32_t height,
                                      SolarCamera camera, double et,
                                      int32_t orbits, int32_t belt) {
  SolarGLRenderer *r = opaque;
  if (!current(r))
    return 0;
  if (width <= 0 || height <= 0)
    return solar_gl_fail("Invalid GLES render target size");
  while (glGetError() != GL_NO_ERROR) {
  }
  if (r->depthWidth != width || r->depthHeight != height) {
    glBindRenderbuffer(GL_RENDERBUFFER, r->depth);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT32F, width,
                          height);
    glBindRenderbuffer(GL_RENDERBUFFER, 0);
    r->depthWidth = width;
    r->depthHeight = height;
  }
  glBindFramebuffer(GL_FRAMEBUFFER, r->framebuffer);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                         texture, 0);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                            GL_RENDERBUFFER, r->depth);
  int complete =
      glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
  if (complete) {
    glViewport(0, 0, width, height);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_DITHER);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDepthMask(GL_TRUE);
    glClearColor(0.008f, 0.014f, 0.026f, 1);
    glClearDepthf(0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    if (r->clipControl)
      r->clipControl(GL_LOWER_LEFT_EXT, GL_ZERO_TO_ONE_EXT);
    SolarUniforms u = solar_uniforms(r, camera, (float)width / (float)height);
    u.options.z = (float)height;
    solar_draw_scene(r, camera, u, et, orbits, belt);
    if (r->clipControl)
      r->clipControl(GL_LOWER_LEFT_EXT, GL_NEGATIVE_ONE_TO_ONE_EXT);
  }
  // Leave no reference to the borrowed texture and only default bindings.
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, 0,
                         0);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  if (!complete)
    return solar_gl_fail("The borrowed GLES texture is not renderable");
  GLenum status = glGetError();
  if (status != GL_NO_ERROR) {
    snprintf(last_error, sizeof(last_error), "GLES scene failed: 0x%04x",
             status);
    return 0;
  }
  return 1;
}

void solar_renderer_close(void *opaque) {
  SolarGLRenderer *r = opaque;
  // GL objects belong to the borrowed context; without it only memory is freed.
  if (eglGetCurrentContext() == r->context)
    release(r);
  else
    free(r);
}
