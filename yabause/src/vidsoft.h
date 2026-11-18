/*  Copyright 2006 Theo Berkau

    This file is part of Yabause.

    Yabause is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    Yabause is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Yabause; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA
*/

#ifndef VIDSOFT_H
#define VIDSOFT_H

#include "vdp1.h"

#define VIDCORE_SOFT   2

extern pixel_t *dispbuffer;

extern VideoInterface_struct VIDSoft;

void VIDSoftVdp2DrawScreen(int screen);

#define VIDSOFT_LAYER_PREVIEW_COUNT 7

/* Render independent, uncomposited software previews between emulation frames.
 * Each caller-owned buffer holds 704 * 512 u32 pixels (0xAARRGGBB).
 * Order: VDP1, NBG0, NBG1, NBG2, NBG3, RBG0, RBG1.
 * NBG0 stays transparent while the game uses RBG1 mode.
 * Transparent pixels are zero; width/height describe the packed image.
 * The current VDP1 command list is replayed into an empty scratch framebuffer.
 * If non-NULL, distorted_sprite_count receives one per executed distorted sprite
 * command (including type 3), excluding skipped commands. It is reset per capture.
 * Returns 0 on success, -1 if unavailable. Does not change display toggles.
 */
int VIDSoftRenderLayerPreviews(u32 *layers[VIDSOFT_LAYER_PREVIEW_COUNT], int *width, int *height,
   u32 *distorted_sprite_count);

/* RBG0 coordinates from the same rasterization as the layer image. A zero
 * parameter means no sample (disabled, window, coefficient or screen-over).
 * Geometry is recorded before texture transparency/priority is applied.
 * x/y are signed pre-wrap coordinates; mapx/mapy are before tile lookup.
 * kx/ky are the effective 16.16 source scale factors, not display magnification.
 */
typedef struct {
   int x, y;
   s32 kx, ky;
   u16 mapx, mapy;
   u8 parameter; /* 1 = A, 2 = B */
} VIDSoftRbg0Sample;

typedef struct {
   VIDSoftRbg0Sample *samples; /* Caller-owned 704 * 512 entries. */
   int enabled, mode;
   int coefficient_enabled[2];
} VIDSoftRbg0Preview;

int VIDSoftRenderLayerPreviewsWithRbg0(u32 *layers[VIDSOFT_LAYER_PREVIEW_COUNT],
   int *width, int *height, u32 *distorted_sprite_count, VIDSoftRbg0Preview *rotation);

void VIDSoftSetBilinear(int b); 

void VIDSoftSetNumPriorityThreads(int num);

void Vdp2GetInterlaceInfo(int * start_line, int * line_increment);

void VIDSoftSetNumLayerThreads(int num);

void VIDSoftSetVdp1ThreadEnable(int b);

void VidsoftWaitForVdp1Thread();

#endif
