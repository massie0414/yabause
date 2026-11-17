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

/* Render independent, uncomposited software previews between emulation frames.
 * Each caller-owned buffer holds 704 * 512 u32 pixels (0xAARRGGBB).
 * Order: VDP1, NBG0 (RBG1 in rotation mode), NBG1, NBG2, NBG3, RBG0.
 * Transparent pixels are zero; width/height describe the packed image.
 * The current VDP1 command list is replayed into an empty scratch framebuffer.
 * Returns 0 on success, -1 if unavailable. Does not change display toggles.
 */
int VIDSoftRenderLayerPreviews(u32 *layers[6], int *width, int *height);

void VIDSoftSetBilinear(int b); 

void VIDSoftSetNumPriorityThreads(int num);

void Vdp2GetInterlaceInfo(int * start_line, int * line_increment);

void VIDSoftSetNumLayerThreads(int num);

void VIDSoftSetVdp1ThreadEnable(int b);

void VidsoftWaitForVdp1Thread();

#endif
