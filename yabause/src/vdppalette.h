/* This file is part of Yabause, licensed under GPL-2.0-or-later. */
#ifndef VDPPALETTE_H
#define VDPPALETTE_H

#include "vdp2.h"

enum { VDP_PALETTE_CRAM, VDP_PALETTE_BANK, VDP_PALETTE_LUT, VDP_PALETTE_FLAT };
typedef struct {
   int type, count;
   u32 base, command;
} VdpPaletteSource;

typedef struct {
   u32 argb, raw, address;
   int cram_index; /* -1 for a direct RGB555 value in VDP1 VRAM */
} VdpPaletteColor;

/* Snapshot-only helpers; VRAM uses T1 layout and CRAM uses T2 layout.
   Colors match the emulator's unmodified CRAM decoding (no color offsets,
   transparency, shading or blending). Mode 2 has 1024 physical entries. */
int VdpPaletteReadColor(const u8 *cram, int mode, u32 index, VdpPaletteColor *out);
int Vdp1PaletteReadColor(const u8 *ram, const u8 *cram, const Vdp2 *regs,
                        const VdpPaletteSource *source, int index, VdpPaletteColor *out);
/* Bounded executed-command walk, including flat-color polygons and lines.
   RGB sprites have no palette. Status uses VDP1_DEBUG_END/INVALID/LIMIT. */
int Vdp1PaletteCollect(const u8 *ram, VdpPaletteSource *sources, int capacity, int *status);
/* NBG0..3, RBG0 A, RBG0 B, RBG1. Collect unique palette bases referenced by
   the complete configured maps, including offscreen tiles. Capacity >= 128.
   Returns 0 for direct RGB layers, -1 for invalid input/reserved format.
   Disabled layers can be inspected too; this is not final pixel visibility. */
int Vdp2PaletteCollect(const Vdp2 *regs, const u8 *ram, int layer, u32 *bases, int capacity);

#endif
