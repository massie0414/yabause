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

int Vdp2CellMapConfig(const Vdp2 *r, int layer, Vdp2CellMap *m)
{
   u16 planes[8] = { 0 };
   u32 offset, page_bytes;
   int size, i, pages, mask, bit;
   if (!m) return -1;
   memset(m, 0, sizeof(*m));
   if (Vdp2VramLayerConfig(r, layer, &m->tile)) return -1;
   if (!m->tile.tile_size) return 1;
   if (layer < 4)
   {
      const u16 pnc[] = { r->PNCN0, r->PNCN1, r->PNCN2, r->PNCN3 };
      const u16 ab[] = { r->MPABN0, r->MPABN1, r->MPABN2, r->MPABN3 };
      const u16 cd[] = { r->MPCDN0, r->MPCDN1, r->MPCDN2, r->MPCDN3 };
      m->pattern_control = pnc[layer];
      planes[0] = ab[layer]; planes[1] = cd[layer];
      offset = ((r->MPOFN >> (layer * 4)) & 7) << 6;
      size = (r->PLSZ >> (layer * 2)) & 3;
      m->map_columns = 2;
   }
   else
   {
      const u16 a[] = { r->MPABRA, r->MPCDRA, r->MPEFRA, r->MPGHRA,
         r->MPIJRA, r->MPKLRA, r->MPMNRA, r->MPOPRA };
      const u16 b[] = { r->MPABRB, r->MPCDRB, r->MPEFRB, r->MPGHRB,
         r->MPIJRB, r->MPKLRB, r->MPMNRB, r->MPOPRB };
      memcpy(planes, layer == 4 ? a : b, sizeof(planes));
      m->pattern_control = layer == 6 ? r->PNCN0 : r->PNCR;
      offset = ((r->MPOFR >> (layer == 4 ? 0 : 4)) & 7) << 6;
      size = (r->PLSZ >> (layer == 4 ? 8 : 12)) & 3;
      m->map_columns = 4;
   }
   /* Match ReadPlaneSize's 1x1 fallback for reserved size 2. */
   m->plane_width = (size == 1 || size == 3) ? 1024 : 512;
   m->plane_height = size == 3 ? 1024 : 512;
   m->pattern_bytes = m->pattern_control & 0x8000 ? 2 : 4;
   page_bytes = (512 / m->tile.tile_size) * (512 / m->tile.tile_size) * m->pattern_bytes;
   pages = (m->plane_width / 512) * (m->plane_height / 512);
   mask = 0x7ffff / page_bytes;
   for (i = 0; i < m->map_columns * m->map_columns; ++i)
   {
      u32 number = offset | ((planes[i / 2] >> ((i & 1) * 8)) & 0xff);
      m->plane_address[i] = ((number & mask) / pages) * pages * page_bytes;
   }
   bit = layer < 4 ? layer : layer == 6 ? 5 : 4;
   m->enabled = (r->BGON >> bit) & 1;
   if (layer == 0 && (r->BGON & 0x20)) m->enabled = 0;
   m->tile.transparency = !(r->BGON & (0x100 << (layer == 6 ? 0 : bit)));
   m->tile.width = m->tile.tile_size;
   m->tile.length = m->tile.tile_size * m->tile.tile_size * bits_per_pixel[m->tile.color] / 8;
   return 0;
}

static int valid_cell_map(const Vdp2CellMap *m)
{
   return m && (m->map_columns == 2 || m->map_columns == 4) &&
      (m->plane_width == 512 || m->plane_width == 1024) &&
      (m->plane_height == 512 || m->plane_height == 1024) &&
      (m->pattern_bytes == 2 || m->pattern_bytes == 4) &&
      (m->tile.tile_size == 8 || m->tile.tile_size == 16) &&
      m->tile.color >= 0 && m->tile.color <= 4;
}

static u32 cell_word(const u8 *ram, u32 address)
{
   return (ram[address & 0x7ffff] << 8) | ram[(address + 1) & 0x7ffff];
}

int Vdp2CellMapRead(const Vdp2CellMap *m, const u8 *ram, int x, int y, Vdp2Cell *c)
{
   int size, page_cells, page;
   u32 name, character, palette, supplement;
   if (!valid_cell_map(m) || !ram || !c || x < 0 || y < 0 ||
       x >= m->plane_width * m->map_columns || y >= m->plane_height * m->map_columns) return -1;
   memset(c, 0, sizeof(*c));
   size = m->tile.tile_size;
   page_cells = 512 / size;
   c->plane = (y / m->plane_height) * m->map_columns + x / m->plane_width;
   x %= m->plane_width; y %= m->plane_height;
   page = (y / 512) * (m->plane_width / 512) + x / 512;
   c->pattern_address = (m->plane_address[c->plane] +
      (page * page_cells * page_cells + (y % 512 / size) * page_cells + x % 512 / size) *
      m->pattern_bytes) & 0x7ffff;
   name = cell_word(ram, c->pattern_address);
   if (m->pattern_bytes == 2)
   {
      c->pattern_name = name;
      supplement = m->pattern_control & 0x3ff;
      palette = m->tile.color == 0 ? ((name & 0xf000) >> 8) | ((supplement & 0xe0) << 3)
                                  : (name & 0x7000) >> 4;
      c->special_priority = (supplement >> 9) & 1;
      c->special_color = (supplement >> 8) & 1;
      if (!(m->pattern_control & 0x4000))
      {
         c->flip = (name >> 10) & 3;
         character = size == 8 ? (name & 0x3ff) | ((supplement & 0x1f) << 10)
            : ((name & 0x3ff) << 2) | (supplement & 3) | ((supplement & 0x1c) << 10);
      }
      else
         character = size == 8 ? (name & 0xfff) | ((supplement & 0x1c) << 10)
            : ((name & 0xfff) << 2) | (supplement & 3) | ((supplement & 0x10) << 10);
   }
   else
   {
      u32 second = cell_word(ram, c->pattern_address + 2);
      c->pattern_name = (name << 16) | second;
      character = second & 0x7fff;
      palette = (name & (m->tile.color == 0 ? 0x7f : 0x70)) << 4;
      c->flip = (name >> 14) & 3;
      c->special_priority = (name >> 13) & 1;
      c->special_color = (name >> 12) & 1;
   }
   c->character_address = (character * 32) & 0x7ffff;
   c->palette = (m->tile.palette + (m->tile.color < 2 ? palette : 0)) & 0x7ff;
   return 0;
}

int Vdp2CellRender(const Vdp2CellMap *m, const Vdp2Cell *cell,
                  const u8 *ram, const u8 *cram, int cram_mode,
                  u32 *pixels, u32 capacity)
{
   Vdp2VramConfig tile;
   if (!valid_cell_map(m) || !cell) return -1;
   tile = m->tile;
   tile.start = cell->character_address;
   tile.palette = cell->palette;
   tile.width = tile.tile_size;
   tile.length = tile.tile_size * tile.tile_size * bits_per_pixel[tile.color] / 8;
   return Vdp2VramRender(&tile, ram, cram, cram_mode, pixels, capacity);
}
