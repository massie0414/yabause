/* Synthetic graphics only; no BIOS or game assets. */
#include "vdp1.h"
#include "vdp2.h"

static void setup_layer_scene(void)
{
   int layer, i;
   const u16 colors[5] = { 0x001F, 0x03E0, 0x7C00, 0x03FF, 0x7FE0 };
   memset(Vdp1Regs, 0, sizeof(*Vdp1Regs));
   memset(Vdp2Regs, 0, sizeof(*Vdp2Regs));
   memset(Vdp1Ram, 0, 0x80000);
   memset(Vdp2Ram, 0, 0x80000);
   memset(Vdp2ColorRam, 0, 0x1000);
   Vdp1Regs->systemclipX2 = Vdp1Regs->userclipX2 = 511;
   Vdp1Regs->systemclipY2 = Vdp1Regs->userclipY2 = 255;
   Vdp1Regs->EWDR = 0xFFFF;
   Vdp1External.manualerase = 1;
   T1WriteWord(Vdp1Ram, 4, 0xA8); /* RGB sprite, end codes disabled */
   T1WriteWord(Vdp1Ram, 8, 0x100);
   T1WriteWord(Vdp1Ram, 10, 0x0108); /* 8 x 8 */
   T1WriteWord(Vdp1Ram, 12, 16);
   T1WriteWord(Vdp1Ram, 14, 16);
   T1WriteWord(Vdp1Ram, 0x20, 0x8000);
   for (i = 0; i < 64; i++) T1WriteWord(Vdp1Ram, 0x800 + i * 2, 0xFC1F);

   Vdp2Regs->TVMD = 0x8000;
   Vdp2Regs->BGON = 0x1F;
   Vdp2Regs->PRINA = 0x0201;
   Vdp2Regs->PRINB = 0x0403;
   Vdp2Regs->PRIR = 5;
   Vdp2Regs->PRISA = 6;
   Vdp2Regs->SPCTL = 0x20;
   Vdp2Regs->PNCN0 = Vdp2Regs->PNCN1 = Vdp2Regs->PNCN2 = Vdp2Regs->PNCN3 = Vdp2Regs->PNCR = 0x8000;
   Vdp2Regs->ZMXN0.all = Vdp2Regs->ZMYN0.all = Vdp2Regs->ZMXN1.all = Vdp2Regs->ZMYN1.all = 0x10000;
   Vdp2Regs->MPABN0 = Vdp2Regs->MPCDN0 = 0x0101;
   Vdp2Regs->MPABN1 = Vdp2Regs->MPCDN1 = 0x0202;
   Vdp2Regs->MPABN2 = Vdp2Regs->MPCDN2 = 0x0303;
   Vdp2Regs->MPABN3 = Vdp2Regs->MPCDN3 = 0x0404;
   Vdp2Regs->MPABRA = Vdp2Regs->MPCDRA = Vdp2Regs->MPEFRA = Vdp2Regs->MPGHRA = 0x0505;
   Vdp2Regs->MPIJRA = Vdp2Regs->MPKLRA = Vdp2Regs->MPMNRA = Vdp2Regs->MPOPRA = 0x0505;
   Vdp2Regs->RPTA.all = 0x38000;
   T1WriteLong(Vdp2Ram, 0x70010, 0x10000); /* deltaYst */
   T1WriteLong(Vdp2Ram, 0x70014, 0x10000); /* deltaX */
   T1WriteLong(Vdp2Ram, 0x7001C, 0x10000); /* A */
   T1WriteLong(Vdp2Ram, 0x7002C, 0x10000); /* E */
   T1WriteLong(Vdp2Ram, 0x7004C, 0x10000); /* kx */
   T1WriteLong(Vdp2Ram, 0x70050, 0x10000); /* ky */
   for (i = 0; i < 32; i++) T1WriteByte(Vdp2Ram, 0x1000 + i, 0x11);
   T1WriteByte(Vdp2Ram, 0x1000, 0); /* Transparent pixels in every tile. */
   for (layer = 0; layer < 5; layer++)
   {
      T2WriteWord(Vdp2ColorRam, (layer * 16 + 1) * 2, colors[layer]);
      for (i = 0; i < 4096; i++)
         T1WriteWord(Vdp2Ram, (layer + 1) * 0x2000 + i * 2, 0x80 | (layer << 12));
   }
   Vdp2Internal.ColorMode = 0;
   for (i = 0; i < 270; i++) Vdp2Lines[i] = *Vdp2Regs;
}

static void setup_rbg1_scene(int with_rbg0)
{
   int i;
   setup_layer_scene();
   Vdp2Regs->BGON = 0x20 | (with_rbg0 ? 0x10 : 0);
   /* RBG1 uses parameter B and the NBG0 character/palette configuration. */
   memcpy(Vdp2Ram + 0x70080, Vdp2Ram + 0x70000, 0x80);
   Vdp2Regs->MPABRB = Vdp2Regs->MPCDRB = Vdp2Regs->MPEFRB = Vdp2Regs->MPGHRB = 0x0101;
   Vdp2Regs->MPIJRB = Vdp2Regs->MPKLRB = Vdp2Regs->MPMNRB = Vdp2Regs->MPOPRB = 0x0101;
   for (i = 0; i < 270; i++) Vdp2Lines[i] = *Vdp2Regs;
}
