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
      fprintf(stderr, "FAIL: %s (%d,%d): %06X != %06X\n", message, x, y, actual, rgb);
      failures++;
   }
}

static void render(void)
{
   int i;
   for (i = 0; i < 270; i++) Vdp2Lines[i] = *Vdp2Regs;
   VIDSoft.Vdp2DrawStart();
   VIDSoft.Vdp2DrawScreens();
   VIDSoft.Vdp2DrawEnd(); /* Join layer workers before inspecting Titan. */
   TitanRender(output);
}

static void setup(void)
{
   int i;
   setup_layer_scene();
   Vdp1External.disptoggle = 0;
   Vdp2External.disptoggle = ~0;
   Vdp2Regs->BGON = 0x10;
   Vdp2Regs->KTCTL = 0x11; /* A: 32-bit coefficients and line colors. */
   Vdp2Regs->KTAOF = 1;
   Vdp2Regs->LNCLEN = 0x10;
   Vdp2Regs->CCCTL = 0x110; /* Add line color to a black, opaque RBG0. */
   Vdp2Regs->LCTA.all = 0x30000;
   T1WriteWord(Vdp2Ram, 0x60000, 0x187); /* Low seven bits must be replaced. */
   T2WriteWord(Vdp2ColorRam, 0x41 * 2, 0);
   T2WriteWord(Vdp2ColorRam, 0x181 * 2, 0x001F);
   T2WriteWord(Vdp2ColorRam, 0x182 * 2, 0x03E0);
   T2WriteWord(Vdp2ColorRam, 0x183 * 2, 0x7C00);
   T2WriteWord(Vdp2ColorRam, 0x187 * 2, 0x7FE0);
   for (i = 0; i < 32; i++) T1WriteByte(Vdp2Ram, 0x1000 + i, 0x11);
   T1WriteLong(Vdp2Ram, 0x70054, 0); /* KAst */
   T1WriteLong(Vdp2Ram, 0x70058, 0); /* deltaKAst */
   T1WriteLong(Vdp2Ram, 0x7005C, 0x10000); /* deltaKAx */
   for (i = 0; i < 512; i++)
      T1WriteLong(Vdp2Ram, 0x40000 + i * 4, ((i < 160 ? 1 : 2) << 24) | 0x10000);
}

static void setup_parameter_b(void)
{
   int i;
   memcpy(Vdp2Ram + 0x70080, Vdp2Ram + 0x70000, 0x80);
   Vdp2Regs->MPABRB = Vdp2Regs->MPCDRB = Vdp2Regs->MPEFRB = Vdp2Regs->MPGHRB = 0x0505;
   Vdp2Regs->MPIJRB = Vdp2Regs->MPKLRB = Vdp2Regs->MPMNRB = Vdp2Regs->MPOPRB = 0x0505;
   Vdp2Regs->KTAOF |= 0x100;
   T1WriteLong(Vdp2Ram, 0x700D4, 512 << 16);
   for (i = 0; i < 512; i++)
      T1WriteLong(Vdp2Ram, 0x40800 + i * 4, ((i < 160 ? 3 : 1) << 24) | 0x10000);
}

