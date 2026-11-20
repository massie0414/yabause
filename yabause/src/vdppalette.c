/* This file is part of Yabause, licensed under GPL-2.0-or-later. */
#include "vdppalette.h"
#include "vdp1debug.h"
#include "vdp2vram.h"

static u16 word(const u8 *ram, u32 address)
{
   return (ram[address & 0x7ffff] << 8) | ram[(address + 1) & 0x7ffff];
}

int VdpPaletteReadColor(const u8 *cram, int mode, u32 index, VdpPaletteColor *out)
{
   if (!cram || !out || mode < 0 || mode > 2) return -1;
   out->cram_index = index & (mode == 2 ? 1023 : 2047);
   out->address = out->cram_index * (mode == 2 ? 4 : 2);
   if (mode == 2)
   {
      out->raw = T2ReadLong((u8 *)cram, out->address);
      out->argb = 0xff000000u | ((out->raw & 255) << 16) |
         (out->raw & 0xff00) | ((out->raw >> 16) & 255);
   }
   else
   {
      out->raw = T2ReadWord((u8 *)cram, out->address);
      out->argb = Vdp1DebugRGB555((u16)out->raw);
   }
   return 0;
}

int Vdp1PaletteReadColor(const u8 *ram, const u8 *cram, const Vdp2 *r,
                        const VdpPaletteSource *s, int index, VdpPaletteColor *out)
{
   static const u16 masks[16] = {
      0x7ff, 0x7ff, 0x7ff, 0x7ff, 0x3ff, 0x7ff, 0x3ff, 0x1ff,
      0x7f, 0x3f, 0x3f, 0x3f, 0xff, 0xff, 0xff, 0xff
   };
   u16 pixel;
   if (!r || !s || !out || index < 0 || index >= s->count) return -1;
   if (s->type == VDP_PALETTE_CRAM)
      return VdpPaletteReadColor(cram, (r->RAMCTL >> 12) & 3, s->base + index, out);
   if (s->type == VDP_PALETTE_LUT)
   {
      if (!ram) return -1;
      pixel = word(ram, s->base + index * 2);
   }
   else if (s->type == VDP_PALETTE_BANK || s->type == VDP_PALETTE_FLAT)
      pixel = (u16)(s->base + index);
   else return -1;
   if ((r->SPCTL & 0x20) && (pixel & 0x8000))
   {
      out->argb = Vdp1DebugRGB555(pixel);
      out->raw = pixel;
      out->address = s->type == VDP_PALETTE_LUT ? (s->base + index * 2) & 0x7ffff : s->command + 6;
      out->cram_index = -1;
      return 0;
   }
   return VdpPaletteReadColor(cram, (r->RAMCTL >> 12) & 3,
      (pixel & masks[r->SPCTL & 15]) + ((r->CRAOFB & 0x70) << 4), out);
}

int Vdp1PaletteCollect(const u8 *ram, VdpPaletteSource *sources, int capacity, int *status)
{
   u32 address = 0, ret = 0xffffffff;
   int step, count = 0;
   if (!status) return 0;
   *status = VDP1_DEBUG_INVALID;
   if (!ram || !sources || capacity < 1) return 0;
   for (step = 0; step < VDP1_DEBUG_MAX_COMMANDS; ++step)
   {
      u16 control, pmod, color, size;
      int command, mode, colors = 0, type = VDP_PALETTE_BANK;
      if (address > 0x80000 - 32) return count;
      control = word(ram, address);
      if (control & 0x8000) { *status = VDP1_DEBUG_END; return count; }
      command = control & 15;
      if (command > 11) return count;
      pmod = word(ram, address + 4);
      color = word(ram, address + 6);
      size = word(ram, address + 10);
      mode = (pmod >> 3) & 7;
      if (!(control & 0x4000))
      {
         if (command <= 3 && (size & 0x3f00) && (size & 255))
         {
            static const int counts[8] = {16, 16, 64, 128, 256, 0, 0, 0};
            colors = counts[mode];
            if (mode == 1) type = VDP_PALETTE_LUT;
         }
         else if (command >= 4 && command <= 7)
         {
            colors = 1;
            type = VDP_PALETTE_FLAT;
         }
      }
      if (colors)
      {
         VdpPaletteSource *s;
         if (count == capacity) break;
         s = &sources[count++];
         s->type = type;
         s->count = colors;
         s->command = address;
         s->base = type == VDP_PALETTE_LUT ? color * 8 : color & ~(colors - 1);
      }
      switch ((control >> 12) & 3)
      {
         case 0: address += 32; break;
         case 1: address = word(ram, address + 2) * 8; break;
         case 2:
            if (ret == 0xffffffff) ret = address + 32;
            address = word(ram, address + 2) * 8;
            break;
         case 3:
            if (ret == 0xffffffff) address += 32;
            else { address = ret; ret = 0xffffffff; }
            break;
      }
   }
   *status = VDP1_DEBUG_LIMIT;
   return count;
}

