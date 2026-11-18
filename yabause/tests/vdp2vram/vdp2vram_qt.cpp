/* This file is part of Yabause, licensed under GPL-2.0-or-later. */
#include "qt/ui/UIVdp2VramViewer.h"
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QFile>
#include <QGraphicsPixmapItem>
#include <QGraphicsView>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QTimer>
#include <cstdio>

// The offline viewer fixture needs no emulation instance or video backend.
extern "C" {
Vdp2 *Vdp2Regs = NULL;
u8 *Vdp2Ram = NULL, *Vdp2ColorRam = NULL;
}
static Vdp2 regs;
static u8 ram[0x80000], cram[0x1000];
static int checks, failures;

static void check(bool condition, const char *message)
{
    ++checks;
    if (!condition) { fprintf(stderr, "FAIL: %s\n", message); ++failures; }
}

static QImage image(UIVdp2VramViewer &viewer)
{
    QGraphicsView *view = viewer.findChild<QGraphicsView *>("vramImage");
    return static_cast<QGraphicsPixmapItem *>(view->scene()->items().first())->pixmap().toImage();
}

static bool loadSnapshot(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return false;
    QByteArray data = file.readAll();
    if (data.size() < 20 || data.left(3) != "YSS" || data.at(3) != 1) return false;
    u32 version;
    memcpy(&version, data.constData() + 4, 4);
    if (version != 1 && version != 2) return false;
    qint64 pos = version == 2 ? 20 : 12;
    while (pos + 12 <= data.size())
    {
        u32 size;
        memcpy(&size, data.constData() + pos + 8, 4);
        if (pos + 12 + size > data.size()) return false;
        if (data.mid(int(pos), 4) == "VDP2")
        {
            if (size < sizeof(regs) + sizeof(ram) + sizeof(cram)) return false;
            const char *source = data.constData() + pos + 12;
            memcpy(&regs, source, sizeof(regs));
            memcpy(ram, source + sizeof(regs), sizeof(ram));
            memcpy(cram, source + sizeof(regs) + sizeof(ram), sizeof(cram));
            return true;
        }
        pos += 12 + size;
    }
    return false;
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    UIVdp2VramViewer viewer(NULL);
    const QStringList args = app.arguments();
    if (args.contains("--snapshot"))
    {
        int index = args.indexOf("--snapshot");
        if (index + 1 >= args.size() || !loadSnapshot(args[index + 1])) return 2;
        viewer.setSnapshot(&regs, ram, cram);
        viewer.findChild<QCheckBox *>("vramAutoRefresh")->setChecked(false);
        viewer.setWindowTitle("VDP2 VRAM Viewer - saved snapshot");
        viewer.show();
        return app.exec();
    }
    viewer.show();
    QApplication::processEvents();
    check(image(viewer).isNull(), "empty viewer before emulation");
    check(!viewer.findChild<QPushButton *>("vramSave")->isEnabled(), "no export before capture");
    regs.RAMCTL = 0x1000;
    ram[0] = 0x12;
    T2WriteWord(cram, 2, 0x001f);
    T2WriteWord(cram, 4, 0x03e0);
    viewer.setSnapshot(&regs, ram, cram);
    QImage first = image(viewer);
    check(first.size() == QSize(256, 4096) && first.pixel(0, 0) == 0xfff80000 &&
        first.pixel(1, 0) == 0xff00f800, "Qt displays decoded native ARGB pixels");
    viewer.findChild<QCheckBox *>("vramAutoRefresh")->setChecked(false);
    memset(ram, 0, sizeof(ram));
    viewer.findChild<QComboBox *>("vramZoom")->setCurrentIndex(3);
    check(viewer.findChild<QGraphicsView *>("vramImage")->transform().m11() == 4, "4x pixel zoom");
    viewer.findChild<QCheckBox *>("vramGrayscale")->setChecked(true);
    check(image(viewer).pixel(0, 0) == 0xff111111, "redecode uses copied VRAM after live memory changes");
    viewer.findChild<QCheckBox *>("vramGrayscale")->setChecked(false);
    check(image(viewer) == first, "palette preserved in frozen snapshot");
    viewer.findChild<QSpinBox *>("vramPalette")->setValue(1);
    check(image(viewer).pixel(0, 0) == 0xff00f800, "palette control changes interpretation");
    viewer.findChild<QComboBox *>("vramLayout")->setCurrentIndex(2);
    viewer.findChild<QComboBox *>("vramWidth")->setCurrentIndex(2);
    check(image(viewer).size() == QSize(512, 256), "non-square bitmap dimensions");
    check(!viewer.findChild<QComboBox *>("vramBank")->isEnabled(), "bitmap size takes precedence over tile range");
    check(image(viewer).save("vram-qt-roundtrip.png", "PNG"), "PNG write succeeds");
    check(QImage("vram-qt-roundtrip.png") == image(viewer), "PNG preserves dimensions and pixels");
    regs.CHCTLA = 0x123f;
    regs.MPOFN = 0x21;
    viewer.setSnapshot(&regs, ram, cram);
    viewer.findChild<QComboBox *>("vramLayer")->setCurrentIndex(1);
    viewer.findChild<QPushButton *>("vramApplyLayer")->click();
    check(viewer.findChild<QComboBox *>("vramColor")->currentIndex() == 1 &&
        viewer.findChild<QSpinBox *>("vramAddress")->value() == 0x40000 &&
        image(viewer).size() == QSize(512, 256), "layer button applies snapshot registers");
    viewer.findChild<QComboBox *>("vramLayout")->setCurrentIndex(0);
    viewer.findChild<QComboBox *>("vramBank")->setCurrentIndex(4);
    check(viewer.findChild<QSpinBox *>("vramAddress")->value() == 0x60000, "bank selection jumps to its address");
    viewer.findChild<QSpinBox *>("vramAddress")->setValue(0x3ffe0);
    check(viewer.findChild<QComboBox *>("vramBank")->currentIndex() == 2 &&
        image(viewer).height() == 8, "manual address follows bank and stops at the bank end");
    viewer.setSnapshot(NULL, NULL, NULL);
    check(image(viewer).isNull() && !viewer.findChild<QPushButton *>("vramSave")->isEnabled(), "unavailable state clears stale pixels");
    viewer.hide();
    check(!viewer.findChild<QTimer *>()->isActive(), "hidden viewer stops refresh timer");
    printf("VDP2 VRAM Qt: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
