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
static VIDSoftRbg0Sample samples[704 * 512 + 1];
static VIDSoftRbg0Preview rotation = { samples };

static void check(int condition, const char *message)
{
   if (!condition) { fprintf(stderr, "FAIL: %s\n", message); failures++; }
}

static void capture_and_check(int software)
{
   int i, width, height, titan_w, titan_h, after_w, after_h;
   u32 sprite_count = 99;
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
   check(VIDSoftRenderLayerPreviewsWithRbg0(layers, &width, &height, &sprite_count, &rotation) == 0, "capture succeeds with coordinate tracing");
   check(sprite_count == 0, "normal sprites do not count as distorted sprites");
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

static void check_distorted_sprites(void)
{
   int i, w, h;
   u32 sprite_count = 99;
   const u16 commands[] = { 0, 1, 4, 0x4002, 2, 3, 0x8002 };
   setup_layer_scene();
   Vdp1External.disptoggle = 0;
   T1WriteWord(Vdp1Ram, 0, 2);
   T1WriteWord(Vdp1Ram, 0x10, 23);
   T1WriteWord(Vdp1Ram, 0x12, 16);
   T1WriteWord(Vdp1Ram, 0x14, 23);
   T1WriteWord(Vdp1Ram, 0x16, 23);
   T1WriteWord(Vdp1Ram, 0x18, 16);
   T1WriteWord(Vdp1Ram, 0x1A, 23);
   check(VIDSoftRenderLayerPreviews(layers, &w, &h, &sprite_count) == 0, "single distorted sprite capture");
   check(sprite_count == 1, "one distorted sprite draw counts as one");
   check(images[0][20 * w + 20] == 0xFFF800F8, "count accompanies the rendered distorted sprite");
   sprite_count = 99;
   check(VIDSoftRenderLayerPreviewsWithRbg0(layers, &w, &h, &sprite_count, &rotation) == 0,
      "capture distorted sprites and RBG0 coordinates together");
   check(sprite_count == 1 && images[0][20 * w + 20] == 0xFFF800F8,
      "coordinate tracing preserves distorted sprite count and image");
   check(samples[30 * w + 20].parameter == 1 && samples[30 * w + 20].x == 20
      && samples[30 * w + 20].y == 30, "sprite counting preserves RBG0 coordinates");
   T1WriteWord(Vdp1Ram, 0, 3);
   check(VIDSoftRenderLayerPreviews(layers, &w, &h, &sprite_count) == 0 && sprite_count == 1,
      "type 3 distorted sprite counts once and capture resets the count");

   for (i = 1; i < sizeof(commands) / sizeof(commands[0]); i++)
      memcpy(Vdp1Ram + i * 0x20, Vdp1Ram, 0x20);
   for (i = 0; i < sizeof(commands) / sizeof(commands[0]); i++)
      T1WriteWord(Vdp1Ram, i * 0x20, commands[i]);
   check(VIDSoftRenderLayerPreviews(layers, &w, &h, &sprite_count) == 0 && sprite_count == 2,
      "exclude normal/scaled sprites, polygons, skipped draws and end commands");

   /* Call the same sprite twice; a third sprite is never reached. */
   T1WriteWord(Vdp1Ram, 0, 0x6000);
   T1WriteWord(Vdp1Ram, 2, 0x10);
   T1WriteWord(Vdp1Ram, 0x20, 0x6000);
   T1WriteWord(Vdp1Ram, 0x22, 0x10);
   T1WriteWord(Vdp1Ram, 0x40, 0x8000);
   T1WriteWord(Vdp1Ram, 0x60, 2);
   T1WriteWord(Vdp1Ram, 0x80, 0x3002);
   check(VIDSoftRenderLayerPreviews(layers, &w, &h, &sprite_count) == 0 && sprite_count == 2,
      "count each execution through CALL/RETURN, excluding unreachable sprites");
   T1WriteWord(Vdp1Ram, 0x20, 0x8000);
   check(VIDSoftRenderLayerPreviews(layers, &w, &h, &sprite_count) == 0 && sprite_count == 1,
      "a later capture can decrease the count");
   Vdp2Regs->TVMD = 0;
   check(VIDSoftRenderLayerPreviews(layers, &w, &h, &sprite_count) == 0 && sprite_count == 0,
      "display off clears the count with the image");
   Vdp2Regs->TVMD = 0x8000;
   T1WriteWord(Vdp1Ram, 0, 0x8000);
   check(VIDSoftRenderLayerPreviews(layers, &w, &h, &sprite_count) == 0 && sprite_count == 0,
      "empty command list has no distorted sprites");
   sprite_count = 99;
   check(VIDSoftRenderLayerPreviews(NULL, &w, &h, &sprite_count) == -1 && sprite_count == 0,
      "unavailable capture clears stale counts");
   setup_layer_scene();
}

static void capture_rotation(void)
{
   int w, h, i;
   for (i = 0; i < 270; i++) Vdp2Lines[i] = *Vdp2Regs;
   samples[704 * 512].parameter = 99;
   check(VIDSoftRenderLayerPreviewsWithRbg0(layers, &w, &h, NULL, &rotation) == 0, "rotation capture");
   check(samples[704 * 512].parameter == 99, "coordinate buffer bounds");
}

static void check_sample(int x, int y, int width, int sx, int sy, int parameter, s32 kx, s32 ky)
{
   const VIDSoftRbg0Sample *s = &samples[y * width + x];
   if (s->x != sx || s->y != sy || s->parameter != parameter || s->kx != kx || s->ky != ky)
   {
      fprintf(stderr, "FAIL: sample (%d,%d): source (%d,%d), parameter %d, k (%x,%x); expected (%d,%d), %d, (%x,%x)\n",
         x, y, s->x, s->y, s->parameter, s->kx, s->ky, sx, sy, parameter, kx, ky);
      failures++;
   }
}

static void check_rotation(void)
{
   int i;
   setup_layer_scene();
   capture_rotation();
   check_sample(20, 30, 320, 20, 30, 1, 0x10000, 0x10000);
   check(samples[0].parameter == 1 && images[5][0] == 0, "transparent texel still has geometry");
   check(rotation.enabled && rotation.mode == 0 && !rotation.coefficient_enabled[0], "rotation metadata");

   T1WriteLong(Vdp2Ram, 0x70000, (u32)(-32 * 65536));
   T1WriteLong(Vdp2Ram, 0x7004C, 0x20000);
   capture_rotation();
   check_sample(20, 30, 320, -24, 30, 1, 0x20000, 0x10000);
   check(samples[30 * 320 + 20].mapx == 2024, "negative source wraps into map before tile addressing");

   setup_layer_scene();
   T1WriteLong(Vdp2Ram, 0x7001C, 0); /* 90-degree rotation: x = -screen y, y = screen x */
   T1WriteLong(Vdp2Ram, 0x70020, 0xFFFF0000);
   T1WriteLong(Vdp2Ram, 0x70028, 0x10000);
   T1WriteLong(Vdp2Ram, 0x7002C, 0);
   capture_rotation();
   check_sample(20, 30, 320, -30, 20, 1, 0x10000, 0x10000);

   setup_layer_scene();
   Vdp2Regs->KTCTL = 1;
   Vdp2Regs->KTAOF = 1;
   T1WriteLong(Vdp2Ram, 0x7005C, 0x10000);
   for (i = 0; i < 512; i++) T1WriteLong(Vdp2Ram, 0x40000 + i * 4, i < 160 ? 0x8000 : 0x20000);
   capture_rotation();
   check_sample(20, 30, 320, 10, 15, 1, 0x8000, 0x8000);
   check_sample(180, 30, 320, 360, 60, 1, 0x20000, 0x20000);
   check(rotation.coefficient_enabled[0], "coefficient metadata");
   T1WriteLong(Vdp2Ram, 0x70058, 0x10000);
   T1WriteLong(Vdp2Ram, 0x7005C, 0);
   capture_rotation();
   check_sample(20, 180, 320, 40, 360, 1, 0x20000, 0x20000);

   /* B has a different horizontal offset; mode 2's color source remains A
    * but its image coordinates must be labeled B. */
   memcpy(Vdp2Ram + 0x70080, Vdp2Ram + 0x70000, 0x80);
   T1WriteLong(Vdp2Ram, 0x70080, 100 << 16);
   Vdp2Regs->MPABRB = Vdp2Regs->MPCDRB = Vdp2Regs->MPEFRB = Vdp2Regs->MPGHRB = 0x0505;
   Vdp2Regs->MPIJRB = Vdp2Regs->MPKLRB = Vdp2Regs->MPMNRB = Vdp2Regs->MPOPRB = 0x0505;
   T1WriteLong(Vdp2Ram, 0x70058, 0);
   T1WriteLong(Vdp2Ram, 0x7005C, 0x10000);
   for (i = 0; i < 512; i++) T1WriteLong(Vdp2Ram, 0x40000 + i * 4, i < 160 ? 0x10000 : 0x80010000);
   Vdp2Regs->RPMD = 2;
   capture_rotation();
   check_sample(20, 30, 320, 20, 30, 1, 0x10000, 0x10000);
   check_sample(180, 30, 320, 280, 30, 2, 0x10000, 0x10000);
   Vdp2Regs->RPMD = 0;
   capture_rotation();
   check(samples[30 * 320 + 180].parameter == 0, "invalid coefficient clears previous sample");

   for (i = 0; i < 512; i++) T1WriteLong(Vdp2Ram, 0x40000 + i * 4, 0x10000);
   Vdp2Regs->RPMD = 3;
   Vdp2Regs->WCTLD = 3;
   Vdp2Regs->WPSX0 = 0; Vdp2Regs->WPEX0 = 319;
   Vdp2Regs->WPSY0 = 0; Vdp2Regs->WPEY0 = 223;
   capture_rotation();
   check_sample(20, 30, 320, 20, 30, 1, 0x10000, 0x10000);
   check_sample(180, 30, 320, 280, 30, 2, 0x10000, 0x10000);
   Vdp2Regs->RPMD = 1;
   capture_rotation();
   check_sample(20, 30, 320, 120, 30, 2, 0x10000, 0x10000);

   Vdp2Regs->RPMD = 0;
   Vdp2Regs->WCTLC = 3;
   capture_rotation();
   check(samples[30 * 320 + 180].parameter == 0, "layer window clips coordinate samples");
   Vdp2Regs->WCTLC = 0;
   Vdp2Regs->PLSZ = 2 << 10;
   T1WriteLong(Vdp2Ram, 0x70000, 2040 << 16);
   capture_rotation();
   check(samples[30 * 320 + 20].parameter == 0, "screen-over clips coordinate samples");

   setup_layer_scene();
   Vdp2Regs->KTCTL = 3; /* 16-bit coefficient */
   Vdp2Regs->KTAOF = 2;
   T1WriteWord(Vdp2Ram, 0x40000, 0x200);
   capture_rotation();
   check_sample(20, 30, 320, 10, 15, 1, 0x8000, 0x8000);
   Vdp2Regs->TVMD = 0x80E3;
   capture_rotation();
   check_sample(702, 510, 704, 175, 127, 1, 0x8000, 0x8000);
   check_sample(703, 511, 704, 175, 127, 1, 0x8000, 0x8000);
   Vdp2Regs->BGON = 0x20; /* RBG1 must not pollute this trace. */
   capture_rotation();
   check(!rotation.enabled && samples[30 * 704 + 20].parameter == 0, "disabled RBG0 clears samples even with RBG1 enabled");
   Vdp2Regs->TVMD = 0;
   capture_rotation();
   check(!rotation.enabled && samples[0].parameter == 0, "display off clears geometry");
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
   check(VIDSoftRenderLayerPreviews(layers, &w, &h, NULL) == 0, "RBG1-only preview capture");
   check(images[6][20 * w + 20] == 0xFFF80000, "RBG1 preview uses its own fixed output and parameter B");
   check(images[1][20 * w + 20] == 0, "NBG0 stays empty in RBG1 mode");
   check(images[5][20 * w + 20] == 0, "RBG0 is empty in RBG1-only mode");
   for (i = 2; i < 5; i++) check(images[i][20 * w + 20] == 0, "unused normal backgrounds stay empty");
   check(Vdp2External.disptoggle == 0, "RBG1 preview preserves display switches");
   setup_rbg1_scene(1);
   check(VIDSoftRenderLayerPreviews(layers, &w, &h, NULL) == 0, "both rotation backgrounds captured");
   check(images[6][20 * w + 20] == 0xFFF80000, "RBG1 keeps its separate image");
   check(images[1][20 * w + 20] == 0, "NBG0 does not contain either rotation background");
   check(images[5][20 * w + 20] == 0xFF00F8F8, "RBG0 keeps its separate image");
   setup_layer_scene();
   check(VIDSoftRenderLayerPreviews(layers, &w, &h, NULL) == 0, "return to normal background mode");
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
   check_distorted_sprites();
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
   check_distorted_sprites();
   check_rbg1(after);
   check_rotation();
   setup_layer_scene();
   VIDSoftSetVdp1ThreadEnable(1);
   VIDSoftSetNumLayerThreads(3);
   for (i = 0; i < 3; i++)
   {
      Vdp1Regs->addr = 0;
      VIDSoft.Vdp1DrawStart();
      VIDSoft.Vdp2DrawScreens();
      check(VIDSoftRenderLayerPreviewsWithRbg0(layers, &w, &h, NULL, &rotation) == 0, "coordinate capture waits for active renderer workers");
   }
   VIDSoftSetVdp1ThreadEnable(0);
   VIDSoftSetNumLayerThreads(0);
   Vdp2Regs->TVMD = 0x80E3; /* 704 x 512, double-density interlace. */
   check(VIDSoftRenderLayerPreviews(layers, &w, &h, NULL) == 0 && w == 704 && h == 512, "maximum interlaced dimensions");
   for (i = 0; i < VIDSOFT_LAYER_PREVIEW_COUNT; i++) check(images[i][704 * 512] == 0x12345678, "interlaced output bounds");
   T1WriteLong(Vdp2Ram, 0x70020, 0x8000); /* Rotate/shear: use the non-coefficient path. */
   check(VIDSoftRenderLayerPreviews(layers, &w, &h, NULL) == 0, "rotated interlaced background capture");
   check(images[5][500 * w + 500] == 0xFF00F8F8, "rotated background fills the last field rows");
   for (i = 0; i < VIDSOFT_LAYER_PREVIEW_COUNT; i++) check(images[i][704 * 512] == 0x12345678, "rotated output bounds");
   Vdp2Regs->TVMD = 0;
   check(VIDSoftRenderLayerPreviews(layers, &w, &h, NULL) == 0, "display off capture");
   for (i = 0; i < VIDSOFT_LAYER_PREVIEW_COUNT; i++) check(images[i][20 * w + 20] == 0, "display off clears old images");
   YabauseDeInit();
   check(VIDSoftRenderLayerPreviews(layers, &w, &h, NULL) == -1, "capture after teardown rejected");
   free(before); free(after);
   printf("Layer preview failures: %d\n", failures);
   return failures ? 1 : 0;
}
