/* This file is part of Yabause, licensed under GPL-2.0-or-later. */
#include "vdp1debug.h"
#include <stdlib.h>

static u8 ram[VDP1_DEBUG_VRAM_SIZE], saved[VDP1_DEBUG_VRAM_SIZE];
static u16 cram[2048];
static u32 palette[2048], pixels[128];
static Vdp1DebugImage commands[VDP1_DEBUG_MAX_COMMANDS];
static int checks;

static void check(int condition, const char *message)
{
   ++checks;
   if (!condition) { fprintf(stderr, "FAIL: %s\n", message); exit(1); }
}

static void word(u32 address, u16 value)
{
   ram[address & 0x7FFFF] = value >> 8;
   ram[(address + 1) & 0x7FFFF] = value & 255;
}

static void command(u32 address, u16 control, u32 link)
{
   word(address, control);
   word(address + 2, link / 8);
   word(address + 4, 0xA8);
   word(address + 8, 0x200);
   word(address + 10, 0x0102);
}

static void traversal(void)
{
   int status, count;
   memset(ram, 0, sizeof(ram));
   command(0, 0x2000, 0x80); /* CALL */
   command(0x20, 0x5000, 0x100); /* SKIP ASSIGN */
   command(0x80, 0x3001, 0); /* sprite, RETURN */
   command(0x100, 0x8000, 0);
   count = Vdp1DebugCollectImages(ram, commands, VDP1_DEBUG_MAX_COMMANDS, &status);
   check(count == 2 && status == VDP1_DEBUG_END, "CALL, RETURN, SKIP, ASSIGN and END");
   check(commands[0].address == 0x1000 && commands[0].width == 8 && commands[0].height == 2,
         "source address and dimensions");
   check(commands[1].command_address == 0x80, "subroutine image retained");
   command(0, 0x1000, 0); /* cycle */
   count = Vdp1DebugCollectImages(ram, commands, VDP1_DEBUG_MAX_COMMANDS, &status);
   check(count == VDP1_DEBUG_MAX_COMMANDS && status == VDP1_DEBUG_LIMIT, "cyclic list bounded");
   count = Vdp1DebugCollectImages(ram, commands, 1, &status);
   check(count == 1 && status == VDP1_DEBUG_LIMIT, "caller capacity respected");
   command(0, 0x1000, 0x7FFF8);
   Vdp1DebugCollectImages(ram, commands, 10, &status);
   check(status == VDP1_DEBUG_INVALID, "truncated command rejected");
   command(0, 12, 0);
   check(Vdp1DebugCollectImages(ram, commands, 10, &status) == 0 && status == VDP1_DEBUG_INVALID,
         "invalid command rejected");
   command(0, 0, 0); word(10, 0); word(32, 0x8000);
   check(Vdp1DebugCollectImages(ram, commands, 10, &status) == 0 && status == VDP1_DEBUG_END,
         "zero size skipped");
   word(10, 0x0101); word(4, 7 << 3);
   check(Vdp1DebugCollectImages(ram, commands, 10, &status) == 0, "reserved color mode skipped");
}

