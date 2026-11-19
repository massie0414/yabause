/* Exercise signed DSP output through the actual SCSP mixer, without a game. */
#include <stdio.h>
#include "yui.h"
#include "peripheral.h"
#include "cs0.h"
#include "m68kcore.h"
#include "scsp.h"
#include "scspdsp.h"
#include "scsp_dsp_jit.h"

SH2Interface_struct *SH2CoreList[] = { &SH2Interpreter, NULL };
PerInterface_struct *PERCoreList[] = { &PERDummy, NULL };
CDInterface *CDCoreList[] = { &DummyCD, NULL };
SoundInterface_struct *SNDCoreList[] = { &SNDDummy, NULL };
VideoInterface_struct *VIDCoreList[] = { &VIDDummy, NULL };
M68K_struct *M68KCoreList[] = { &M68KDummy, &M68KMusashi, NULL };
void YuiErrorMsg(const char *text) { fprintf(stderr, "%s\n", text); }
void YuiSwapBuffers(void) {}
extern void new_scsp_run_sample(void);
extern int new_scsp_outbuf_pos;
extern s32 new_scsp_outbuf_l[900], new_scsp_outbuf_r[900];
static int failures, checks;

static void check_sample(int jit_core, int level, int pan, int input,
                         int expected_l, int expected_r)
{
   int l = new_scsp_outbuf_l[0], r = new_scsp_outbuf_r[0];
   checks++;
   if (l != expected_l || r != expected_r)
   {
      if (failures < 12)
         fprintf(stderr, "FAIL: %s level=%d pan=%d input=%d: (%d,%d), expected (%d,%d)\n",
                 jit_core ? "JIT" : "interpreter", level, pan, input,
                 l, r, expected_l, expected_r);
      failures++;
   }
}

static void effect_output(int jit_core)
{
   static const int inputs[] = { -32768, -16384, -1024, -2, 0, 2, 1024, 16384, 32766 };
   static const int pans[] = { 0, 2, 0x12, 0xf, 0x1f };
   int level, p, n, effect;
   union ScspDspInstruction instruction = {0};
   yabsys.use_scsp_dsp_jit = jit_core;
   use_new_scsp = 1;
   ScspReset();
   ScspWriteWord(0x400, 15);
   /* MIXS0 -> multiply by 0.5 -> EFREG4. No RAM or feedback effects. */
   dsp_inf.set_coef(0x800, 0);
   instruction.part.xsel = 1;
   instruction.part.ira = 0x20;
   instruction.part.ysel = 1;
   instruction.part.zero = 1;
   dsp_inf.set_mpro(instruction.all, 0);
   instruction.all = 0;
   instruction.part.ewt = 1;
   instruction.part.ewa = 4;
   dsp_inf.set_mpro(instruction.all, 1);
   if (jit_core) scsp_dsp_jit_need_recompile();

   for (level = 1; level <= 7; level++)
      for (p = 0; p < sizeof(pans) / sizeof(pans[0]); p++)
         for (n = 0; n < sizeof(inputs) / sizeof(inputs[0]); n++)
         {
            int pan_l = (pans[p] & 0x10) ? 0 : (pans[p] & 15);
            int pan_r = (pans[p] & 0x10) ? (pans[p] & 15) : 0;
            ScspWriteWord(4 * 32 + 0x16, (level << 5) | pans[p]);
            dsp_inf.set_mixs(0, inputs[n]);
            new_scsp_outbuf_pos = 0;
            new_scsp_run_sample();
            effect = (inputs[n] / 2) >> (7 - level);
            check_sample(jit_core, level, pans[p], inputs[n],
                         (effect >> pan_l) >> 2, (effect >> pan_r) >> 2);
         }
}

