#ifndef GLES_TARGET_H
#define GLES_TARGET_H

#include "solar_renderer.h"
#include <GLES3/gl3.h>

enum { GLES_TARGET_SIZE = 256 };

void gles_target_create(void);
void gles_target_render(void *renderer, SolarCamera camera);
void gles_target_read(unsigned char *pixels);

#endif
