/* This file is part of Yabause, licensed under GPL-2.0-or-later. */
#ifndef VDP1DEBUG_H
#define VDP1DEBUG_H

#include "core.h"

#define VDP1_DEBUG_VRAM_SIZE 0x80000
#define VDP1_DEBUG_MAX_COMMANDS 16384

typedef struct {
   u32 command_address;
   u32 address;
   u16 width, height;
   u16 pmod, color;
} Vdp1DebugImage;

enum { VDP1_DEBUG_END, VDP1_DEBUG_INVALID, VDP1_DEBUG_LIMIT };

/* All inputs are snapshots. No emulation state is read or changed.
   ram uses T1 byte order. The bounded walk includes only executed sprites.
   Return the number of images; status explains why command traversal stopped. */
int Vdp1DebugCollectImages(const u8 *ram, Vdp1DebugImage *images,
                          int capacity, int *status);

/* Palette entries and output pixels are numeric 0xAARRGGBB, independent of
   host byte order. CRAM words use T2 (native word) layout. */
u32 Vdp1DebugRGB555(u16 color);
void Vdp1DebugBuildPalette(const u16 *cram, int color_mode, u32 *palette);
u32 Vdp1DebugImageBytes(const Vdp1DebugImage *image);

/* Modes 0..5 are CMDPMOD color modes; 6/7 are raw 4/8-bit grayscale.
   Decode source order, without flipping, distortion, shading or blending.
   Reads wrap at the VRAM boundary. Returns -1 for invalid input or capacity. */
int Vdp1DebugDecodeImage(const u8 *ram, const u32 *palette, u16 spctl,
                        u16 craofb, const Vdp1DebugImage *image,
                        u32 *pixels, u32 capacity);

#endif
