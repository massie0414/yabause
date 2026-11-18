/* This file is part of Yabause, licensed under GPL-2.0-or-later. */
#include "vdp2vram.h"

static const int bits_per_pixel[5] = { 4, 8, 16, 16, 32 };

int Vdp2VramDimensions(const Vdp2VramConfig *c, int *width, int *height)
{
   u32 tiles, columns, tile_bytes;
   if (!c || !width || !height || c->color < 0 || c->color > 4 ||
       c->start >= 0x80000 || !c->length || c->length > 0x80000 ||
       c->width < 8 || c->width > 1024 ||
       (c->tile_size != 0 && c->tile_size != 8 && c->tile_size != 16)) return -1;
   *width = c->width;
   if (!c->tile_size)
   {
      if (c->height < 1 || c->height > 512) return -1;
      *height = c->height;
   }
   else
   {
      if (c->width % c->tile_size) return -1;
      tile_bytes = c->tile_size * c->tile_size * bits_per_pixel[c->color] / 8;
      tiles = (c->length + tile_bytes - 1) / tile_bytes;
      columns = c->width / c->tile_size;
      *height = ((tiles + columns - 1) / columns) * c->tile_size;
   }
   return 0;
}

static u32 pixel_number(const Vdp2VramConfig *c, int x, int y)
{
   int size = c->tile_size;
   if (!size) return y * c->width + x;
   /* A 16x16 character contains four 8x8 cells: TL, TR, BL, BR. */
   return ((y / size) * (c->width / size) + x / size) * size * size +
      ((y % size) / 8 * (size / 8) + (x % size) / 8) * 64 +
      (y % 8) * 8 + x % 8;
}

int Vdp2VramPixelAddress(const Vdp2VramConfig *c, int x, int y,
                        u32 *address, int *low_nibble)
{
   int w, h;
   u32 bit;
   if (!address || !low_nibble || Vdp2VramDimensions(c, &w, &h) ||
       x < 0 || y < 0 || x >= w || y >= h) return -1;
   bit = pixel_number(c, x, y) * bits_per_pixel[c->color];
   if (c->tile_size && (bit / 8 + (bits_per_pixel[c->color] + 7) / 8 > c->length))
      return -1;
   *address = (c->start + bit / 8) & 0x7ffff;
   *low_nibble = (bit & 7) != 0;
   return 0;
}

static u32 rgb555(u32 value)
{
   return 0xff000000 | ((value & 31) << 19) |
      ((value & 0x3e0) << 6) | ((value & 0x7c00) >> 7);
}

static u32 rgb888(u32 value)
{
   return 0xff000000 | ((value & 0xff) << 16) |
      (value & 0xff00) | ((value >> 16) & 0xff);
}

int Vdp2VramRender(const Vdp2VramConfig *c, const u8 *ram,
                  const u8 *cram, int cram_mode, u32 *pixels, u32 capacity)
{
   int w, h, x, y, bpp;
   if (!ram || !cram || !pixels || cram_mode < 0 || cram_mode > 2 ||
       Vdp2VramDimensions(c, &w, &h) || capacity < (u32)w * h) return -1;
   bpp = bits_per_pixel[c->color];
   for (y = 0; y < h; ++y)
      for (x = 0; x < w; ++x)
      {
         u32 bit = pixel_number(c, x, y) * bpp;
         u32 address = (c->start + bit / 8) & 0x7ffff;
         u32 dot = 0, color = 0;
         int i;
         if (c->tile_size && bit / 8 + (bpp + 7) / 8 > c->length)
         {
            pixels[y * w + x] = 0;
            continue;
         }
         /* Byte reads also handle unaligned addresses and VRAM end wrapping. */
         for (i = 0; i < (bpp + 7) / 8; ++i)
            dot = (dot << 8) | ram[(address + i) & 0x7ffff];
         if (bpp == 4) dot = (dot >> ((bit & 7) ? 0 : 4)) & 15;
         if (c->transparency && (c->color < 3 ? dot == 0 :
             (dot & (c->color == 3 ? 0x8000 : 0x80000000)) == 0))
         {
            pixels[y * w + x] = 0;
            continue;
         }
         if (c->color < 3)
         {
            if (c->grayscale)
            {
               u32 max_index = c->color == 0 ? 15 : c->color == 1 ? 255 : 2047;
               u32 value = (dot & max_index) * 255 / max_index;
               color = 0xff000000 | value * 0x010101;
            }
            else
            {
               u32 entry = dot + c->palette;
               if (cram_mode == 2)
                  color = rgb888(T2ReadLong((u8 *)cram, (entry * 4) & 0xfff));
               else
                  color = rgb555(T2ReadWord((u8 *)cram, (entry * 2) & 0xfff));
            }
         }
         else color = c->color == 3 ? rgb555(dot) : rgb888(dot);
         pixels[y * w + x] = color;
      }
   return 0;
}

int Vdp2VramLayerConfig(const Vdp2 *r, int layer, Vdp2VramConfig *c)
{
   int ch, size = 0, bitmap = 0;
   u32 bank = 0, palette = 0;
   if (!r || !c || layer < 0 || layer > 6) return -1;
   memset(c, 0, sizeof(*c));
   if (layer == 0 || layer == 1 || layer == 6)
   {
      ch = r->CHCTLA >> (layer == 1 ? 8 : 0);
      c->color = (ch >> 4) & (layer == 1 ? 3 : 7);
      bitmap = ch & 2;
      size = (ch >> 2) & 3;
      bank = layer == 6 ? (r->MPOFR >> 4) & 7 : (r->MPOFN >> (layer * 4)) & 7;
      palette = (r->BMPNA >> (layer == 1 ? 8 : 0)) & 7;
      c->palette = ((r->CRAOFA >> (layer == 1 ? 4 : 0)) & 7) << 8;
   }
   else if (layer == 2 || layer == 3)
   {
      ch = r->CHCTLB >> ((layer - 2) * 4);
      c->color = (ch >> 1) & 1;
      c->palette = ((r->CRAOFA >> (layer * 4)) & 7) << 8;
   }
   else
   {
      ch = r->CHCTLB >> 8;
      c->color = (ch >> 4) & 7;
      bitmap = ch & 2;
      size = (ch >> 2) & 1;
      bank = (r->MPOFR >> (layer == 5 ? 4 : 0)) & 7;
      palette = r->BMPNB & 7;
      c->palette = (r->CRAOFB & 7) << 8;
   }
   c->tile_size = bitmap ? 0 : (ch & 1) ? 16 : 8;
   c->width = bitmap ? (size & 2 ? 1024 : 512) : 256;
   c->height = bitmap ? (size & 1 ? 512 : 256) : 0;
   c->start = bitmap ? (bank * 0x20000) & 0x7ffff : 0;
   c->length = 0x80000;
   if (bitmap && c->color < 2) c->palette += palette << 8;
   c->palette &= 0x7ff;
   return c->color <= 4 ? 0 : -1;
}
