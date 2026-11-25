#include "gles_target.h"
#include "solar_ephemeris.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// GLES counterpart of shadows.m; the assertions are kept identical.
enum { IMAGE_SIZE = GLES_TARGET_SIZE };
typedef struct {
  unsigned char bytes[IMAGE_SIZE * IMAGE_SIZE * 4];
} Image;
static const double AU = 149597870.7;

static SolarVector add(SolarVector a, SolarVector b) {
  return (SolarVector){a.x + b.x, a.y + b.y, a.z + b.z};
}
static SolarVector subtract(SolarVector a, SolarVector b) {
  return (SolarVector){a.x - b.x, a.y - b.y, a.z - b.z};
}
static SolarVector scale(SolarVector a, double b) {
  return (SolarVector){a.x * b, a.y * b, a.z * b};
}
static double length(SolarVector a) {
  return sqrt(a.x * a.x + a.y * a.y + a.z * a.z);
}

static SolarBody body_at(int id, double radius, double et) {
  SolarState state = solar_spice_state(id, et);
  assert(state.valid);
  SolarVector shape = {1, 1, 1};
  if (id == 599)
    shape = (SolarVector){1.022, 1.022, .955};
  if (id == 699)
    shape = (SolarVector){1.035, 1.035, .931};
  return (SolarBody){.position = scale(state.position, 1 / AU),
                     .axis_x = state.axis_x,
                     .axis_y = state.axis_y,
                     .axis_z = state.axis_z,
                     .radii = scale(shape, .035 * pow(radius / 6371, .45)),
                     .physical_position = state.position,
                     .physical_radii = scale(shape, radius),
                     .body = id,
                     .roughness = .8};
}

static void place_system(SolarBody *body, SolarVector barycenter,
                         double factor) {
  body->position =
      scale(add(barycenter,
                scale(subtract(body->physical_position, barycenter), factor)),
            1 / AU);
}

static SolarCamera daylight_camera(SolarBody body, double distance) {
  SolarVector light =
      scale(body.physical_position, -1 / length(body.physical_position));
  return (SolarCamera){
      add(body.position, scale(light, body.radii.x * distance)),
      body.position,
      {0, 0, 1},
      .72};
}

static Image *capture(void *renderer, SolarCamera camera, SolarBody receiver,
                      const SolarBody *occluder) {
  SolarBody sun = {.physical_radii = {696340, 696340, 696340}, .body = 10};
  solar_renderer_begin(renderer);
  solar_renderer_body(renderer, sun, 0);
  solar_renderer_body(renderer, receiver, 1);
  if (occluder)
    solar_renderer_body(renderer, *occluder, 0);
  gles_target_render(renderer, camera);
  Image *image = malloc(sizeof(Image));
  assert(image);
  gles_target_read(image->bytes);
  return image;
}

static int same(const Image *a, const Image *b) {
  return memcmp(a->bytes, b->bytes, sizeof(a->bytes)) == 0;
}

typedef struct {
  int darkened_pixels;
  double minimum_ratio, mean_difference;
} Difference;

static Difference difference(const Image *before, const Image *after) {
  const unsigned char *a = before->bytes, *b = after->bytes;
  Difference result = {0, 1, 0};
  for (int i = 0; i < IMAGE_SIZE * IMAGE_SIZE; i++) {
    int first = a[4 * i] + a[4 * i + 1] + a[4 * i + 2];
    int second = b[4 * i] + b[4 * i + 1] + b[4 * i + 2];
    if (first - second > 6)
      result.darkened_pixels++;
    if (first > 120)
      result.minimum_ratio = fmin(result.minimum_ratio, (double)second / first);
    for (int channel = 0; channel < 3; channel++)
      result.mean_difference += abs(a[4 * i + channel] - b[4 * i + channel]);
  }
  result.mean_difference /= IMAGE_SIZE * IMAGE_SIZE * 3;
  return result;
}

static void report(const char *label, Difference delta) {
  printf("%s: %d shadow pixels, darkest lit-pixel ratio %.3f, mean RGB "
         "difference %.5f\n",
         label, delta.darkened_pixels, delta.minimum_ratio,
         delta.mean_difference);
}