int main(void)
{
   int i;
   yabauseinit_struct init = {0};
   init.biospath = ""; init.buppath = ""; init.skip_load = 1;
   if (YabauseInit(&init)) return 2;
   VideoUseGL = 0;
   if (VideoChangeCore(VIDCORE_SOFT)) return 2;

   setup(); render();
   check_pixel(20, 0, 320, 0x0000F8, "first row reads the current coefficient");
   check_pixel(20, 20, 320, 0x0000F8, "left coefficient color");
   check_pixel(180, 20, 320, 0x00F800, "right coefficient color on the same row");

   /* Keep per-line palette banks while replacing each pixel's low bits. */
   Vdp2Regs->LCTA.all |= 0x80000000;
   for (i = 1; i < 224; i++) T1WriteWord(Vdp2Ram, 0x60000 + i * 2, 0x287);
   T2WriteWord(Vdp2ColorRam, 0x281 * 2, 0x7C00);
   T2WriteWord(Vdp2ColorRam, 0x282 * 2, 0x001F);
   render();
   check_pixel(20, 0, 320, 0x0000F8, "per-line palette bank first row");
   check_pixel(20, 1, 320, 0xF80000, "per-line palette bank next row left");
   check_pixel(180, 1, 320, 0x0000F8, "per-line palette bank next row right");
   setup();

   /* Panzer Dragoon uses mode 2: even parameter-B pixels use A's line color. */
   Vdp2Regs->RPMD = 2;
   setup_parameter_b();
   for (i = 160; i < 512; i++) T1WriteLong(Vdp2Ram, 0x40000 + i * 4, 0x82010000);
   render();
   check_pixel(20, 20, 320, 0x0000F8, "mode 2 parameter A");
   check_pixel(180, 20, 320, 0x00F800, "mode 2 parameter B keeps A line color");

   setup(); setup_parameter_b();
   Vdp2Regs->RPMD = 1;
   Vdp2Regs->KTCTL = 0x1100;
   render();
   check_pixel(20, 20, 320, 0xF80000, "mode 1 parameter B left");
   check_pixel(180, 20, 320, 0x0000F8, "mode 1 parameter B right");

   setup(); setup_parameter_b();
   Vdp2Regs->RPMD = 3;
   Vdp2Regs->KTCTL = 0x1111;
   Vdp2Regs->WCTLD = 3; /* A inside window 0, B outside. */
   Vdp2Regs->WPSX0 = 0; Vdp2Regs->WPEX0 = 319;
   Vdp2Regs->WPSY0 = 0; Vdp2Regs->WPEY0 = 223;
   render();
   check_pixel(20, 20, 320, 0x0000F8, "mode 3 A's coefficient inside window");
   check_pixel(180, 20, 320, 0x0000F8, "mode 3 B's coefficient outside window");
   Vdp2Regs->KTCTL = 0x1101; /* Only B enables coefficient line colors. */
   render();
   check_pixel(20, 20, 320, 0xF8F800, "mode 3 A uses ordinary line color");
   check_pixel(180, 20, 320, 0x0000F8, "mode 3 B still uses coefficient color");

   setup();
   Vdp2Regs->KTCTL = 1; /* Coefficient line-color use disabled. */
   render();
   check_pixel(20, 20, 320, 0xF8F800, "ordinary line color left");
   check_pixel(180, 20, 320, 0xF8F800, "ordinary line color right");

   Vdp2Regs->KTCTL = 0x13; /* 16-bit coefficients have no line-color bits. */
   Vdp2Regs->KTAOF = 2;
   for (i = 0; i < 512; i++) T1WriteWord(Vdp2Ram, 0x40000 + i * 2, 0x400);
   render();
   check_pixel(20, 20, 320, 0xF8F800, "16-bit coefficients use ordinary line color");

   setup();
   T1WriteLong(Vdp2Ram, 0x70058, 0x10000);
   T1WriteLong(Vdp2Ram, 0x7005C, 0);
   render();
   check_pixel(20, 20, 320, 0x0000F8, "per-line coefficients upper row");
   check_pixel(20, 180, 320, 0x00F800, "per-line coefficients lower row");

   setup();
   Vdp2Regs->TVMD = 0x8002; /* High resolution doubles each rotation pixel. */
   render();
   check_pixel(40, 20, 640, 0x0000F8, "hires first copy");
   check_pixel(41, 20, 640, 0x0000F8, "hires second copy");
   check_pixel(360, 20, 640, 0x00F800, "hires right coefficient");

   setup();
   Vdp2Regs->TVMD = 0x80E3; /* 704 x 512, interlaced fields. */
   T1WriteLong(Vdp2Ram, 0x70058, 0x10000);
   T1WriteLong(Vdp2Ram, 0x7005C, 0);
   render();
   {
      int start, increment;
      Vdp2GetInterlaceInfo(&start, &increment);
      check_pixel(40, 20 + start, 704, 0x0000F8, "interlaced field upper row");
      check_pixel(40, 400 + start, 704, 0x00F800, "interlaced field uses layer row");
      check_pixel(703, 510 + start, 704, 0x00F800, "last interlaced pixel");
   }

   setup();
   VIDSoftSetNumLayerThreads(3);
   VIDSoftSetNumPriorityThreads(4);
   render();
   check_pixel(20, 20, 320, 0x0000F8, "threaded coefficient left");
   check_pixel(180, 20, 320, 0x00F800, "threaded coefficient right");
   {
      u32 *layers[VIDSOFT_LAYER_PREVIEW_COUNT];
      int width = 0, height = 0;
      for (i = 0; i < VIDSOFT_LAYER_PREVIEW_COUNT; i++) layers[i] = calloc(704 * 512, sizeof(u32));
      if (VIDSoftRenderLayerPreviews(layers, &width, &height) != 0 || width != 320 || height != 224)
      {
         fprintf(stderr, "FAIL: coefficient layer preview capture\n");
         failures++;
      }
      else if (layers[5][20 * width + 20] != 0xFF000000) /* Preview omits blending. */
      {
         fprintf(stderr, "FAIL: coefficient layer preview should remain unblended\n");
         failures++;
      }
      TitanRender(output);
      check_pixel(20, 20, 320, 0x0000F8, "preview preserves live coefficient left");
      check_pixel(180, 20, 320, 0x00F800, "preview preserves live coefficient right");
      for (i = 0; i < VIDSOFT_LAYER_PREVIEW_COUNT; i++) free(layers[i]);
   }
   VIDSoftSetNumLayerThreads(0);
   VIDSoftSetNumPriorityThreads(0);

   YabauseDeInit();
   if (failures) return 1;
   puts("Rotation line-color regressions passed");
   return 0;
}
