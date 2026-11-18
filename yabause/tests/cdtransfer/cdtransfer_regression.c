/* Synthetic sectors exercise the same CD read paths used by movie streaming. */
#include <stdio.h>
#include <string.h>
#include "yui.h"
#include "peripheral.h"
#include "m68kcore.h"
#include "cs2.h"

SH2Interface_struct *SH2CoreList[] = { &SH2Interpreter, NULL };
PerInterface_struct *PERCoreList[] = { &PERDummy, NULL };
CDInterface *CDCoreList[] = { &DummyCD, NULL };
SoundInterface_struct *SNDCoreList[] = { &SNDDummy, NULL };
VideoInterface_struct *VIDCoreList[] = { &VIDDummy, NULL };
M68K_struct *M68KCoreList[] = { &M68KDummy, &M68KMusashi, NULL };
void YuiErrorMsg(const char *text) { fprintf(stderr, "%s\n", text); }
void YuiSwapBuffers(void) {}
static int checks, failures;

static unsigned char pattern(int sector, int byte)
{
   return (unsigned char)(sector * 37 + byte * 13 + byte / 256);
}

static void check(int condition, int path, int remove, int offset, const char *what)
{
   checks++;
   if (!condition)
   {
      failures++;
      fprintf(stderr, "FAIL path=%d delete=%d offset=%d: %s\n", path, remove, offset, what);
   }
}

static void transfer(int path, int remove, int offset)
{
   enum { sectors = 20, bytes = 2048, count = 2 };
   partition_struct *part = &Cs2Area->partition[0];
   unsigned int output[count * bytes / 4];
   int i, j, valid = 1;
   Cs2Area->reg.CR1 = 0;
   Cs2Area->reg.CR3 = 0;
   Cs2ResetSelector();
   part->size = 0;
   for (i = 0; i < sectors; i++)
   {
      block_struct *block = Cs2AllocateBlock(&part->blocknum[i], bytes);
      part->block[i] = block;
      part->numblocks++;
      part->size += bytes;
      block->FAD = 150 + i;
      for (j = 0; j < bytes; j++) block->data[j] = pattern(i, j);
   }
   Cs2Area->reg.CR2 = offset;
   Cs2Area->reg.CR3 = 0;
   Cs2Area->reg.CR4 = count;
   if (remove) Cs2GetThenDeleteSectorData();
   else Cs2GetSectorData();

   /* Split the copy inside a sector to exercise the byte cursor as well. */
   if (path == 0)
      for (i = 0; i < count * bytes / 4; i++) output[i] = Cs2ReadLong(NULL, 0x25818000);
   else if (path == 1)
   {
      Cs2RapidCopyT1(output, 17);
      Cs2RapidCopyT1(output + 17, count * bytes / 4 - 17);
   }
   else
   {
      Cs2RapidCopyT2(output, 17);
      Cs2RapidCopyT2(output + 17, count * bytes / 4 - 17);
   }
   for (i = 0; i < count * bytes; i++)
   {
      unsigned char actual;
      if (path == 0) actual = (unsigned char)(output[i / 4] >> (24 - 8 * (i & 3)));
      else if (path == 1) actual = T1ReadByte((u8 *)output, i);
      else actual = T2ReadByte((u8 *)output, i);
      if (actual != pattern(offset + i / bytes, i % bytes)) valid = 0;
   }
   check(valid, path, remove, offset, "requested sector contents");
   check(Cs2Area->cdwnum == count * bytes, path, remove, offset, "transfer byte count");
   Cs2EndDataTransfer();
   check(part->numblocks == sectors - (remove ? count : 0), path, remove, offset, "remaining sector count");
   valid = part->size == part->numblocks * bytes;
   for (i = 0; i < part->numblocks; i++)
      if (part->block[i]->FAD != 150 + i + ((remove && i >= offset) ? count : 0)) valid = 0;
   check(valid, path, remove, offset, "only requested sectors deleted; order preserved");
}

int main(void)
{
   yabauseinit_struct init = {0};
   const int offsets[] = { 0, 1, 16, 18 };
   int path, remove, i;
   init.m68kcoretype = 3; init.biospath = ""; init.buppath = ""; init.skip_load = 1;
   if (YabauseInit(&init)) return 2;
   for (path = 0; path < 3; path++)
      for (remove = 0; remove < 2; remove++)
         for (i = 0; i < 4; i++) transfer(path, remove, offsets[i]);
   YabauseDeInit();
   printf("CD sector transfer: %d checks, %d failures\n", checks, failures);
   return failures ? 1 : 0;
}
