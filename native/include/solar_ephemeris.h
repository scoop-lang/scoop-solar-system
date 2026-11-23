#ifndef SOLAR_EPHEMERIS_H
#define SOLAR_EPHEMERIS_H

#include "solar_renderer.h"

typedef struct {
  SolarVector position, velocity, axis_x, axis_y, axis_z;
  int32_t valid;
} SolarState;

typedef struct {
  double value;
  int32_t valid;
} SolarTime;

typedef struct {
  double periapsis, eccentricity, inclination, node, argument, anomaly, epoch,
      gm;
  double period;
  int32_t valid;
} SolarOrbit;

int32_t solar_spice_open(const char *resource_root);
void solar_spice_close(void);
const char *solar_spice_error(void);
SolarTime solar_spice_parse(const char *utc);
const char *solar_spice_format(double et);
SolarState solar_spice_state(int32_t body, double et);
SolarOrbit solar_spice_orbit(int32_t body, int32_t center, double et);
SolarVector solar_spice_conic(SolarOrbit orbit, double fraction);
const char *solar_resource_root(void);

#endif
