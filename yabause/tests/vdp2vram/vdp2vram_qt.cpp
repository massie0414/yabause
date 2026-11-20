/* This file is part of Yabause, licensed under GPL-2.0-or-later. */
#include "qt/ui/UIVdp2VramViewer.h"
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QFile>
#include <QGraphicsPixmapItem>
#include <QGraphicsView>
#include <QLabel>
#include <QMouseEvent>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
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

static QImage image(UIVdp2VramViewer &viewer, const char *name = "vramImage")
{
    QGraphicsView *view = viewer.findChild<QGraphicsView *>(name);
    foreach (QGraphicsItem *item, view->scene()->items())
        if (item->type() == QGraphicsPixmapItem::Type)
            return static_cast<QGraphicsPixmapItem *>(item)->pixmap().toImage();
    return QImage();
}

static void cellFixture()
{
    memset(&regs, 0, sizeof(regs));
    memset(ram, 0, sizeof(ram));
    memset(cram, 0, sizeof(cram));
    regs.BGON = 1;
    regs.RAMCTL = 0x1000;
    regs.CHCTLA = 1;
    regs.MPABN0 = 0x4140;
    regs.MPCDN0 = 0x4342;
    for (int i = 0; i < 4; ++i) memset(ram + 0x1000 + i * 32, (i + 1) * 0x11, 32);
    const u16 colors[] = { 0x001f, 0x03e0, 0x7c00, 0x7fff };
    for (int i = 0; i < 4; ++i)
    {
        T2WriteWord(cram, (i + 1) * 2, colors[i]);
        T2WriteWord(cram, (i + 17) * 2, colors[3 - i]);
    }
    for (int plane = 0; plane < 4; ++plane)
        for (int y = 0; y < 32; ++y)
            for (int x = 0; x < 32; ++x)
            {
                u32 address = 0x40000 + plane * 0x1000 + (y * 32 + x) * 4;
                T1WriteWord(ram, address, ((x & 3) << 14) | (((x / 4 + y / 4 + plane) & 1) ? 1 : 0));
                T1WriteWord(ram, address + 2, 0x80);
            }
}

