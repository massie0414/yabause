#include <stdio.h>
#include <string.h>
#include "yui.h"
#include "peripheral.h"
#include "m68kcore.h"
#include "cs2.h"
#include "scsp.h"
#include "scu.h"

SH2Interface_struct *SH2CoreList[] = { &SH2Interpreter, NULL };
PerInterface_struct *PERCoreList[] = { &PERDummy, NULL };
CDInterface *CDCoreList[] = { &DummyCD, NULL };
SoundInterface_struct *SNDCoreList[] = { &SNDDummy, NULL };
VideoInterface_struct *VIDCoreList[] = { &VIDDummy, NULL };
M68K_struct *M68KCoreList[] = { &M68KDummy, &M68KMusashi, NULL };
void YuiErrorMsg(const char *text) { fprintf(stderr, "%s\n", text); }
void YuiSwapBuffers(void) {}
extern scudspregs_struct *ScuDsp;
extern void dsp_dma01(scudspregs_struct *, u32);
extern void dsp_dma03(scudspregs_struct *, u32);
extern void dsp_dma05(scudspregs_struct *, u32);
extern void dsp_dma07(scudspregs_struct *, u32);
extern void scu_dma_tick_all(u32);

int main(void)
{
   yabauseinit_struct init = {0};
   int timed, form, add, i, checks = 0, failures = 0;
   void (*readers[])(scudspregs_struct *, u32) = {dsp_dma01, dsp_dma03, dsp_dma05, dsp_dma07};
   const u32 modes[] = {0, 0x2000, 0x4000, 0x6000};
   const u32 address = 0x05a01000;
   init.m68kcoretype = 3; init.biospath = ""; init.buppath = ""; init.skip_load = 1;
   if (YabauseInit(&init)) return 2;
   for (i = 0; i < 16; i++) SoundRamWriteLong(0x1000 + i * 4, 0x12340000 + i * 0x101);
   for (timed = 0; timed < 2; timed++)
      for (form = 0; form < 4; form++)
         for (add = 0; add < 8; add++)
         {
            u32 instruction = 0xc0000000 | modes[form] | (add << 15);
            int valid = 1;
            ScuReset();
            memset(ScuDsp->MD, 0, sizeof(ScuDsp->MD));
            yabsys.use_scu_dma_timing = timed;
            ScuDsp->RA0 = address >> 2;
            ScuDsp->CT[0] = 61;
            ScuDsp->CT[1] = 0;
            ScuDsp->MD[1][0] = 8;
            /* Destination bank 0; register-count forms read count from M1. */
            instruction |= (form & 1) ? 1 : 8;
            readers[form](ScuDsp, instruction);
            scu_dma_tick_all(64);
            for (i = 0; i < 8; i++)
               if (ScuDsp->MD[0][(61 + i) & 63] != 0x12340000 + i * 0x101) valid = 0;
            if (ScuDsp->CT[0] != 5) valid = 0;
            if (ScuDsp->RA0 != (address >> 2) + ((form >= 2) ? 0 : 8)) valid = 0;
            checks++;
            if (!valid)
            {
               failures++;
               fprintf(stderr, "FAIL timed=%d form=%d add=%d: B-bus contents/address/counter\n", timed, form, add);
            }
         }
   YabauseDeInit();
   printf("SCU DSP B-bus reads: %d checks, %d failures\n", checks, failures);
   return failures ? 1 : 0;
}
