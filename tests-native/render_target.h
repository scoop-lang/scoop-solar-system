#ifndef RENDER_TARGET_H
#define RENDER_TARGET_H

#include "solar_renderer.h"

#ifdef __cplusplus
extern "C" {
#endif

// Square offscreen target shared by the GLES and D3D12 checks.
enum { TARGET_SIZE = 256 };

void target_render(void *renderer, SolarCamera camera);
// Rows are returned top-first as RGBA8, matching the Skia texture origin.
void target_read(unsigned char *pixels);
void check_shadows(void *renderer, const char *resource_root);

#ifdef __cplusplus
}
#endif

#endif