static void disabled_lfo(void)
{
   int waveform, ramp, i, reference[4];
   /* A 22.05 kHz alternating wave must repeat every four output samples.
      A constant wave must retain its amplitude. Both LFO depths are zero,
      so frequency and waveform selection must not change either property. */
   yabsys.use_scsp_dsp_jit = 0;
   for (waveform = 0; waveform < 4; waveform++)
      for (ramp = 0; ramp < 2; ramp++)
      {
         ScspReset();
         ScspWriteWord(0x400, 15);
         for (i = 0; i < 8; i++)
            T2WriteWord(SoundRam, 0x1000 + i * 2,
                        ramp ? ((i & 1) ? -8192 : 8192) : 4096);
         ScspWriteWord(0x02, 0x1000);
         ScspWriteWord(0x06, 8);
         ScspWriteWord(0x08, 31);
         ScspWriteWord(0x10, 0x7800);
         ScspWriteWord(0x12, (31 << 10) | (waveform << 8) | (waveform << 3));
         ScspWriteWord(0x16, 7 << 13);
         ScspWriteWord(0x00, 0x1820);
         for (i = 0; i < 8192 + 68; i++)
         {
            int actual, expected;
            new_scsp_outbuf_pos = 0;
            new_scsp_run_sample();
            actual = new_scsp_outbuf_l[0];
            if (i >= 64 && i < 68) reference[i - 64] = actual;
            if (i < 68) continue;
            expected = ramp ? reference[i & 3] : 1024;
            checks++;
            if (actual != expected || (ramp && actual != 2048 && actual != -2048))
            {
               if (failures < 12)
                  fprintf(stderr, "FAIL: disabled LFO wave=%d ramp=%d sample=%d: %d, expected %d\n",
                          waveform, ramp, i, actual, expected);
               failures++;
            }
         }
      }
}

static void write_instruction(unsigned int step, u64 instruction)
{
   int word;
   for (word = 0; word < 4; word++)
      ScspWriteWord(0x800 + step * 8 + word * 2,
                    (u16)(instruction >> (48 - word * 16)));
}

static void madrs_output(int jit_core)
{
   /* All 32 MADRS words are independent R/W registers, including 16..31.
      Check the CPU readback and the registers actually used by the DSP. */
   int i;
   union ScspDspInstruction instruction = {0};
   yabsys.use_scsp_dsp_jit = jit_core;
   use_new_scsp = 1;
   ScspReset();
   for (i = 0; i < 32; i++)
      ScspWriteWord(0x780 + i * 2, 0x8100 + i * 0x101);
   for (i = 0; i < 32; i++)
   {
      u16 expected = 0x8100 + i * 0x101;
      u16 actual = ScspReadWord(0x780 + i * 2);
      u16 dsp_actual = (u16)dsp_inf.get_madrs(i);
      checks += 2;
      if (actual != expected || dsp_actual != expected)
      {
         if (failures < 12)
            fprintf(stderr, "FAIL: %s MADRS%d: CPU=%04x DSP=%04x expected=%04x\n",
                    jit_core ? "JIT" : "interpreter", i, actual, dsp_actual, expected);
         failures += (actual != expected) + (dsp_actual != expected);
      }
   }

   /* Read a distinct sound-RAM location through each MASA selector, then
      multiply by 0.5 and route EFREG4 through the real stereo mixer.
      0x0000 and 0x8000 encode +0x400000 and -0x800000 in DSP float format. */
   ScspWriteWord(0x400, 15);
   ScspWriteWord(0x700, 0x4000);
   ScspWriteWord(4 * 32 + 0x16, 7 << 5);
   T2WriteWord(SoundRam, 0, 0x6000); /* Silence at the unset MADRS address. */
   for (i = 0; i < 32; i++)
   {
      ScspWriteWord(0x780 + i * 2, 0x1000 + i);
      T2WriteWord(SoundRam, (0x1000 + i) * 2, (i & 1) ? 0x8000 : 0x0000);
   }
   instruction.part.iwt = 1;
   write_instruction(3, instruction.all);
   instruction.all = 0;
   instruction.part.xsel = 1;
   instruction.part.ysel = 1;
   instruction.part.zero = 1;
   write_instruction(4, instruction.all);
   instruction.all = 0;
   instruction.part.ewt = 1;
   instruction.part.ewa = 4;
   write_instruction(5, instruction.all);
   for (i = 0; i < 32; i++)
   {
      int expected = (i & 1) ? -4096 : 2048;
      instruction.all = 0;
      instruction.part.table = 1;
      instruction.part.mrd = 1;
      instruction.part.masa = i;
      write_instruction(1, instruction.all);
      new_scsp_outbuf_pos = 0;
      new_scsp_run_sample();
      check_sample(jit_core, 7, 0, i, expected, expected);
   }
}

int main(void)
{
   yabauseinit_struct init = {0};
   init.m68kcoretype = 3; init.biospath = ""; init.buppath = "";
   init.skip_load = 1;
   if (YabauseInit(&init) != 0) return 2;
   effect_output(0);
   effect_output(1);
   disabled_lfo();
   madrs_output(0);
   madrs_output(1);
   YabauseDeInit();
   printf("SCSP mixing, disabled LFO and MADRS: %d checks, %d failures\n", checks, failures);
   return failures ? 1 : 0;
}
