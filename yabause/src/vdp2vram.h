/* This file is part of Yabause, licensed under GPL-2.0-or-later. */
#ifndef VDP2VRAM_H
#define VDP2VRAM_H

#include "vdp2.h"

typedef struct {
   int color;             /* CHCTL color number: 4/8/16-bit index, RGB555, RGB888 */
   int tile_size;         /* 0 = bitmap, 8 or 16 = sequential character patterns */
   int width, height;     /* height is calculated from length for character patterns */
   u32 start, length;     /* byte offset and number of bytes, at most 512 KiB */
   u32 palette;           /* CRAM entry added to the pixel's color index */
   int transparency;
   int grayscale;
} Vdp2VramConfig;

/* No global emulator state is read or changed. RAM uses Yabause's T1/T2 layouts.
 * Output pixels are native u32 0xAARRGGBB, independent of the video backend. */
int Vdp2VramDimensions(const Vdp2VramConfig *config, int *width, int *height);
int Vdp2VramPixelAddress(const Vdp2VramConfig *config, int x, int y,
                        u32 *address, int *low_nibble);
int Vdp2VramRender(const Vdp2VramConfig *config, const u8 *ram,
                  const u8 *cram, int cram_mode, u32 *pixels, u32 capacity);
/* NBG0..3, RBG0 parameter A, RBG0 parameter B, RBG1 (parameter B).
 * Tile palettes vary per map entry; these presets supply only the CRAM offset. */
int Vdp2VramLayerConfig(const Vdp2 *regs, int layer, Vdp2VramConfig *config);

#endif
