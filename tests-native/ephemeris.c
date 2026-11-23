#include "SpiceUsr.h"
#include "solar_ephemeris.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>

static double length(SolarVector v) {
  return sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
}
int main(int argc, char **argv) {
  assert(argc == 2);
  if (!solar_spice_open(argv[1])) {
    puts(solar_spice_error());
    return 1;
  }
  SolarTime minimum = solar_spice_parse("1900-01-01T00:00:00");
  SolarTime maximum = solar_spice_parse("2100-01-01T00:00:00");
  SpiceInt count;
  ktotal_c("SPK", &count);
  unsigned covered = 0;
  for (SpiceInt i = 0; i < count; i++) {
    char file[1024], type[32], source[1024];
    SpiceInt handle;
    SpiceBoolean found;
    kdata_c(i, "SPK", sizeof(file), sizeof(type), sizeof(source), file, type,
            source, &handle, &found);
    assert(found);
    SPICEINT_CELL(objects, 64);
    SPICEDOUBLE_CELL(coverage, 10000);
    scard_c(0, &objects);
    spkobj_c(file, &objects);
    for (SpiceInt j = 0; j < card_c(&objects); j++) {
      scard_c(0, &coverage);
      spkcov_c(file, SPICE_CELL_ELEM_I(&objects, j), &coverage);
      assert(!failed_c());
      if (!wnincd_c(minimum.value, maximum.value, &coverage)) {
        printf("Coverage gap: %s target %d required %.3f..%.3f; %d intervals\n",
               file, (int)SPICE_CELL_ELEM_I(&objects, j), minimum.value,
               maximum.value, (int)wncard_c(&coverage));
        for (SpiceInt interval = 0; interval < wncard_c(&coverage);
             interval++) {
          SpiceDouble left, right;
          wnfetd_c(&coverage, interval, &left, &right);
          printf("  %.3f..%.3f\n", left, right);
        }
        return 1;
      }
      covered++;
    }
  }
  assert(covered == 40);
  printf("%u body/reference-center segments cover the full UTC interval "
         "without gaps\n",
         covered);
  const int ids[] = {10,  199, 299, 399, 499, 599, 699, 799, 899, 999, 301,
                     401, 402, 501, 502, 503, 504, 601, 602, 603, 604, 605,
                     606, 608, 701, 702, 703, 704, 705, 801, 901};
  unsigned samples = 0;
  for (int year = 1900; year <= 2100; year++) {
    char utc[32];
    snprintf(utc, sizeof(utc), "%04d-01-01T00:00:00", year);
    SolarTime t = solar_spice_parse(utc);
    assert(t.valid);
    for (unsigned i = 0; i < sizeof(ids) / sizeof(ids[0]); i++) {
      SolarState s = solar_spice_state(ids[i], t.value);
      if (!s.valid) {
        printf("%d %d %s\n", year, ids[i], solar_spice_error());
        return 1;
      }
      assert(fabs(length(s.axis_x) - 1) < 1e-12);
      assert(fabs(length(s.axis_y) - 1) < 1e-12);
      assert(fabs(length(s.axis_z) - 1) < 1e-12);
      assert(isfinite(length(s.position)));
      samples++;
    }
  }
  SolarTime t = solar_spice_parse("2000-01-01T12:00:00");
  assert(t.valid);
  SolarState earth = solar_spice_state(399, t.value);
  assert(length(earth.position) > 147e6 && length(earth.position) < 153e6);
  SolarOrbit orbit = solar_spice_orbit(399, 10, t.value);
  assert(orbit.valid);
  SolarVector p = solar_spice_conic(orbit, 0);
  assert(length((SolarVector){p.x - earth.position.x, p.y - earth.position.y,
                              p.z - earth.position.z}) < 0.01);
  printf("%u body/epoch states and rotations passed; Earth J2000 UTC %.9f %.9f "
         "%.9f km\n",
         samples, earth.position.x, earth.position.y, earth.position.z);
  SolarTime before = solar_spice_parse("2016-12-31T23:59:59");
  SolarTime after = solar_spice_parse("2017-01-01T00:00:00");
  assert(fabs(after.value - before.value - 2) < 1e-5);
  assert(!solar_spice_parse("not a date").valid);
  assert(solar_spice_parse("2026-11-24T00:00:00").valid);
  solar_spice_close();
  return 0;
}
