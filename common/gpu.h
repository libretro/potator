#ifndef __GPU_H__
#define __GPU_H__

#include "types.h"
#include "supervision.h" // SV_*

void gpu_init(void);
void gpu_reset(void);
void gpu_done(void);
void gpu_set_map_func(SV_MapRGBFunc func);
void gpu_set_color_scheme(int colorScheme);
void gpu_render_scanline(uint32 scanline, uint16 *backbuffer, uint8 innerx, uint8 size);
void gpu_set_ghosting(int frameCount);

#endif
