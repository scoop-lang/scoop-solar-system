#ifndef SOLAR_RENDERER_H
#define SOLAR_RENDERER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  double x, y, z;
} SolarVector;
typedef struct {
  SolarVector eye, target, up;
  double field_of_view;
} SolarCamera;

typedef struct {
  // Display positions and radii use AU-sized scene units.
  SolarVector position, axis_x, axis_y, axis_z, radii;
  // Physical geometry uses Sun-centered ECLIPJ2000 positions and radii in km.
  SolarVector physical_position, physical_radii;
  int32_t body, system, selected;
  double roughness;
} SolarBody;

// Metal passes its device and command queue; GLES passes the EGL display and
// the context that is current during every later call; D3D12 passes its device
// and direct command queue, which the renderer retains.
void *solar_renderer_create(void *device, void *queue);
int32_t solar_renderer_load(void *renderer, int32_t body, const char *filename);
void solar_renderer_begin(void *renderer);
// Supply every physical body, including the Sun, even when its mesh is hidden.
void solar_renderer_body(void *renderer, SolarBody body, int32_t visible);
void solar_renderer_orbit_point(void *renderer, int32_t slot, int32_t index,
                                SolarVector point);
void solar_renderer_orbit_place(void *renderer, int32_t slot,
                                SolarVector center, double scale,
                                int32_t selected);
// Metal renders into a borrowed MTLTexture.
int32_t solar_renderer_render(void *renderer, void *texture, SolarCamera camera,
                              double et, int32_t orbits, int32_t belt);
// GLES renders into a borrowed RGBA8 texture name of the current context.
int32_t solar_renderer_render_texture(void *renderer, uint32_t texture,
                                      int32_t width, int32_t height,
                                      SolarCamera camera, double et,
                                      int32_t orbits, int32_t belt);
// D3D12 renders into a borrowed ID3D12Resource on the retained queue and
// returns it to the given resource state before returning.
int32_t solar_renderer_render_resource(void *renderer, void *resource,
                                       uint32_t state, SolarCamera camera,
                                       double et, int32_t orbits, int32_t belt);
// GLES requires the creating context to be current.
void solar_renderer_close(void *renderer);
const char *solar_renderer_error(void);

#ifdef __cplusplus
}
#endif

#endif
