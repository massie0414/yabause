/* This file is part of Yabause, licensed under GPL-2.0-or-later. */
#include "qt/ui/UIVdpPaletteViewer.h"
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QTimer>
#include <cstdio>

extern "C" {
Vdp2 *Vdp2Regs = NULL;
u8 *Vdp1Ram = NULL, *Vdp2Ram = NULL, *Vdp2ColorRam = NULL;
}
static Vdp2 regs;
static u8 vdp1[0x80000], vdp2[0x80000], cram[0x1000];
static int checks, failures;
static void check(bool condition, const char *message)
{
    ++checks;
    if (!condition) { fprintf(stderr, "FAIL: %s\n", message); ++failures; }
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    UIVdpPaletteViewer first(NULL, true), second(NULL, false);
    QTableWidget *grid1 = first.findChild<QTableWidget *>("paletteGrid");
    QTableWidget *grid2 = second.findChild<QTableWidget *>("paletteGrid");
    QComboBox *source1 = first.findChild<QComboBox *>("paletteSource");
    QComboBox *source2 = second.findChild<QComboBox *>("paletteSource");
    QComboBox *bank = second.findChild<QComboBox *>("paletteBank");
    first.show(); second.show();
    QApplication::processEvents();
    check(grid1->rowCount() == 0 && grid2->rowCount() == 0, "empty before capture");
    check(!first.findChild<QPushButton *>("paletteSave")->isEnabled(), "empty capture cannot export");
    for (int i = 0; i < 2048; ++i)
        T2WriteWord(cram, i * 2, (i & 31) | (((i >> 4) & 31) << 5) | (((i >> 8) & 7) << 12));
    regs.RAMCTL = 0x1000;
    regs.SPCTL = 0x20;
    regs.CRAOFB = 0x10;
    regs.BGON = 3;
    regs.CHCTLA = 0x1212; // two 256-color bitmaps
    regs.BMPNA = 0x0201;
    T1WriteWord(vdp1, 4, 8); // LUT
    T1WriteWord(vdp1, 6, 0x20); // VRAM 0x100
    T1WriteWord(vdp1, 10, 0x101);
    T1WriteWord(vdp1, 32, 0); // duplicate LUT
    T1WriteWord(vdp1, 36, 8);
    T1WriteWord(vdp1, 38, 0x20);
    T1WriteWord(vdp1, 42, 0x101);
    T1WriteWord(vdp1, 64, 0x8000);
    T1WriteWord(vdp1, 0x100, 0x801f);
    T1WriteWord(vdp1, 0x102, 0x0020);
    first.setSnapshot(&regs, vdp1, cram);
    second.setSnapshot(&regs, vdp2, cram);
    check(source1->count() == 2 && source1->currentIndex() == 1, "duplicate LUTs grouped and first palette selected");
    check(grid1->rowCount() == 1 && grid1->item(0, 0)->background().color().rgba() == 0xfff80000, "LUT direct RGB swatch");
    check(grid1->item(0, 1)->toolTip().contains("0x120") && grid1->item(0, 1)->toolTip().contains("0x00102"), "LUT has resolved CRAM and source VRAM addresses");
    check(source2->currentIndex() == 1 && grid2->rowCount() == 16 && bank->currentData().toUInt() == 0x100, "VDP2 first active bitmap palette");
    source2->setCurrentIndex(2);
    check(bank->currentData().toUInt() == 0x200, "VDP2 NBG1 has independent palette");
    first.findChild<QCheckBox *>("paletteAutoRefresh")->setChecked(false);
    second.findChild<QCheckBox *>("paletteAutoRefresh")->setChecked(false);
    T1WriteWord(vdp1, 0x100, 0x83e0);
    T2WriteWord(cram, 0x400, 0x001f);
    source1->setCurrentIndex(0); source1->setCurrentIndex(1);
    source2->setCurrentIndex(0); source2->setCurrentIndex(2);
    check(grid1->item(0, 0)->background().color().rgba() == 0xfff80000, "VDP1 frozen LUT survives source switching");
    check(grid2->item(0, 0)->background().color().rgba() != 0xfff80000, "VDP2 frozen CRAM survives source switching");
    grid2->setCurrentCell(3, 5);
    second.setSnapshot(&regs, vdp2, cram);
    check(source2->currentIndex() == 2 && grid2->currentRow() == 3 && grid2->currentColumn() == 5, "refresh preserves source and selected color");
    first.setSnapshot(&regs, vdp1, cram);
    check(grid1->item(0, 0)->background().color().rgba() == 0xff00f800, "new capture updates LUT");
    regs.CHCTLA = 0x3032; // direct RGB for NBG0 and NBG1
    second.setSnapshot(&regs, vdp2, cram);
    check(grid2->rowCount() == 0 && second.findChild<QLabel *>("paletteStatus")->text().contains("Direct RGB555"), "RGB layer clearly reports no palette");
    regs.RAMCTL = 0x2000;
    T2WriteLong(cram, 0, 0x80ab5634);
    second.setSnapshot(&regs, vdp2, cram);
    source2->setCurrentIndex(0);
    check(grid2->rowCount() == 64 && grid2->item(0, 0)->background().color().rgba() == 0xff3456ab, "32-bit CRAM uses 1024 swatches and correct byte order");
    regs.RAMCTL = 0x3000;
    second.setSnapshot(&regs, vdp2, cram);
    check(grid2->rowCount() == 0 && !second.findChild<QPushButton *>("paletteSave")->isEnabled(), "reserved CRAM mode clears pixels and export");
    first.setSnapshot(NULL, NULL, NULL);
    check(grid1->rowCount() == 0 && first.findChild<QLabel *>("paletteDetails")->text().isEmpty(), "unavailable capture clears stale color details");
    if (!app.arguments().contains("--test"))
    {
        regs.RAMCTL = 0x1000;
        regs.CHCTLA = 0x1212;
        first.setSnapshot(&regs, vdp1, cram);
        second.setSnapshot(&regs, vdp2, cram);
        source2->setCurrentIndex(1);
        first.move(40, 60); second.move(830, 60);
        return app.exec();
    }
    first.hide(); second.hide();
    check(!first.findChild<QTimer *>()->isActive() && !second.findChild<QTimer *>()->isActive(), "hidden viewers stop refresh");
    printf("VDP palette Qt: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