static u32 pattern_palette(u16 value, u16 pnc, int color)
{
   if (pnc & 0x8000)
      return color == 0 ? ((value & 0xf000) >> 8) | ((pnc & 0xe0) << 3) :
                          (value & 0x7000) >> 4;
   return (value & (color == 0 ? 0x7f : 0x70)) << 4;
}

int Vdp2PaletteCollect(const Vdp2 *r, const u8 *ram, int layer, u32 *bases, int capacity)
{
   Vdp2VramConfig c;
   u16 maps[8], pnc, plane_size;
   u32 offset, page_bytes, plane_bytes, address_mask;
   u8 seen[128] = {0};
   int planes, shift, pages, i, j, count = 0, step;
   if (!r || !ram || !bases || capacity < 128 || Vdp2VramLayerConfig(r, layer, &c)) return -1;
   if (c.color >= 3) return 0;
   if (!c.tile_size || c.color == 2)
   {
      bases[0] = c.palette;
      return 1;
   }
   if (layer < 4)
   {
      const u16 normal_maps[8] = {r->MPABN0, r->MPCDN0, r->MPABN1, r->MPCDN1,
         r->MPABN2, r->MPCDN2, r->MPABN3, r->MPCDN3};
      const u16 controls[4] = {r->PNCN0, r->PNCN1, r->PNCN2, r->PNCN3};
      maps[0] = normal_maps[layer * 2]; maps[1] = normal_maps[layer * 2 + 1];
      planes = 4;
      pnc = controls[layer];
      plane_size = (r->PLSZ >> (layer * 2)) & 3;
      offset = ((r->MPOFN >> (layer * 4)) & 7) << 6;
   }
   else
   {
      const u16 rotation_maps[16] = {r->MPABRA, r->MPCDRA, r->MPEFRA, r->MPGHRA,
         r->MPIJRA, r->MPKLRA, r->MPMNRA, r->MPOPRA,
         r->MPABRB, r->MPCDRB, r->MPEFRB, r->MPGHRB,
         r->MPIJRB, r->MPKLRB, r->MPMNRB, r->MPOPRB};
      int b = layer != 4;
      memcpy(maps, rotation_maps + b * 8, sizeof(maps));
      planes = 16;
      pnc = layer == 6 ? r->PNCN0 : r->PNCR;
      plane_size = (r->PLSZ >> (b ? 12 : 8)) & 3;
      offset = ((r->MPOFR >> (b ? 4 : 0)) & 7) << 6;
   }
   /* Same plane alignment/page sizes as CalcPlaneAddr in vidshared.h. */
   shift = plane_size == 3 ? 2 : plane_size == 1 ? 1 : 0;
   pages = 1 << shift;
   step = pnc & 0x8000 ? 2 : 4;
   page_bytes = (c.tile_size == 8 ? 4096 : 1024) * step;
   plane_bytes = pages * page_bytes;
   address_mask = step == 2 ? (c.tile_size == 8 ? 0x3f : 0x1ff) :
                             (c.tile_size == 8 ? 0x1f : 0x7f);
   for (i = 0; i < planes; ++i)
   {
      u32 map = offset | ((maps[i / 2] >> ((i & 1) * 8)) & 255);
      u32 address = ((map & address_mask) >> shift) * plane_bytes;
      for (j = 0; j < (int)plane_bytes; j += step)
      {
         u32 base = (c.palette + pattern_palette(word(ram, address + j), pnc, c.color)) & 2047;
         seen[base >> 4] = 1;
      }
   }
   /* Rotation screen-over mode 1 can reference a separate pattern. */
   if (layer >= 4 && ((r->PLSZ >> (layer == 4 ? 10 : 14)) & 3) == 1)
   {
      u16 over = layer == 4 ? r->OVPNRA : r->OVPNRB;
      seen[((c.palette + pattern_palette(over, pnc, c.color)) & 2047) >> 4] = 1;
   }
   for (i = 0; i < 128; ++i) if (seen[i]) bases[count++] = i * 16;
   return count;
}
