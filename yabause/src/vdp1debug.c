/* This file is part of Yabause, licensed under GPL-2.0-or-later. */
#include "vdp1debug.h"

static u16 read_word(const u8 *ram, u32 address)
{
   return (ram[address & 0x7FFFF] << 8) | ram[(address + 1) & 0x7FFFF];
}

int Vdp1DebugCollectImages(const u8 *ram, Vdp1DebugImage *images,
                          int capacity, int *status)
{
   u32 address = 0, return_address = 0xFFFFFFFF;
   int step, count = 0;
   *status = VDP1_DEBUG_INVALID;
   if (!ram || !images || capacity < 1) return 0;
   for (step = 0; step < VDP1_DEBUG_MAX_COMMANDS; ++step)
   {
      u16 control, size, pmod;
      if (address > VDP1_DEBUG_VRAM_SIZE - 32) return count;
      control = read_word(ram, address);
      if (control & 0x8000)
      {
         *status = VDP1_DEBUG_END;
         return count;
      }
      if ((control & 15) > 11) return count;
      size = read_word(ram, address + 10);
      pmod = read_word(ram, address + 4);
      if (!(control & 0x4000) && (control & 15) <= 3 &&
          (size & 0x3F00) && (size & 255) && ((pmod >> 3) & 7) <= 5)
      {
         Vdp1DebugImage *image;
         if (count == capacity) break;
         image = &images[count++];
         image->command_address = address;
         image->address = read_word(ram, address + 8) * 8;
         image->width = (size & 0x3F00) >> 5;
         image->height = size & 255;
         image->pmod = pmod;
         image->color = read_word(ram, address + 6);
      }
      switch ((control >> 12) & 3)
      {
         case 0: address += 32; break;
         case 1: address = read_word(ram, address + 2) * 8; break;
         case 2:
            if (return_address == 0xFFFFFFFF) return_address = address + 32;
            address = read_word(ram, address + 2) * 8;
            break;
         case 3:
            if (return_address == 0xFFFFFFFF) address += 32;
            else { address = return_address; return_address = 0xFFFFFFFF; }
            break;
      }
   }
   *status = VDP1_DEBUG_LIMIT;
   return count;
}

u32 Vdp1DebugRGB555(u16 color)
{
   return 0xFF000000u | ((color & 31) << 19) |
      ((color & 0x3E0) << 6) | ((color & 0x7C00) >> 7);
}

void Vdp1DebugBuildPalette(const u16 *cram, int color_mode, u32 *palette)
{
   int i;
   for (i = 0; i < 2048; ++i)
   {
      if (color_mode == 0 || color_mode == 1)
         palette[i] = Vdp1DebugRGB555(cram[i]);
      else if (color_mode == 2)
      {
         u16 first = cram[(i * 2) & 2047];
         u16 second = cram[(i * 2 + 1) & 2047];
         palette[i] = 0xFF000000u | ((second & 255) << 16) |
                      (second & 0xFF00) | (first & 255);
      }
      else palette[i] = 0;
   }
}

u32 Vdp1DebugImageBytes(const Vdp1DebugImage *image)
{
   int mode = (image->pmod >> 3) & 7;
   u32 stride = (mode <= 1 || mode == 6) ? (image->width + 1) / 2 :
                image->width * (mode == 5 ? 2 : 1);
   return stride * image->height;
}

static u32 palette_color(u16 pixel, const u32 *palette, u16 spctl, u16 craofb)
{
   static const u16 masks[16] = {
      0x7FF, 0x7FF, 0x7FF, 0x7FF, 0x3FF, 0x7FF, 0x3FF, 0x1FF,
      0x7F, 0x3F, 0x3F, 0x3F, 0xFF, 0xFF, 0xFF, 0xFF
   };
   if ((spctl & 0x20) && (pixel & 0x8000)) return Vdp1DebugRGB555(pixel);
   return palette[((pixel & masks[spctl & 15]) + ((craofb & 0x70) << 4)) & 2047];
}

int Vdp1DebugDecodeImage(const u8 *ram, const u32 *palette, u16 spctl,
                        u16 craofb, const Vdp1DebugImage *image,
                        u32 *pixels, u32 capacity)
{
   int x, y, mode, spd, ecd;
   u32 stride;
   if (!ram || !palette || !image || !pixels || !image->width || !image->height ||
       image->width > 1024 || image->height > 2048 ||
       (u32)image->width * image->height > capacity) return -1;
   mode = (image->pmod >> 3) & 7;
   spd = image->pmod & 0x40;
   /* High-speed shrink also disables end-code processing. */
   ecd = image->pmod & 0x1080;
   stride = Vdp1DebugImageBytes(image) / image->height;
   for (y = 0; y < image->height; ++y)
   {
      int ends = 0;
      for (x = 0; x < image->width; ++x)
      {
         u32 address = image->address + y * stride;
         u16 dot, pixel;
         u32 color = 0;
         if (mode <= 1 || mode == 6)
         {
            dot = ram[(address + x / 2) & 0x7FFFF];
            dot = (x & 1) ? dot & 15 : dot >> 4;
         }
         else if (mode == 5) dot = read_word(ram, address + x * 2);
         else dot = ram[(address + x) & 0x7FFFF];

         if (mode >= 6)
         {
            u32 gray = mode == 6 ? dot * 17 : dot;
            color = 0xFF000000u | gray * 0x010101;
         }
         else if (!ecd && (ends >= 2 || dot == (mode <= 1 ? 15 : mode == 5 ? 0x7FFF : 255)))
            ++ends;
         else
         {
            static const u16 masks[6] = {15, 15, 63, 127, 255, 65535};
            dot &= masks[mode];
            if (spd || (mode == 5 ? (dot & 0x8000) != 0 : dot != 0))
            {
               if (mode == 5) color = Vdp1DebugRGB555(dot);
               else if (mode == 1)
               {
                  pixel = read_word(ram, image->color * 8 + dot * 2);
                  color = palette_color(pixel, palette, spctl, craofb);
               }
               else
               {
                  pixel = (image->color & ~masks[mode]) | dot;
                  color = palette_color(pixel, palette, spctl, craofb);
               }
            }
         }
         pixels[y * image->width + x] = color;
      }
   }
   return 0;
}
