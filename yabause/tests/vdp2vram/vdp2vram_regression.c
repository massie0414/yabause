#include <stdio.h>
#include <stdlib.h>
#include "vdp2vram.h"

static u8 ram[0x80000], cram[0x1000];
static u32 pixels[1024 * 4096 + 1];
static int failures, checks;

static void check(int condition, const char *message)
{
   ++checks;
   if (!condition) { fprintf(stderr, "FAIL: %s\n", message); ++failures; }
}

static Vdp2VramConfig config(int color)
{
   Vdp2VramConfig c = { 0 };
   c.color = color;
   c.tile_size = 8;
   c.width = 16;
   c.height = 256;
   c.length = 0x80000;
   return c;
}

static void render(Vdp2VramConfig *c, int mode)
{
   check(Vdp2VramRender(c, ram, cram, mode, pixels, 1024 * 4096) == 0, "decode succeeds");
}

static void color_tests(void)
{
   Vdp2VramConfig c = config(0);
   int mode;
   ram[0] = 0x12;
   c.palette = 0x120;
   for (mode = 0; mode < 2; ++mode)
   {
      T2WriteWord(cram, 0x121 * 2, 0x001f);
      T2WriteWord(cram, 0x122 * 2, 0x03e0);
      render(&c, mode);
      check(pixels[0] == 0xfff80000 && pixels[1] == 0xff00f800, "4bpp high nibble first, palette offset, RGB order");
   }
   T2WriteLong(cram, 0x121 * 4, 0x80123456);
   render(&c, 2);
   check(pixels[0] == 0xff563412, "32-bit CRAM color byte order");
   c.color = 1;
   c.palette = 0x700;
   ram[0] = 0xab;
   T2WriteWord(cram, 0x7ab * 2, 0x7c00);
   render(&c, 1);
   check(pixels[0] == 0xff0000f8, "8bpp palette lookup");
   c.color = 2;
   c.palette = 0x100;
   T1WriteWord(ram, 0, 0x0723);
   T2WriteWord(cram, 0x23 * 2, 0x7fff);
   render(&c, 1);
   check(pixels[0] == 0xfff8f8f8, "16-bit index and CRAM wrap");
   T2WriteLong(cram, 0x23 * 4, 0x00123456);
   render(&c, 2);
   check(pixels[0] == 0xff563412, "32-bit CRAM wrap");
   c.color = 3;
   T1WriteWord(ram, 0, 0x801f);
   T1WriteWord(ram, 2, 0x03e0);
   c.transparency = 1;
   render(&c, 1);
   check(pixels[0] == 0xfff80000 && pixels[1] == 0, "RGB555 transparency uses MSB");
   c.transparency = 0;
   render(&c, 1);
   check(pixels[1] == 0xff00f800, "transparency can be disabled to inspect stored colors");
   c.color = 4;
   T1WriteLong(ram, 0, 0x80123456);
   T1WriteLong(ram, 4, 0x00123456);
   c.transparency = 1;
   render(&c, 1);
   check(pixels[0] == 0xff563412 && pixels[1] == 0, "RGB888 storage and transparency");
   c.color = 0;
   c.grayscale = 1;
   ram[0] = 0x0f;
   render(&c, 1);
   check(pixels[0] == 0 && pixels[1] == 0xffffffff, "indexed zero transparency and grayscale");
   c.transparency = 0;
   render(&c, 1);
   check(pixels[0] == 0xff000000, "grayscale black is visible when transparency is off");
}