void check_shadows(void *renderer, const char *resource_root) {
  assert(solar_spice_open(resource_root));

  const char *dates[] = {"2027-01-07T10:05:08", "2027-02-06T16:00:00",
                         "2027-08-02T10:07:00"};
  double annular_ratio = 0;
  for (int test = 0; test < 3; test++) {
    SolarTime time = solar_spice_parse(dates[test]);
    assert(time.valid);
    SolarBody earth = body_at(399, 6371, time.value);
    SolarBody moon = body_at(301, 1737.4, time.value);
    SolarState barycenter = solar_spice_state(3, time.value);
    assert(barycenter.valid);
    place_system(&earth, barycenter.position, 52);
    place_system(&moon, barycenter.position, 52);
    SolarCamera camera = daylight_camera(earth, 3.6);
    Image *clear = capture(renderer, camera, earth, NULL);
    Image *shadow = capture(renderer, camera, earth, &moon);
    Difference delta = difference(clear, shadow);
    report(dates[test], delta);
    if (test == 0) {
      // The reported false eclipse must disappear over the entire visible
      // Earth.
      assert(same(clear, shadow));
    } else {
      // Real eclipses must survive the fix; annular light must exceed totality.
      assert(delta.darkened_pixels > 1000);
      assert(delta.minimum_ratio < .8);
      if (test == 1) {
        annular_ratio = delta.minimum_ratio;
        assert(annular_ratio > .15);
      } else {
        assert(delta.minimum_ratio < annular_ratio * .6);
      }
    }

    // An arbitrarily large, repositioned display Moon cannot alter
    // illumination.
    moon.radii = scale(moon.radii, 100);
    moon.position = add(moon.position, (SolarVector){7, -3, 1});
    Image *changed_moon = capture(renderer, camera, earth, &moon);
    assert(same(shadow, changed_moon));

    // Scaling the display Earth and its viewing distance preserves the same
    // surface image.
    earth.radii = scale(earth.radii, 4);
    camera.eye =
        add(camera.target, scale(subtract(camera.eye, camera.target), 4));
    Image *changed_earth = capture(renderer, camera, earth, &moon);
    assert(difference(shadow, changed_earth).mean_difference < .2);
  }

  const struct {
    const char *utc;
    int receiver, occluder;
    double receiver_radius, occluder_radius;
  } eclipses[] = {{"2026-03-03T11:33:00", 301, 399, 1737.4, 6371},
                  {"2027-01-07T11:00:00", 599, 501, 69911, 1821.6},
                  {"2027-01-08T08:10:00", 501, 599, 1821.6, 69911}};
  for (unsigned i = 0; i < sizeof(eclipses) / sizeof(eclipses[0]); i++) {
    SolarTime time = solar_spice_parse(eclipses[i].utc);
    assert(time.valid);
    SolarBody receiver =
        body_at(eclipses[i].receiver, eclipses[i].receiver_radius, time.value);
    SolarBody occluder =
        body_at(eclipses[i].occluder, eclipses[i].occluder_radius, time.value);
    SolarCamera camera = daylight_camera(receiver, 3.6);
    Image *clear = capture(renderer, camera, receiver, NULL);
    Image *shadow = capture(renderer, camera, receiver, &occluder);
    Difference delta = difference(clear, shadow);
    report(eclipses[i].utc, delta);
    assert(delta.darkened_pixels > 5);
    assert(delta.minimum_ratio < .8);
  }

  SolarTime time = solar_spice_parse("2027-01-07T10:05:08");
  SolarBody saturn = body_at(699, 58232, time.value);
  SolarCamera camera = daylight_camera(saturn, 7);
  camera.eye.z += saturn.radii.x * 3;
  Image *rings = capture(renderer, camera, saturn, NULL);
  saturn.radii = scale(saturn.radii, 4);
  camera.eye =
      add(camera.target, scale(subtract(camera.eye, camera.target), 4));
  Image *larger_rings = capture(renderer, camera, saturn, NULL);
  assert(difference(rings, larger_rings).mean_difference < .2);
  solar_spice_close();
  puts("Physical shadows: solar/lunar and Jovian eclipses, hidden occluders, "
       "display-scale invariance and Saturn rings passed");
}
