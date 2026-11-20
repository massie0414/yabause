/* This file is part of Yabause, licensed under GPL-2.0-or-later. */
#include "vdppalette.h"
#include "vdp1debug.h"
#include <stdio.h>

static u8 ram[0x80000], cram[0x1000];
static Vdp2 regs;
static VdpPaletteSource sources[VDP1_DEBUG_MAX_COMMANDS];
static int checks, failures;
#define CHECK(condition) do { ++checks; if (!(condition)) { \
   fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); ++failures; } } while (0)

static void colors(void)
{
   VdpPaletteColor c;
   VdpPaletteSource s = {VDP_PALETTE_BANK, 16, 0x1820, 0};
   static const u16 masks[] = {0x7ff,0x7ff,0x7ff,0x7ff,0x3ff,0x7ff,0x3ff,0x1ff,
      0x7f,0x3f,0x3f,0x3f,0xff,0xff,0xff,0xff};
   int i;
   memset(&regs, 0, sizeof(regs));
   T2WriteWord(cram, 0, 0x801f);
   T2WriteWord(cram, 4094, 0x7c00);
   CHECK(VdpPaletteReadColor(cram, 0, 0, &c) == 0 && c.argb == 0xfff80000 && c.raw == 0x801f);
   CHECK(VdpPaletteReadColor(cram, 1, 2047, &c) == 0 && c.argb == 0xff0000f8 && c.address == 4094);
   CHECK(VdpPaletteReadColor(cram, 1, 2048, &c) == 0 && c.cram_index == 0);
   T2WriteLong(cram, 4092, 0x80ab5634);
   CHECK(VdpPaletteReadColor(cram, 2, 1023, &c) == 0 && c.argb == 0xff3456ab && c.raw == 0x80ab5634);
   CHECK(VdpPaletteReadColor(cram, 2, 2047, &c) == 0 && c.address == 4092 && c.cram_index == 1023);
   CHECK(VdpPaletteReadColor(cram, 3, 0, &c) == -1);
   CHECK(VdpPaletteReadColor(NULL, 0, 0, &c) == -1);
   regs.RAMCTL = 0x1000;
   regs.CRAOFB = 0x70;
   for (i = 0; i < 16; ++i)
   {
      regs.SPCTL = i;
      CHECK(Vdp1PaletteReadColor(ram, cram, &regs, &s, 3, &c) == 0 &&
            c.cram_index == (((0x1823 & masks[i]) + 0x700) & 2047));
   }
   s.type = VDP_PALETTE_LUT; s.base = 0x7fff8;
   T1WriteWord(ram, 0x7fff8, 0x801f);
   T1WriteWord(ram, 0, 0x0020);
   regs.SPCTL = 0x20;
   CHECK(Vdp1PaletteReadColor(ram, cram, &regs, &s, 0, &c) == 0 &&
      c.argb == 0xfff80000 && c.cram_index == -1 && c.address == 0x7fff8);
   CHECK(Vdp1PaletteReadColor(ram, cram, &regs, &s, 4, &c) == 0 && c.cram_index == 0x720);
   regs.SPCTL = 0;
   CHECK(Vdp1PaletteReadColor(ram, cram, &regs, &s, 0, &c) == 0 && c.cram_index == 0x71f);
   CHECK(Vdp1PaletteReadColor(ram, cram, &regs, &s, 16, &c) == -1);
   s.type = VDP_PALETTE_FLAT; s.count = 1; s.base = 0x83e0; s.command = 0x40;
   regs.SPCTL = 0x20;
   CHECK(Vdp1PaletteReadColor(ram, cram, &regs, &s, 0, &c) == 0 &&
      c.argb == 0xff00f800 && c.address == 0x46 && c.cram_index == -1);
}

static void commands(void)
{
   int status, n, mode;
   memset(ram, 0, sizeof(ram));
   for (mode = 0; mode < 6; ++mode)
   {
      T1WriteWord(ram, mode * 32 + 4, mode << 3);
      T1WriteWord(ram, mode * 32 + 6, 0x123f);
      T1WriteWord(ram, mode * 32 + 10, 0x101);
   }
   T1WriteWord(ram, 6 * 32, 4); // polygon
   T1WriteWord(ram, 6 * 32 + 6, 0x801f);
   T1WriteWord(ram, 7 * 32, 0x8000);
   n = Vdp1PaletteCollect(ram, sources, VDP1_DEBUG_MAX_COMMANDS, &status);
   CHECK(n == 6 && status == VDP1_DEBUG_END);
   CHECK(sources[0].base == 0x1230 && sources[0].count == 16);
   CHECK(sources[1].type == VDP_PALETTE_LUT && sources[1].base == 0x91f8);
   CHECK(sources[2].base == 0x1200 && sources[2].count == 64);
   CHECK(sources[3].count == 128 && sources[4].count == 256);
   CHECK(sources[5].type == VDP_PALETTE_FLAT && sources[5].base == 0x801f);
   CHECK(Vdp1PaletteCollect(ram, sources, 1, &status) == 1 && status == VDP1_DEBUG_LIMIT);
   T1WriteWord(ram, 0, 0x5000); // skip and jump
   T1WriteWord(ram, 2, 8); // command 2
   CHECK(Vdp1PaletteCollect(ram, sources, VDP1_DEBUG_MAX_COMMANDS, &status) == 4 && sources[0].count == 64);
   T1WriteWord(ram, 0, 0x2000); T1WriteWord(ram, 2, 8); // call 2; return to 1/end
   T1WriteWord(ram, 32, 0x8000);
   T1WriteWord(ram, 64, 0x3000);
   CHECK(Vdp1PaletteCollect(ram, sources, VDP1_DEBUG_MAX_COMMANDS, &status) == 2 && status == VDP1_DEBUG_END);
   T1WriteWord(ram, 0, 0x1000); T1WriteWord(ram, 2, 0); // loop
   CHECK(Vdp1PaletteCollect(ram, sources, VDP1_DEBUG_MAX_COMMANDS, &status) == VDP1_DEBUG_MAX_COMMANDS && status == VDP1_DEBUG_LIMIT);
   T1WriteWord(ram, 0, 0xc);
   CHECK(Vdp1PaletteCollect(ram, sources, VDP1_DEBUG_MAX_COMMANDS, &status) == 0 && status == VDP1_DEBUG_INVALID);
   T1WriteWord(ram, 0, 0x5000); T1WriteWord(ram, 2, 0xffff);
   CHECK(Vdp1PaletteCollect(ram, sources, VDP1_DEBUG_MAX_COMMANDS, &status) == 0 && status == VDP1_DEBUG_INVALID);
}

