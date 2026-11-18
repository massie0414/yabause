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

#define VIDSOFT_VDP1_FRAMEBUFFER_MAX_PIXELS 0x40000

/* Snapshot the live software framebuffer, between emulation frames.
 * back=0 selects the display buffer, back=1 the drawing/CPU-access buffer.
 * pixels and optional raw each hold VIDSOFT_VDP1_FRAMEBUFFER_MAX_PIXELS entries.
 * pixels are 0xAARRGGBB, raw contains native 8/16-bit pixel values.
 * Decode RGB555/CRAM using current SPCTL/CRAOFB; ignore VDP2 priorities,
 * windows, blending and display switches. Zero is transparent; shadow/window
 * markers are magenta. Full native buffer dimensions are returned, without
 * display cropping, rotation or interlace scaling. No command replay occurs.
 * Returns -1 (and zero dimensions) unless the software renderer is active.
 */
int VIDSoftGetVdp1Framebuffer(int back, u32 *pixels, u16 *raw,
   int *width, int *height, int *pixel_size);

void VIDSoftSetBilinear(int b); 

void VIDSoftSetNumPriorityThreads(int num);

void Vdp2GetInterlaceInfo(int * start_line, int * line_increment);

void VIDSoftSetNumLayerThreads(int num);

void VIDSoftSetVdp1ThreadEnable(int b);

void VidsoftWaitForVdp1Thread();

#endif
