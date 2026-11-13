/* No BIOS/game required: save a playing voice, change scenes, and restore it. */
#include <stdio.h>
#include <stdlib.h>
#include "yui.h"
#include "peripheral.h"
#include "cs0.h"
#include "m68kcore.h"
#include "scsp.h"
#include "scspdsp.h"
#include "scsp_dsp_jit.h"
#include "vdp2.h"

SH2Interface_struct *SH2CoreList[] = { &SH2Interpreter, NULL };
PerInterface_struct *PERCoreList[] = { &PERDummy, NULL };
CDInterface *CDCoreList[] = { &DummyCD, NULL };
SoundInterface_struct *SNDCoreList[] = { &SNDDummy, NULL };
VideoInterface_struct *VIDCoreList[] = { &VIDDummy, NULL };
M68K_struct *M68KCoreList[] = { &M68KDummy, &M68KMusashi, NULL };
void YuiErrorMsg(const char *text) { fprintf(stderr, "%s\n", text); }
void YuiSwapBuffers(void) {}
extern void new_scsp_run_sample(void);
extern void scsp_set_use_new(int which);
extern int new_scsp_outbuf_pos;
extern s32 new_scsp_outbuf_l[900], new_scsp_outbuf_r[900];
static int failures;
static void check(int condition, const char *message)
{
   if (!condition) { fprintf(stderr, "FAIL: %s\n", message); failures++; }
}
static void samples(int count, s32 *left, s32 *right)
{
   int i;
   new_scsp_outbuf_pos = 0;
   for (i = 0; i < count; i++) new_scsp_run_sample();
   if (left) memcpy(left, new_scsp_outbuf_l, count * sizeof(s32));
   if (right) memcpy(right, new_scsp_outbuf_r, count * sizeof(s32));
}
static void setup_voice(void)
{
   int i;
   union ScspDspInstruction instruction = {0};
   ScspReset();
   for (i = 0; i < 256; i++) SoundRamWriteWord(i * 2, (u16)(i * 127 - 16000));
   ScspWriteWord(0x402, 0);
   ScspWriteWord(0x400, 15);
   ScspWriteWord(0x006, 256);
   ScspWriteWord(0x008, 31);
   ScspWriteWord(0x014, 7);
   ScspWriteWord(0x016, 0xe0e0);
   ScspWriteWord(0x000, 0x1820);
   dsp_inf.set_coef(0x4000, 0);
   instruction.part.xsel = 1; instruction.part.ira = 0x20;
   instruction.part.ysel = 1; instruction.part.zero = 1;
   dsp_inf.set_mpro(instruction.all, 0);
   instruction.all = 0; instruction.part.ewt = 1;
   dsp_inf.set_mpro(instruction.all, 1);
   if (yabsys.use_scsp_dsp_jit) scsp_dsp_jit_need_recompile();
}
static void round_trip(int jit_core, int pending_program)
{
   s32 left[512], right[512], restored_l[512], restored_r[512];
   int version, size, i, audible = 0;
   u64 program;
   FILE *state = tmpfile();
   check(state != NULL, "create temporary state");
   if (!state) return;
   yabsys.use_scsp_dsp_jit = jit_core;
   use_new_scsp = 1;
   setup_voice();
   samples(137, NULL, NULL);
   if (pending_program) dsp_inf.set_mpro(0, 0); /* Uploaded, not activated yet. */
   program = dsp_inf.get_mpro(0);
   check(SoundSaveState(state) > 0, "save active sound core");
   samples(512, left, right);
   for (i = 0; i < 512; i++) if (left[i] || right[i]) audible++;
   check(audible > 400, "fixture produces a sustained audible voice");
   /* Simulate a different area/music program, and a cold reset as well. */
   ScspReset();
   dsp_inf.set_coef(0x1000, 0);
   dsp_inf.set_mpro(0, 0);
   samples(211, NULL, NULL);
   rewind(state);
   check(StateCheckRetrieveHeader(state, "SCSP", &version, &size) == 0, "read state header");
   check(SoundLoadState(state, version, size) >= 0, "load sound state");
   check(ScspReadWord(0x006) == 256, "voice registers restored");
   check(dsp_inf.get_mpro(0) == program, "DSP program restored");
   check(dsp_inf.get_coef(0) == 0x4000, "DSP coefficient restored");
   samples(512, restored_l, restored_r);
   check(memcmp(left, restored_l, sizeof(left)) == 0, "left PCM is identical after state load");
   check(memcmp(right, restored_r, sizeof(right)) == 0, "right PCM is identical after state load");
   printf("DSP %s: compared 512 stereo samples after reset/load\n", jit_core ? "JIT" : "interpreter");
   fclose(state);
}

static void legacy_and_truncated(void)
{
   FILE *state = tmpfile(), *truncated = tmpfile();
   int version, size, value, i;
   check(state && truncated, "create compatibility fixtures");
   if (!state || !truncated) return;
   use_new_scsp = 0;
   ScspReset();
   check(SoundSaveState(state) > 0, "save legacy sound state");
   rewind(state);
   check(StateCheckRetrieveHeader(state, "SCSP", &version, &size) == 0 && version == 2, "legacy version unchanged");
   use_new_scsp = 1;
   check(SoundLoadState(state, version, size) >= 0, "legacy state remains loadable");
   check(ftell(state) == size + 12, "legacy chunk preserves following chunk position");
   fclose(state);
   state = tmpfile();
   setup_voice();
   check(SoundSaveState(state) > 0, "save extended state for truncation test");
   rewind(state);
   check(StateCheckRetrieveHeader(state, "SCSP", &version, &size) == 0, "extended header");
   for (i = 0; i < size - 4; i++) { value = fgetc(state); if (value == EOF) break; fputc(value, truncated); }
   rewind(truncated);
   check(SoundLoadState(truncated, version, size) < 0, "truncated extended sound state is rejected");
   fclose(truncated);
   fclose(state);
}
int main(void)
{
   yabauseinit_struct init = {0};
   init.m68kcoretype = 3; init.biospath = ""; init.buppath = "";
   init.skip_load = 1;
   if (YabauseInit(&init) != 0) return 2;
   round_trip(0, 0);
   round_trip(1, 0);
   round_trip(1, 1);
   legacy_and_truncated();
   YabauseDeInit();
   printf("Failures: %d\n", failures);
   return failures ? 1 : 0;
}