static void layers(void)
{
   u32 bases[128];
   int n, layer;
   memset(ram, 0, sizeof(ram));
   memset(&regs, 0, sizeof(regs));
   regs.CHCTLA = 0x12; regs.BMPNA = 3; regs.CRAOFA = 7;
   CHECK(Vdp2PaletteCollect(&regs, ram, 0, bases, 128) == 1 && bases[0] == 0x200);
   regs.CHCTLA = 0x32;
   CHECK(Vdp2PaletteCollect(&regs, ram, 0, bases, 128) == 0);
   regs.CHCTLA = 0x72;
   CHECK(Vdp2PaletteCollect(&regs, ram, 0, bases, 128) == -1);
   regs.CHCTLA = 0x20; // 2048 color mode ignores per-tile palette data
   CHECK(Vdp2PaletteCollect(&regs, ram, 0, bases, 128) == 1 && bases[0] == 0x700);
   regs.CHCTLA = 0; regs.CRAOFA = 1; regs.PNCN0 = 0x80a0;
   T1WriteWord(ram, 0, 0x3000);
   n = Vdp2PaletteCollect(&regs, ram, 0, bases, 128);
   CHECK(n == 2 && bases[0] == 0x600 && bases[1] == 0x630); // supplement bits + CRAOF
   regs.PNCN0 = 0;
   T1WriteWord(ram, 0, 0x807f); // 2-word pattern; upper flags must not become palette
   T1WriteWord(ram, 2, 0x7fff);
   n = Vdp2PaletteCollect(&regs, ram, 0, bases, 128);
   CHECK(n == 2 && bases[0] == 0xf0 && bases[1] == 0x100);
   regs.CHCTLA = 0x10;
   n = Vdp2PaletteCollect(&regs, ram, 0, bases, 128);
   CHECK(n == 2 && bases[0] == 0 && bases[1] == 0x100);
   // All four normal layers use their own map, pattern-control and CRAOF fields.
   for (layer = 0; layer < 4; ++layer)
   {
      memset(ram, 0, sizeof(ram)); memset(&regs, 0, sizeof(regs));
      regs.PNCN0 = regs.PNCN1 = regs.PNCN2 = regs.PNCN3 = 0x8000;
      regs.MPABN0 = regs.MPABN1 = regs.MPABN2 = regs.MPABN3 = 1;
      regs.CRAOFA = (layer + 1) << (layer * 4);
      T1WriteWord(ram, 0x2000, 0x9000);
      CHECK(Vdp2PaletteCollect(&regs, ram, layer, bases, 128) == 2 &&
         bases[0] == (u32)(layer + 1) * 256 && bases[1] == (u32)(layer + 1) * 256 + 0x90);
   }
   // 16x16, 2x2-page planes and MPOFN must address the aligned plane.
   memset(ram, 0, sizeof(ram)); memset(&regs, 0, sizeof(regs));
   regs.CHCTLA = 1; regs.PNCN0 = 0x8000; regs.PLSZ = 3;
   regs.MPOFN = 1; regs.MPABN0 = regs.MPCDN0 = 0x0505;
   T1WriteWord(ram, 0x22000 + 0x1ffe, 0xe000);
   CHECK(Vdp2PaletteCollect(&regs, ram, 0, bases, 128) == 2 && bases[1] == 0xe0);
   // Rotation A/B and RBG1 use separate map registers and palette offsets.
   memset(ram, 0, sizeof(ram)); memset(&regs, 0, sizeof(regs));
   regs.PNCR = regs.PNCN0 = 0x8000;
   regs.MPABRA = 1; regs.MPOPRB = 2;
   regs.CRAOFB = 3; regs.CRAOFA = 5;
   T1WriteWord(ram, 0x2000, 0x1000); T1WriteWord(ram, 0x4000, 0x2000);
   CHECK(Vdp2PaletteCollect(&regs, ram, 4, bases, 128) == 2 && bases[1] == 0x310);
   CHECK(Vdp2PaletteCollect(&regs, ram, 5, bases, 128) == 2 && bases[1] == 0x320);
   CHECK(Vdp2PaletteCollect(&regs, ram, 6, bases, 128) == 2 && bases[1] == 0x520);
   regs.PLSZ = 1 << 10; regs.OVPNRA = 0x4000;
   CHECK(Vdp2PaletteCollect(&regs, ram, 4, bases, 128) == 3 && bases[2] == 0x340);
   CHECK(Vdp2PaletteCollect(&regs, ram, 7, bases, 128) == -1);
   CHECK(Vdp2PaletteCollect(&regs, ram, 0, bases, 127) == -1);
}

int main(void)
{
   colors(); commands(); layers();
   printf("VDP palette: %d checks, %d failures\n", checks, failures);
   return failures ? 1 : 0;
}