static void layout_tests(void)
{
   Vdp2VramConfig c = config(0);
   int w, h, nibble, color;
   u32 address;
   check(!Vdp2VramPixelAddress(&c, 1, 0, &address, &nibble) && address == 0 && nibble,
      "hover identifies low nibble");
   check(!Vdp2VramPixelAddress(&c, 8, 0, &address, &nibble) && address == 32,
      "second 8x8 tile begins after 32 bytes");
   check(!Vdp2VramPixelAddress(&c, 0, 8, &address, &nibble) && address == 64,
      "tile atlas row stride");
   c.tile_size = 16;
   check(!Vdp2VramPixelAddress(&c, 8, 0, &address, &nibble) && address == 32, "16x16 top right cell");
   check(!Vdp2VramPixelAddress(&c, 0, 8, &address, &nibble) && address == 64, "16x16 bottom left cell");
   check(!Vdp2VramPixelAddress(&c, 8, 8, &address, &nibble) && address == 96, "16x16 bottom right cell");
   c.grayscale = 1;
   memset(ram, 0x11, 32); memset(ram + 32, 0x22, 32);
   memset(ram + 64, 0x33, 32); memset(ram + 96, 0x44, 32);
   render(&c, 1);
   check(pixels[0] == 0xff111111 && pixels[8] == 0xff222222 &&
      pixels[8 * 16] == 0xff333333 && pixels[8 * 16 + 8] == 0xff444444,
      "four cells are rendered in hardware character order");
   c.tile_size = 0;
   check(!Vdp2VramPixelAddress(&c, 0, 1, &address, &nibble) && address == 8, "bitmap linear row stride");
   c.color = 4;
   c.start = 0x7ffff;
   ram[0x7ffff] = 0x80; ram[0] = 0x12; ram[1] = 0x34; ram[2] = 0x56;
   render(&c, 1);
   check(pixels[0] == 0xff563412, "unaligned multibyte pixel wraps safely at VRAM end");
   check(!Vdp2VramPixelAddress(&c, 1, 0, &address, &nibble) && address == 3, "bitmap address wraps");
   c = config(0);
   c.length = 33;
   render(&c, 1);
   check(pixels[10] == 0 && pixels[16 + 8] == 0, "partial last tile is padded transparently");
   check(Vdp2VramPixelAddress(&c, 10, 0, &address, &nibble) == -1, "padding has no VRAM address");
   c = config(0);
   c.width = 256;
   for (color = 0; color < 5; ++color)
   {
      static const int heights[] = { 4096, 2048, 1024, 1024, 512 };
      c.color = color;
      check(!Vdp2VramDimensions(&c, &w, &h) && w == 256 && h == heights[color], "full VRAM atlas dimensions");
      pixels[w * h] = 0xdeadbeef;
      render(&c, 1);
      check(pixels[w * h] == 0xdeadbeef, "output guard preserved");
      check(Vdp2VramRender(&c, ram, cram, 1, pixels, w * h - 1) == -1, "short output rejected");
   }
   c.color = 5;
   check(Vdp2VramDimensions(&c, &w, &h) == -1, "reserved color format rejected");
   c.color = 0; c.width = 15;
   check(Vdp2VramDimensions(&c, &w, &h) == -1, "invalid tile width rejected");
   c.width = 16; c.length = 0;
   check(Vdp2VramDimensions(&c, &w, &h) == -1, "empty range rejected");
   c.length = 32;
   check(Vdp2VramRender(&c, ram, cram, 3, pixels, 1024) == -1, "reserved CRAM mode rejected");
}

static void preset_tests(void)
{
   Vdp2 r = { 0 }, before;
   Vdp2VramConfig c;
   r.CHCTLA = 0x123f; r.CHCTLB = 0x3623;
   r.CRAOFA = 0x4321; r.CRAOFB = 5;
   r.MPOFN = 0x21; r.MPOFR = 0x32;
   r.BMPNA = 0x0203; r.BMPNB = 4;
   before = r;
   check(!Vdp2VramLayerConfig(&r, 0, &c) && c.color == 3 && !c.tile_size &&
      c.width == 1024 && c.height == 512 && c.start == 0x20000 && c.palette == 0x100, "NBG0 RGB bitmap preset");
   check(!Vdp2VramLayerConfig(&r, 1, &c) && c.color == 1 && !c.tile_size &&
      c.width == 512 && c.height == 256 && c.start == 0x40000 && c.palette == 0x400, "NBG1 palette bitmap preset");
   check(!Vdp2VramLayerConfig(&r, 2, &c) && c.color == 1 && c.tile_size == 16 && c.palette == 0x300, "NBG2 tile preset");
   check(!Vdp2VramLayerConfig(&r, 3, &c) && c.color == 1 && c.tile_size == 8 && c.palette == 0x400, "NBG3 tile preset");
   check(!Vdp2VramLayerConfig(&r, 4, &c) && c.color == 3 && !c.tile_size &&
      c.start == 0x40000 && c.width == 512 && c.height == 512, "RBG0 parameter A bitmap preset");
   check(!Vdp2VramLayerConfig(&r, 5, &c) && c.start == 0x60000, "RBG0 parameter B bitmap preset");
   check(!Vdp2VramLayerConfig(&r, 6, &c) && c.color == 3 && c.start == 0x60000 && c.palette == 0x100, "RBG1 uses NBG0 format and parameter B address");
   check(!memcmp(&r, &before, sizeof(r)), "registers are unchanged");
   r.CHCTLA = 0x70;
   check(Vdp2VramLayerConfig(&r, 0, &c) == -1, "reserved layer format rejected");
   check(Vdp2VramLayerConfig(&r, 7, &c) == -1, "invalid layer rejected");
}

int main(void)
{
   color_tests();
   layout_tests();
   preset_tests();
   printf("VDP2 VRAM: %d checks, %d failures\n", checks, failures);
   return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
