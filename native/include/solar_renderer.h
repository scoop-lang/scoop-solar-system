#ifndef SOLAR_RENDERER_H
#define SOLAR_RENDERER_H

#include <stdint.h>

typedef struct {
  double x, y, z;
} SolarVector;
typedef struct {
  SolarVector eye, target, up;
  double field_of_view;
} SolarCamera;

typedef struct {
  SolarVector position, axis_x, axis_y, axis_z, radii;
  int32_t body, system, selected;
  double roughness;
} SolarBody;

void *solar_renderer_create(void *device, void *queue);
int32_t solar_renderer_load(void *renderer, int32_t body, const char *filename);
void solar_renderer_begin(void *renderer);
void solar_renderer_body(void *renderer, SolarBody body);
void solar_renderer_orbit_point(void *renderer, int32_t slot, int32_t index,
                                SolarVector point);
void solar_renderer_orbit_place(void *renderer, int32_t slot,
                                SolarVector center, double scale,
                                int32_t selected);
int32_t solar_renderer_render(void *renderer, void *texture, SolarCamera camera,
                              double et, int32_t orbits, int32_t belt);
void solar_renderer_close(void *renderer);
const char *solar_renderer_error(void);

#endif
