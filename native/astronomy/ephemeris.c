#include "SpiceUsr.h"
#include "solar_ephemeris.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static char error_text[2048];
static char time_text[64];

/* N0067's f2c layer otherwise attempts update access even for input kernels.
 * The application only reads kernels, including those inside a signed bundle.
 * These modes affect this statically linked reader, not the preparation tools.
 */
extern char *f__w_mode[4];

static int checked(void) {
  if (!failed_c())
    return 1;
  getmsg_c("LONG", sizeof(error_text), error_text);
  reset_c();
  return 0;
}

const char *solar_spice_error(void) { return error_text; }

int32_t solar_spice_open(const char *root) {
  f__w_mode[2] = "rb";
  f__w_mode[3] = "r";
  erract_c("SET", 0, "RETURN");
  errprt_c("SET", 0, "NONE");
  const char *files[] = {"naif0012.tls",     "pck00011.tpc",
                         "gm_de440.tpc",     "solar-de442s.bsp",
                         "solar-mar099.bsp", "solar-jup365.bsp",
                         "solar-sat441.bsp", "solar-ura111l.bsp",
                         "solar-nep097.bsp", "solar-plu060.bsp"};
  kclear_c();
  for (unsigned i = 0; i < sizeof(files) / sizeof(files[0]); ++i) {
    char path[4096];
    if (snprintf(path, sizeof(path), "%s/kernels/%s", root, files[i]) >=
        (int)sizeof(path)) {
      snprintf(error_text, sizeof(error_text), "Resource path is too long");
      return 0;
    }
    furnsh_c(path);
    if (!checked()) {
      kclear_c();
      return 0;
    }
  }
  return 1;
}

void solar_spice_close(void) { kclear_c(); }

SolarTime solar_spice_parse(const char *utc) {
  SolarTime result = {0};
  str2et_c(utc, &result.value);
  result.valid = checked();
  return result;
}

const char *solar_spice_format(double et) {
  et2utc_c(et, "ISOC", 0, sizeof(time_text), time_text);
  if (!checked())
    return NULL;
  return time_text;
}

SolarState solar_spice_state(int32_t body, double et) {
  SolarState result = {0};
  double state[6] = {0}, light_time;
  if (body != 10)
    spkez_c(body, et, "ECLIPJ2000", "NONE", 10, state, &light_time);
  if (!checked())
    return result;
  result.position = (SolarVector){state[0], state[1], state[2]};
  result.velocity = (SolarVector){state[3], state[4], state[5]};
  if (body < 10) {
    result.axis_x.x = result.axis_y.y = result.axis_z.z = 1;
  } else {
    char body_name[64], frame[80];
    SpiceBoolean found;
    bodc2n_c(body, sizeof(body_name), body_name, &found);
    if (!found) {
      snprintf(error_text, sizeof(error_text),
               "No body-fixed frame for NAIF %d", body);
      return result;
    }
    snprintf(frame, sizeof(frame), "IAU_%s", body_name);
    double rotation[3][3];
    pxform_c(frame, "ECLIPJ2000", et, rotation);
    if (!checked())
      return result;
    result.axis_x =
        (SolarVector){rotation[0][0], rotation[1][0], rotation[2][0]};
    result.axis_y =
        (SolarVector){rotation[0][1], rotation[1][1], rotation[2][1]};
    result.axis_z =
        (SolarVector){rotation[0][2], rotation[1][2], rotation[2][2]};
  }
  result.valid = 1;
  return result;
}

SolarOrbit solar_spice_orbit(int32_t body, int32_t center, double et) {
  SolarOrbit result = {0};
  double state[6], light_time, gm, elements[8];
  SpiceInt count;
  spkez_c(body, et, "ECLIPJ2000", "NONE", center, state, &light_time);
  bodvcd_c(center, "GM", 1, &count, &gm);
  if (!checked())
    return result;
  if (center == 9 && (body == 999 || body == 901)) {
    double companion_gm;
    bodvcd_c(body == 999 ? 901 : 999, "GM", 1, &count, &companion_gm);
    if (!checked())
      return result;
    double fraction = companion_gm / gm;
    gm *= fraction * fraction * fraction;
  }
  oscelt_c(state, et, gm, elements);
  if (!checked())
    return result;
  memcpy(&result, elements, sizeof(elements));
  double semi_major = elements[0] / (1 - elements[1]);
  result.period = 2 * pi_c() * sqrt(semi_major * semi_major * semi_major / gm);
  result.valid = isfinite(result.period) && result.period > 0;
  if (!result.valid)
    snprintf(error_text, sizeof(error_text),
             "Expected an elliptical reference orbit");
  return result;
}

SolarVector solar_spice_conic(SolarOrbit orbit, double fraction) {
  double elements[8], state[6];
  memcpy(elements, &orbit, sizeof(elements));
  conics_c(elements, orbit.epoch + fraction * orbit.period, state);
  if (!checked())
    return (SolarVector){NAN, NAN, NAN};
  return (SolarVector){state[0], state[1], state[2]};
}
