#include <stdio.h>
#include <stdlib.h>
#include "yui.h"
#include "peripheral.h"
#include "cs0.h"
#include "m68kcore.h"
#include "scsp.h"
#include "vidsoft.h"
#include "titan/titan.h"
#include "../layerpreview/scene.h"

SH2Interface_struct *SH2CoreList[] = { &SH2Interpreter, NULL };
PerInterface_struct *PERCoreList[] = { &PERDummy, NULL };
CDInterface *CDCoreList[] = { &DummyCD, NULL };
SoundInterface_struct *SNDCoreList[] = { &SNDDummy, NULL };
VideoInterface_struct *VIDCoreList[] = { &VIDDummy, &VIDSoft, NULL };
M68K_struct *M68KCoreList[] = { &M68KDummy, &M68KMusashi, NULL };
void YuiErrorMsg(const char *text) { fprintf(stderr, "%s\n", text); }
void YuiSwapBuffers(void) {}
extern int VideoUseGL;
static int failures;
static pixel_t output[704 * 512];

static void check_pixel(int x, int y, int width, u32 rgb, const char *message)
{
   u32 actual = output[y * width + x] & 0xFFFFFF;
   if (actual != rgb)
   {
      if (failures < 20)
         fprintf(stderr, "FAIL: %s (%d,%d): %06X != %06X\n", message, x, y, actual, rgb);
      failures++;
   }
}

static u16 color(int x, int y)
{
   return 0x8000 | ((x / 8) & 31) | ((y & 31) << 5) | (((x / 32 + y / 4) & 31) << 10);
}

static u32 rgb(int x, int y)
{
   u16 c = color(x, y);
   return ((c & 31) << 3) | ((c & 0x3E0) << 6) | ((c & 0x7C00) << 9);
}

static void setup(int layer, int interlace)
{
   setup_layer_scene();
   memset(Vdp2Ram, 0, 0x80000);
   memset(cell_scroll_data, 0, sizeof(struct CellScrollData) * 270);
   Vdp1External.disptoggle = 0;
   Vdp2External.disptoggle = ~0;
   Vdp2Regs->TVMD = interlace ? 0x80C1 : 0x8001; /* 352 x 448 or 224. */
   Vdp2Regs->BGON = 1 << layer;
   Vdp2Regs->CHCTLA = layer ? 0x3200 : 0x32; /* 512 x 256 RGB bitmap. */
   Vdp2Regs->LSTA0.all = Vdp2Regs->LSTA1.all = 0x30000;
   Vdp2Regs->VCSTA.all = 0x3F800;
   Vdp2Regs->BKTAU = 3;
   Vdp2Regs->BKTAL = 0x8000;
}

static void render(void)
{
   int i;
   for (i = 0; i < 270; i++) Vdp2Lines[i] = *Vdp2Regs;
   VIDSoft.Vdp2DrawStart();
   VIDSoft.Vdp2DrawScreens();
   VIDSoft.Vdp2DrawEnd();
   TitanRender(output);
}

/* Like Grandia, pack 352-wide rows into a 512-wide bitmap, using the
   fractional carry from the line + cell scroll to cross bitmap rows. */
static void packed_movie(int layer, int both, int interlace)
{
   int x, y, field, start, step, height = interlace ? 448 : 224;
   int stride = both ? 2 : 1, offset = both ? layer : 0;
   setup(layer, interlace);
   Vdp2Regs->SCRCTL = (7 << (8 * layer)) | (both ? 0x101 : 0);
   for (y = 0; y < 192; y++)
      for (x = 0; x < 352; x++)
         T1WriteWord(Vdp2Ram, (y * 352 + x) * 2, color(x, y));
   for (y = 0; y < height; y++)
   {
      int row = y * 192 / height;
      T1WriteLong(Vdp2Ram, 0x60000 + y * 8, (u32)(row * 352) << 16);
      T1WriteLong(Vdp2Ram, 0x60004 + y * 8, row * 352 * 128);
   }
   for (y = 0; y < 270; y++)
      for (x = 0; x < 44; x++)
      {
         if (both) cell_scroll_data[y].data[x * stride + (1 - offset)] = 99 << 16;
         cell_scroll_data[y].data[x * stride + offset] = x * 0x400;
      }
   for (field = 0; field < (interlace ? 2 : 1); field++)
   {
      vdp2_is_odd_frame = field;
      render();
      Vdp2GetInterlaceInfo(&start, &step);
      for (y = start; y < height; y += step)
         for (x = 0; x < 352; x++)
            check_pixel(x, y, 352, rgb(x, y * 192 / height), "packed movie row");
   }
}