static void cellTests(UIVdp2VramViewer &viewer)
{
    cellFixture();
    viewer.findChild<QComboBox *>("vramLayer")->setCurrentIndex(0);
    viewer.setSnapshot(&regs, ram, cram);
    viewer.findChild<QTabWidget *>("vramTabs")->setCurrentIndex(1);
    QApplication::processEvents();
    QImage map = image(viewer, "cellMap"), tiles = image(viewer, "cellTiles");
    check(map.size() == QSize(1024, 1024), "whole normal map includes four planes");
    check(tiles.size() == QSize(32, 16), "same image and palette deduplicated across flips and planes");
    check(map.pixel(0, 0) == 0xfff80000 && map.pixel(16, 0) == 0xff00f800 &&
        map.pixel(32, 0) == 0xff0000f8 && map.pixel(48, 0) == 0xfff8f8f8,
        "map applies H, V and HV flips across all four cells of a 16x16 pattern");
    check(tiles.pixel(0, 0) == 0xfff80000 && tiles.pixel(8, 0) == 0xff00f800 &&
        tiles.pixel(0, 8) == 0xff0000f8 && tiles.pixel(16, 0) == 0xfff8f8f8,
        "tile atlas keeps original orientation and distinguishes map palettes");
    check(viewer.findChild<QComboBox *>("cellPlane")->count() == 5 &&
        viewer.findChild<QComboBox *>("cellPlane")->itemText(2).contains("0X41000"),
        "plane chooser shows decoded addresses");
    check(viewer.findChild<QGraphicsView *>("cellTiles")->transform().m11() == 4, "tile atlas starts at 4x zoom");
    check(map.save("cell-map-roundtrip.png", "PNG") && QImage("cell-map-roundtrip.png") == map,
        "cell map PNG preserves native pixels without grid overlay");
    viewer.findChild<QComboBox *>("cellPlane")->setCurrentIndex(2);
    check(image(viewer, "cellMap").size() == QSize(512, 512) &&
        image(viewer, "cellMap").pixel(0, 0) == 0xfff8f8f8, "selected plane shows correct map origin");
    viewer.findChild<QComboBox *>("cellMapZoom")->setCurrentIndex(1);
    QApplication::processEvents();
    QGraphicsView *view = viewer.findChild<QGraphicsView *>("cellMap");
    const QPoint point = view->mapFromScene(QPointF(0.5, 0.5));
    QMouseEvent move(QEvent::MouseMove, QPointF(point), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(view->viewport(), &move);
    QString hover = viewer.findChild<QLabel *>("cellHover")->text();
    check(hover.contains("Cell (32, 0)") && hover.contains("Plane B") && hover.contains("0X41000") &&
        hover.contains("0X01000") && hover.contains("0X010"), "map hover uses global cell coordinates and actual entry attributes");
    QMouseEvent click(QEvent::MouseButtonPress, QPointF(point), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(view->viewport(), &click);
    bool selection = false;
    foreach (QGraphicsItem *item, viewer.findChild<QGraphicsView *>("cellTiles")->scene()->items())
        if (item->type() == QGraphicsRectItem::Type) selection = item->isVisible();
    check(selection, "clicking a map cell highlights its original tile");
    memset(ram, 0, sizeof(ram)); memset(cram, 0, sizeof(cram)); regs.CHCTLA = 2;
    viewer.findChild<QComboBox *>("cellPlane")->setCurrentIndex(0);
    check(image(viewer, "cellMap") == map && image(viewer, "cellTiles") == tiles,
        "changing map controls uses copied registers, VRAM and CRAM");
    viewer.findChild<QComboBox *>("cellMapZoom")->setCurrentIndex(0);
    QApplication::processEvents();
    viewer.grab().save("cell-map-qt.png");
    viewer.setSnapshot(&regs, ram, cram);
    check(image(viewer, "cellMap").isNull() && image(viewer, "cellTiles").isNull() &&
        viewer.findChild<QLabel *>("cellInfo")->text().contains("bitmap mode") &&
        !viewer.findChild<QPushButton *>("cellSaveMap")->isEnabled(), "bitmap transition clears map and tile images");
    cellFixture();
    regs.BGON = 0;
    viewer.setSnapshot(&regs, ram, cram);
    check(!image(viewer, "cellMap").isNull() && viewer.findChild<QLabel *>("cellInfo")->text().contains("Disabled"),
        "disabled layer remains inspectable and is labelled");
    viewer.findChild<QComboBox *>("vramLayer")->setCurrentIndex(5);
    check(viewer.findChild<QComboBox *>("cellPlane")->count() == 17 && image(viewer, "cellMap").size() == QSize(2048, 2048),
        "layer selection directly updates the cell map and all rotation planes");
    viewer.findChild<QComboBox *>("cellPlane")->setCurrentIndex(16);
    viewer.findChild<QComboBox *>("vramLayer")->setCurrentIndex(0);
    check(viewer.findChild<QComboBox *>("cellPlane")->currentIndex() <= 4 &&
        !image(viewer, "cellMap").isNull(), "switching from rotation to normal clamps selected plane");
    memset(ram, 0, sizeof(ram));
    regs.CHCTLB = 0;
    regs.PLSZ = 0x0300;
    regs.PNCR = 0x8000;
    viewer.setSnapshot(&regs, ram, cram);
    viewer.findChild<QComboBox *>("vramLayer")->setCurrentIndex(4);
    viewer.findChild<QComboBox *>("cellPlane")->setCurrentIndex(0);
    check(image(viewer, "cellMap").size() == QSize(4096, 4096) &&
        image(viewer, "cellTiles").size() == QSize(8, 8) && image(viewer, "cellMap").pixel(4095, 4095) == 0,
        "largest rotation map covers 262144 cells and transparent last pixel");
    viewer.findChild<QCheckBox *>("cellTransparency")->setChecked(false);
    check(image(viewer, "cellMap").pixel(4095, 4095) == 0xff000000,
        "cell transparency override reveals stored color zero");
    regs.RAMCTL = 0x3000;
    viewer.setSnapshot(&regs, ram, cram);
    check(image(viewer, "cellMap").isNull() && image(viewer, "cellTiles").isNull(), "invalid CRAM clears both cell images");
    viewer.setSnapshot(NULL, NULL, NULL);
    check(image(viewer, "cellMap").isNull() && !viewer.findChild<QPushButton *>("cellSaveTiles")->isEnabled(),
        "lost snapshot clears cell view and disables export");
    viewer.findChild<QTabWidget *>("vramTabs")->setCurrentIndex(0);
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
    cellTests(viewer);
    viewer.setSnapshot(NULL, NULL, NULL);
    check(image(viewer).isNull() && !viewer.findChild<QPushButton *>("vramSave")->isEnabled(), "unavailable state clears stale pixels");
    viewer.hide();
    check(!viewer.findChild<QTimer *>()->isActive(), "hidden viewer stops refresh timer");
    printf("VDP2 VRAM Qt: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