static void decode(void)
{
   Vdp1DebugImage image = {0, 0x1000, 8, 2, 0x80, 0};
   int i, mode;
   memset(ram, 0, sizeof(ram));
   for (i = 0; i < 2048; ++i) palette[i] = 0xFF000000u | i;
   for (mode = 0; mode <= 5; ++mode)
   {
      image.pmod = (mode << 3) | 0x80;
      image.color = mode == 1 ? 0x400 : 0x120;
      memset(ram + image.address, 0, 32);
      if (mode < 2) ram[image.address] = 0x12;
      else if (mode == 5) { word(image.address, 0x801F); word(image.address + 2, 0x83E0); }
      else { ram[image.address] = 1; ram[image.address + 1] = 2; }
      word(0x2002, 0x801F); word(0x2004, 0x83E0);
      check(Vdp1DebugDecodeImage(ram, palette, 0x20, 0, &image, pixels, 128) == 0, "mode decodes");
      check(pixels[2] == 0, "transparent source pixel");
      if (mode == 1 || mode == 5)
         check(pixels[0] == 0xFFF80000 && pixels[1] == 0xFF00F800, "RGB555 channel order");
      else
         check(pixels[0] == palette[(image.color & ~(mode == 0 ? 15 : mode == 2 ? 63 : mode == 3 ? 127 : 255)) | 1],
               "bank lower bits masked");
      image.pmod |= 0x40;
      Vdp1DebugDecodeImage(ram, palette, 0x20, 0, &image, pixels, 128);
      check(pixels[2] != 0, "SPD exposes transparent source pixel");
   }
   image.pmod = 0x80; image.color = 0x780; ram[image.address] = 0x10;
   Vdp1DebugDecodeImage(ram, palette, 7, 0x10, &image, pixels, 128);
   check(pixels[0] == palette[0x281], "sprite type mask and CRAM offset");
   image.pmod = 0x88; image.color = 0x400;
   word(0x2002, 0x6123);
   Vdp1DebugDecodeImage(ram, palette, 0, 0x20, &image, pixels, 128);
   check(pixels[0] == palette[0x323], "LUT palette index and offset");

   for (mode = 0; mode <= 5; ++mode)
   {
      int stride = mode < 2 ? 4 : mode == 5 ? 16 : 8;
      image.pmod = (mode << 3) | 0x40; image.color = 0;
      memset(ram + image.address, 0x11, 32);
      if (mode < 2) ram[image.address] = 0xFF;
      else if (mode == 5) { word(image.address, 0x7FFF); word(image.address + 2, 0x7FFF); }
      else { ram[image.address] = 255; ram[image.address + 1] = 255; }
      word(2, 0x801F);
      Vdp1DebugDecodeImage(ram, palette, 0x20, 0, &image, pixels, 128);
      check(pixels[0] == 0 && pixels[1] == 0 && pixels[7] == 0 && pixels[8] != 0,
            "two end codes terminate only their row in each mode");
      /* One code on one row must not terminate the next row after its first code. */
      memset(ram + image.address, 0x11, 32);
      if (mode < 2) { ram[image.address] = 0xF1; ram[image.address + stride] = 0xF1; }
      else if (mode == 5) { word(image.address, 0x7FFF); word(image.address + stride, 0x7FFF); }
      else { ram[image.address] = 255; ram[image.address + stride] = 255; }
      Vdp1DebugDecodeImage(ram, palette, 0x20, 0, &image, pixels, 128);
      check(pixels[8] == 0 && pixels[9] != 0, "end-code count resets each row");
      image.pmod |= 0x80;
      Vdp1DebugDecodeImage(ram, palette, 0x20, 0, &image, pixels, 128);
      check(pixels[0] != 0, "ECD exposes end codes");
      image.pmod = (image.pmod & ~0x80) | 0x1000;
      Vdp1DebugDecodeImage(ram, palette, 0x20, 0, &image, pixels, 128);
      check(pixels[0] != 0, "high-speed shrink disables end codes");
   }
   image.address = 0x7FFFF; image.pmod = 0xE8; image.height = 1;
   word(0x7FFFF, 0xFC00); word(1, 0x801F);
   Vdp1DebugDecodeImage(ram, palette, 0, 0, &image, pixels, 128);
   check(pixels[0] == 0xFF0000F8 && pixels[1] == 0xFFF80000, "unaligned word and VRAM wrapping");
   image.address = 0x1000; image.pmod = 6 << 3; image.width = 9; image.height = 2;
   ram[image.address] = 0x0F; ram[image.address + 5] = 0xA0;
   Vdp1DebugDecodeImage(ram, palette, 0, 0, &image, pixels, 128);
   check(pixels[0] == 0xFF000000 && pixels[1] == 0xFFFFFFFF && pixels[9] == 0xFFAAAAAA,
         "4bpp grayscale and odd-width row stride");
   image.pmod = 7 << 3; ram[image.address] = 128;
   Vdp1DebugDecodeImage(ram, palette, 0, 0, &image, pixels, 128);
   check(pixels[0] == 0xFF808080, "8bpp grayscale");
   check(Vdp1DebugDecodeImage(ram, palette, 0, 0, &image, pixels, 1) == -1, "output capacity checked");
   image.width = 0;
   check(Vdp1DebugDecodeImage(ram, palette, 0, 0, &image, pixels, 128) == -1, "zero width rejected");
   image.width = 8;
   memcpy(saved, ram, sizeof(ram));
   Vdp1DebugDecodeImage(ram, palette, 0, 0, &image, pixels, 128);
   check(!memcmp(saved, ram, sizeof(ram)), "VRAM remains unchanged");
}

int main(void)
{
   int mode;
   for (mode = 0; mode < 2; ++mode)
   {
      cram[1] = 0x001F; cram[2] = 0x03E0; cram[3] = 0x7C00;
      Vdp1DebugBuildPalette(cram, mode, palette);
      check(palette[1] == 0xFFF80000 && palette[2] == 0xFF00F800 && palette[3] == 0xFF0000F8,
            "16-bit CRAM channel order");
   }
   cram[2] = 0x0056; cram[3] = 0x3412;
   Vdp1DebugBuildPalette(cram, 2, palette);
   check(palette[1] == 0xFF123456 && palette[1025] == palette[1], "32-bit CRAM channel order and wrap");
   traversal();
   decode();
   printf("PASS: %d VDP1 VRAM checks\n", checks);
   return 0;
}
