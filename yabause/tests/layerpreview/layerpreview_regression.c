#include <stdio.h>
#include <stdlib.h>
#include "yui.h"
#include "peripheral.h"
#include "cs0.h"
#include "m68kcore.h"
#include "scsp.h"
#include "vidsoft.h"
#include "titan/titan.h"
#include "scene.h"

SH2Interface_struct *SH2CoreList[] = { &SH2Interpreter, NULL };
PerInterface_struct *PERCoreList[] = { &PERDummy, NULL };
CDInterface *CDCoreList[] = { &DummyCD, NULL };
SoundInterface_struct *SNDCoreList[] = { &SNDDummy, NULL };
VideoInterface_struct *VIDCoreList[] = { &VIDDummy, &VIDSoft, NULL };
M68K_struct *M68KCoreList[] = { &M68KDummy, &M68KMusashi, NULL };
void YuiErrorMsg(const char *text) { fprintf(stderr, "%s\n", text); }
void YuiSwapBuffers(void) {}
extern int VideoUseGL;
extern u8 *vdp1frontframebuffer, *vdp1backframebuffer;
extern u8 sprite_window_mask[704 * 512];
static int failures;
static u32 images[VIDSOFT_LAYER_PREVIEW_COUNT][704 * 512 + 1];
static u32 *layers[VIDSOFT_LAYER_PREVIEW_COUNT] = { images[0], images[1], images[2], images[3], images[4], images[5], images[6] };

static void check(int condition, const char *message)
{
   if (!condition) { fprintf(stderr, "FAIL: %s\n", message); failures++; }
}

static void capture_and_check(int software)
{
   int i, width, height, titan_w, titan_h, after_w, after_h;
   Vdp1 before1 = *Vdp1Regs;
   Vdp2 before2 = *Vdp2Regs;
   VideoInterface_struct *core = VIDCore;
   const u32 colors[VIDSOFT_LAYER_PREVIEW_COUNT] = { 0xFFF800F8, 0xFFF80000, 0xFF00F800, 0xFF0000F8, 0xFFF8F800, 0xFF00F8F8, 0 };
   u8 *ram = (u8 *)malloc(0x101000);
   u8 *fb = software ? (u8 *)malloc(0x80000 + sizeof(sprite_window_mask)) : NULL;
   memcpy(ram, Vdp1Ram, 0x80000);
   memcpy(ram + 0x80000, Vdp2Ram, 0x80000);
   memcpy(ram + 0x100000, Vdp2ColorRam, 0x1000);
   if (software)
   {
      VidsoftWaitForVdp1Thread();
      memcpy(fb, vdp1frontframebuffer, 0x40000);
      memcpy(fb + 0x40000, vdp1backframebuffer, 0x40000);
      memcpy(fb + 0x80000, sprite_window_mask, sizeof(sprite_window_mask));
   }
   Vdp1External.disptoggle = 0;
   Vdp2External.disptoggle = 0;
   Vdp1External.manualerase = 1;
   TitanGetResolution(&titan_w, &titan_h);
   for (i = 0; i < VIDSOFT_LAYER_PREVIEW_COUNT; i++) images[i][704 * 512] = 0x12345678;
   check(VIDSoftRenderLayerPreviews(layers, &width, &height) == 0, "capture succeeds");
   check(width == 320 && height == 224, "native preview dimensions");
   for (i = 0; i < VIDSOFT_LAYER_PREVIEW_COUNT; i++)
   {
      if (images[i][20 * width + 20] != colors[i])
         fprintf(stderr, "Layer %d: got %08X expected %08X\n", i, images[i][20 * width + 20], colors[i]);
      check(images[i][20 * width + 20] == colors[i], "isolated layer colors even with all display toggles off");
      check(images[i][0] == 0, "transparent pixel remains transparent");
      check(images[i][704 * 512] == 0x12345678, "output bounds");
   }
   check(images[0][100 * width + 100] == 0, "VDP1 has no background pixels");
   check(!Vdp1External.disptoggle && !Vdp2External.disptoggle, "display switches preserved");
   check(Vdp1External.manualerase == 1, "erase request preserved");
   check(VIDCore == core, "video backend preserved");
   check(!memcmp(&before1, Vdp1Regs, sizeof(before1)), "VDP1 registers preserved");
   check(!memcmp(&before2, Vdp2Regs, sizeof(before2)), "VDP2 registers preserved");
   check(!memcmp(ram, Vdp1Ram, 0x80000) && !memcmp(ram + 0x80000, Vdp2Ram, 0x80000)
      && !memcmp(ram + 0x100000, Vdp2ColorRam, 0x1000), "VRAM and palette preserved");
   TitanGetResolution(&after_w, &after_h);
   check(titan_w == after_w && titan_h == after_h, "live compositor dimensions preserved");
   if (software)
      check(!memcmp(fb, vdp1frontframebuffer, 0x40000) && !memcmp(fb + 0x40000, vdp1backframebuffer, 0x40000)
         && !memcmp(fb + 0x80000, sprite_window_mask, sizeof(sprite_window_mask)), "live framebuffers and sprite window preserved");
   free(ram);
   free(fb);
}