static void line_functions(void)
{
   int flags, interval, x, y, layer, field;
   for (layer = 0; layer < 2; layer++)
      for (flags = 1; flags <= 7; flags++)
         for (interval = 0; interval < 4; interval++)
         {
            int count = 1 << interval;
            u32 address = 0x7FFFC; /* Also exercise VRAM table wrap. */
            setup(layer, 1);
            Vdp2Regs->LSTA0.all = Vdp2Regs->LSTA1.all = address / 2;
            Vdp2Regs->SCRCTL = ((flags << 1) | (interval << 4)) << (layer * 8);
            Vdp2Regs->SCYDN0 = Vdp2Regs->SCYDN1 = 0x4000;
            Vdp2Regs->ZMYN0.all = Vdp2Regs->ZMYN1.all = 0x8000;
            for (y = 0; y < 256; y++)
               for (x = 0; x < 512; x++)
                  T1WriteWord(Vdp2Ram, (y * 512 + x) * 2, color(x, y));
            /* Wrapped records overwrite the first bitmap rows; only check
               coordinates below that small table region. */
            for (y = 0; y < 448 / count; y++)
            {
               if (flags & 1) { T1WriteLong(Vdp2Ram, address & 0x7FFFF, (512 + (y % 4) * 16) << 16); address += 4; }
               if (flags & 2) { T1WriteLong(Vdp2Ram, address & 0x7FFFF, ((32 + y % 80) << 16) | 0xC000); address += 4; }
               if (flags & 4) { T1WriteLong(Vdp2Ram, address & 0x7FFFF, 0x8000); address += 4; }
            }
            for (field = 0; field < 2; field++)
            {
               int start, step;
               vdp2_is_odd_frame = field;
               render();
               Vdp2GetInterlaceInfo(&start, &step);
               for (y = 64 + start; y < 448; y += step)
                  for (x = 0; x < 352; x += 7)
                  {
                     int sx = ((flags & 1 ? 512 + ((y / count) % 4) * 16 : 0) + (flags & 4 ? x / 2 : x)) & 511;
                     int sy = flags & 2 ? 33 + ((y / count) % 80) + (y % count) / 2 : y / 2;
                     check_pixel(x, y, 352, rgb(sx, sy), "line fields, interval, fraction and wrapping");
                  }
            }
         }
}

static void cell_rows_and_mosaic(void)
{
   int x, y, layer, mosaic;
   for (layer = 0; layer < 2; layer++)
      for (mosaic = 0; mosaic < 2; mosaic++)
      {
         setup(layer, 0);
         Vdp2Regs->CHCTLA |= layer ? 0x400 : 4; /* 512-row bitmap. */
         Vdp2Regs->SCYIN0 = Vdp2Regs->SCYIN1 = 300;
         Vdp2Regs->SCYDN0 = Vdp2Regs->SCYDN1 = 0xC000;
         Vdp2Regs->SCRCTL = 1 << (layer * 8);
         Vdp2Regs->MZCTL = mosaic ? 1 << layer : 0; /* Even a 1 x 1 mosaic wins. */
         for (y = 0; y < 512; y++)
            for (x = 0; x < 512; x++)
               T1WriteWord(Vdp2Ram, (y * 512 + x) * 2, color(x, y));
         for (y = 0; y < 270; y++)
            for (x = 0; x < 44; x++)
               cell_scroll_data[y].data[x] = ((y % 3) << 16) + (x % 3) * 0x8000;
         render();
         for (y = 0; y < 224; y++)
            for (x = 0; x < 352; x += 7)
            {
               int sy = 300 + y + (mosaic ? 0 : y % 3 + (3 + 2 * ((x / 8) % 3)) / 4);
               check_pixel(x, y, 352, rgb(x, sy & 511), "cell snapshots, register fraction, bitmap height and mosaic");
            }
      }
}

int main(void)
{
   int layer, both, interlace;
   yabauseinit_struct init = {0};
   init.biospath = ""; init.buppath = ""; init.skip_load = 1;
   if (YabauseInit(&init)) return 2;
   VideoUseGL = 0;
   if (VideoChangeCore(VIDCORE_SOFT)) return 2;
   for (layer = 0; layer < 2; layer++)
      for (both = 0; both < 2; both++)
         for (interlace = 0; interlace < 2; interlace++)
            packed_movie(layer, both, interlace);
   line_functions();
   cell_rows_and_mosaic();
   VIDSoftSetNumLayerThreads(3);
   VIDSoftSetNumPriorityThreads(4);
   packed_movie(0, 1, 1);
   packed_movie(1, 1, 1);
   VIDSoftSetNumLayerThreads(0);
   VIDSoftSetNumPriorityThreads(0);
   YabauseDeInit();
   if (failures) { fprintf(stderr, "%d pixel checks failed\n", failures); return 1; }
   puts("Line and vertical cell scroll regressions passed");
   return 0;
}