static void check_rbg1(pixel_t *output)
{
   int w, h, i;
   pixel_t enabled;
   setup_rbg1_scene(0);
   Vdp1External.disptoggle = 0;
   Vdp2External.disptoggle = 0x20;
   VIDSoft.Vdp2DrawStart();
   VIDSoft.Vdp2DrawScreens();
   TitanRender(output);
   enabled = output[20 * 320 + 20];
   check((enabled & 0xFFFFFF) != 0, "RBG1-only display switch draws the main image");
   /* The old menu could only change bits 0..4; bit 5 must now be controlled. */
   Vdp2External.disptoggle = 0x1F;
   VIDSoft.Vdp2DrawStart();
   VIDSoft.Vdp2DrawScreens();
   TitanRender(output);
   check(output[20 * 320 + 20] != enabled, "RBG1 is hidden even when every other switch is on");
   Vdp2External.disptoggle = 0;
   check(VIDSoftRenderLayerPreviews(layers, &w, &h) == 0, "RBG1-only preview capture");
   check(images[6][20 * w + 20] == 0xFFF80000, "RBG1 preview uses its own fixed output and parameter B");
   check(images[1][20 * w + 20] == 0, "NBG0 stays empty in RBG1 mode");
   check(images[5][20 * w + 20] == 0, "RBG0 is empty in RBG1-only mode");
   for (i = 2; i < 5; i++) check(images[i][20 * w + 20] == 0, "unused normal backgrounds stay empty");
   check(Vdp2External.disptoggle == 0, "RBG1 preview preserves display switches");
   setup_rbg1_scene(1);
   check(VIDSoftRenderLayerPreviews(layers, &w, &h) == 0, "both rotation backgrounds captured");
   check(images[6][20 * w + 20] == 0xFFF80000, "RBG1 keeps its separate image");
   check(images[1][20 * w + 20] == 0, "NBG0 does not contain either rotation background");
   check(images[5][20 * w + 20] == 0xFF00F8F8, "RBG0 keeps its separate image");
   setup_layer_scene();
   check(VIDSoftRenderLayerPreviews(layers, &w, &h) == 0, "return to normal background mode");
   check(images[1][20 * w + 20] == 0xFFF80000 && images[6][20 * w + 20] == 0,
      "normal mode restores NBG0 and clears the old RBG1 image");
}

int main(void)
{
   int i, w, h;
   pixel_t *before = (pixel_t *)calloc(704 * 512, sizeof(pixel_t));
   pixel_t *after = (pixel_t *)calloc(704 * 512, sizeof(pixel_t));
   yabauseinit_struct init = {0};
   init.biospath = ""; init.buppath = ""; init.skip_load = 1;
   if (YabauseInit(&init) != 0) return 2;
   setup_layer_scene();
   capture_and_check(0); /* No software renderer or Titan initialization. */
   VideoUseGL = 0;
   check(VideoChangeCore(VIDCORE_SOFT) == 0, "initialize software renderer without GL");
   setup_layer_scene();
   Vdp1External.disptoggle = 1;
   Vdp2External.disptoggle = ~0;
   VIDSoft.Vdp1DrawStart();
   VIDSoft.Vdp2DrawStart();
   VIDSoft.Vdp2DrawScreens();
   VIDSoft.Vdp2DrawEnd();
   TitanRender(before);
   capture_and_check(1);
   TitanRender(after);
   check(!memcmp(before, after, 704 * 512 * sizeof(pixel_t)), "live compositor pixels preserved");
   check_rbg1(after);
   setup_layer_scene();
   VIDSoftSetVdp1ThreadEnable(1);
   VIDSoftSetNumLayerThreads(3);
   for (i = 0; i < 3; i++)
   {
      Vdp1Regs->addr = 0;
      VIDSoft.Vdp1DrawStart();
      VIDSoft.Vdp2DrawScreens();
      check(VIDSoftRenderLayerPreviews(layers, &w, &h) == 0, "capture waits for active renderer workers");
   }
   VIDSoftSetVdp1ThreadEnable(0);
   VIDSoftSetNumLayerThreads(0);
   Vdp2Regs->TVMD = 0x80E3; /* 704 x 512, double-density interlace. */
   check(VIDSoftRenderLayerPreviews(layers, &w, &h) == 0 && w == 704 && h == 512, "maximum interlaced dimensions");
   for (i = 0; i < VIDSOFT_LAYER_PREVIEW_COUNT; i++) check(images[i][704 * 512] == 0x12345678, "interlaced output bounds");
   T1WriteLong(Vdp2Ram, 0x70020, 0x8000); /* Rotate/shear: use the non-coefficient path. */
   check(VIDSoftRenderLayerPreviews(layers, &w, &h) == 0, "rotated interlaced background capture");
   check(images[5][500 * w + 500] == 0xFF00F8F8, "rotated background fills the last field rows");
   for (i = 0; i < VIDSOFT_LAYER_PREVIEW_COUNT; i++) check(images[i][704 * 512] == 0x12345678, "rotated output bounds");
   Vdp2Regs->TVMD = 0;
   check(VIDSoftRenderLayerPreviews(layers, &w, &h) == 0, "display off capture");
   for (i = 0; i < VIDSOFT_LAYER_PREVIEW_COUNT; i++) check(images[i][20 * w + 20] == 0, "display off clears old images");
   YabauseDeInit();
   check(VIDSoftRenderLayerPreviews(layers, &w, &h) == -1, "capture after teardown rejected");
   free(before); free(after);
   printf("Layer preview failures: %d\n", failures);
   return failures ? 1 : 0;
}
