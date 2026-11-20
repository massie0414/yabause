/* This file is part of Yabause, licensed under GPL-2.0-or-later. */
#include "UIVdpPaletteViewer.h"
#include "../YabauseThread.h"
extern "C" {
#include "../../vdp1debug.h"
#include "../../vdp2vram.h"
}
#include <QCheckBox>
#include <QComboBox>
#include <QFileDialog>
#include <QHeaderView>
#include <QHideEvent>
#include <QLabel>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QScrollBar>
#include <QSet>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

static QString hexValue(u32 value, int digits = 3)
{
    return QString::fromLatin1("0x") + QString::number(value, 16).rightJustified(digits, '0').toUpper();
}

static QString sourceKey(const VdpPaletteSource &s)
{
    return QString::fromLatin1("%1/%2/%3").arg(s.type).arg(s.base).arg(s.count);
}

static bool layerEnabled(const Vdp2 &r, int layer)
{
    if (layer == 0 && (r.BGON & 0x20)) return false; // RBG1 replaces NBG0.
    if (layer == 4 || layer == 5)
        return (r.BGON & 0x10) && ((r.RPMD & 3) >= 2 || (r.RPMD & 3) == layer - 4);
    return (r.BGON & (1 << (layer == 6 ? 5 : layer))) != 0;
}

UIVdpPaletteViewer::UIVdpPaletteViewer(YabauseThread *emulation, bool vdp1, QWidget *parent)
    : QDialog(parent, Qt::Window), mEmulation(emulation), mVdp1(vdp1), mValid(false),
      mFirstCapture(true), mWalkStatus(VDP1_DEBUG_END), mRegs(), mTimer(new QTimer(this))
{
    setObjectName(vdp1 ? "vdp1PaletteViewer" : "vdp2PaletteViewer");
    setWindowTitle(vdp1 ? tr("VDP1 Palette Viewer") : tr("VDP2 Palette Viewer"));
    resize(760, 650);
    QVBoxLayout *layout = new QVBoxLayout(this);
    QLabel *intro = new QLabel(vdp1 ?
        tr("Palettes referenced by the current VDP1 command list. Bank colors use shared VDP2 color RAM; "
           "LUTs can also contain direct RGB colors. RGB sprites do not use a palette.") :
        tr("Palettes referenced by each VDP2 layer's bitmap or tile maps. "
           "Color RAM is shared with VDP1. Tile maps include offscreen tiles."), this);
    intro->setWordWrap(true);
    layout->addWidget(intro);
    QHBoxLayout *sources = new QHBoxLayout;
    sources->addWidget(new QLabel(tr("Source"), this));
    mSource = new QComboBox(this);
    mSource->setObjectName("paletteSource");
    mSource->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    mSource->setMinimumContentsLength(28);
    sources->addWidget(mSource, 1);
    mBank = new QComboBox(this);
    mBank->setObjectName("paletteBank");
    mBank->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    mBank->setMinimumContentsLength(12);
    mBank->setToolTip(tr("Palette base in CRAM entries, including the layer's color RAM offset."));
    sources->addWidget(mBank);
    mBank->setVisible(!vdp1);
    layout->addLayout(sources);
    mStatus = new QLabel(this);
    mStatus->setObjectName("paletteStatus");
    mStatus->setWordWrap(true);
    layout->addWidget(mStatus);
    mGrid = new QTableWidget(0, 16, this);
    mGrid->setObjectName("paletteGrid");
    mGrid->setEditTriggers(QAbstractItemView::NoEditTriggers);
    mGrid->setSelectionMode(QAbstractItemView::SingleSelection);
    mGrid->setSelectionBehavior(QAbstractItemView::SelectItems);
    mGrid->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    mGrid->verticalHeader()->setDefaultSectionSize(30);
    mGrid->setMinimumHeight(200);
    QStringList headers;
    for (int i = 0; i < 16; ++i) headers << QString::number(i, 16).toUpper();
    mGrid->setHorizontalHeaderLabels(headers);
    layout->addWidget(mGrid, 1);
    mDetails = new QLabel(this);
    mDetails->setObjectName("paletteDetails");
    mDetails->setWordWrap(true);
    mDetails->setMinimumHeight(65);
    mDetails->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(mDetails);
    QLabel *note = new QLabel(tr("Select a color for its index, RGB value and memory address. "
        "Swatches show stored colors before transparency, color offsets, shading and blending."), this);
    note->setWordWrap(true);
    layout->addWidget(note);
    QHBoxLayout *controls = new QHBoxLayout;
    mAutoRefresh = new QCheckBox(tr("Auto refresh"), this);
    mAutoRefresh->setObjectName("paletteAutoRefresh");
    mAutoRefresh->setChecked(true);
    controls->addWidget(mAutoRefresh);
    QPushButton *refreshButton = new QPushButton(tr("Refresh"), this);
    refreshButton->setObjectName("paletteRefresh");
    controls->addWidget(refreshButton);
    controls->addStretch();
    mSave = new QPushButton(tr("Save PNG..."), this);
    mSave->setObjectName("paletteSave");
    controls->addWidget(mSave);
    layout->addLayout(controls);
    mTimer->setSingleShot(true);
    connect(mSource, SIGNAL(currentIndexChanged(int)), this, SLOT(selectSource()));
    connect(mBank, SIGNAL(currentIndexChanged(int)), this, SLOT(redraw()));
    connect(mGrid, SIGNAL(itemSelectionChanged()), this, SLOT(selectColor()));
    connect(mAutoRefresh, SIGNAL(toggled(bool)), this, SLOT(updateTimer()));
    connect(mTimer, SIGNAL(timeout()), this, SLOT(refresh()));
    connect(refreshButton, SIGNAL(clicked()), this, SLOT(refresh()));
    connect(mSave, SIGNAL(clicked()), this, SLOT(saveImage()));
    setSnapshot(NULL, NULL, NULL);
}

void UIVdpPaletteViewer::setSnapshot(const Vdp2 *regs, const u8 *ram, const u8 *cram)
{
    const QString selected = mSource->currentData().toString();
    mValid = regs && ram && cram;
    if (mValid)
    {
        mRegs = *regs;
        mRam.resize(0x80000);
        mCram.resize(0x1000);
        memcpy(mRam.data(), ram, mRam.size());
        memcpy(mCram.data(), cram, mCram.size());
    }
    else { mRam.clear(); mCram.clear(); }
    {
        QSignalBlocker block(mSource);
        mSource->clear();
        mSources.clear();
        mSource->addItem(tr("All CRAM (shared)"), "cram");
        VdpPaletteSource all = {VDP_PALETTE_CRAM, ((mRegs.RAMCTL >> 12) & 3) == 2 ? 1024 : 2048, 0, 0};
        mSources.append(all);
        if (mValid && mVdp1)
        {
            QVector<VdpPaletteSource> commands(VDP1_DEBUG_MAX_COMMANDS);
            int count = Vdp1PaletteCollect(mRam.constData(), commands.data(), commands.size(), &mWalkStatus);
            QSet<QString> seen;
            for (int i = 0; i < count; ++i)
            {
                const VdpPaletteSource &s = commands[i];
                const QString key = sourceKey(s);
                if (seen.contains(key)) continue;
                seen.insert(key);
                QString label = s.type == VDP_PALETTE_LUT ? tr("LUT at VRAM %1 (16 colors)").arg(hexValue(s.base, 5)) :
                    s.type == VDP_PALETTE_FLAT ? tr("Flat color %1").arg(hexValue(s.base, 4)) :
                    tr("Bank %1 (%2 colors)").arg(hexValue(s.base, 4)).arg(s.count);
                mSource->addItem(label, key);
                mSources.append(s);
            }
        }
        else if (!mVdp1)
        {
            const char *names[] = {"NBG0", "NBG1", "NBG2", "NBG3", "RBG0 A", "RBG0 B", "RBG1"};
            for (int i = 0; i < 7; ++i)
                mSource->addItem(QString::fromLatin1(names[i]) +
                    (mValid && !layerEnabled(mRegs, i) ? tr(" (inactive)") : QString()), QString::number(i));
        }
        int index = mSource->findData(selected);
        if (mValid && mFirstCapture)
        {
            mFirstCapture = false;
            if (mVdp1 && mSources.size() > 1) index = 1;
            else if (!mVdp1)
                for (int i = 0; i < 7; ++i)
                    if (layerEnabled(mRegs, i)) { index = i + 1; break; }
        }
        mSource->setCurrentIndex(qMax(0, index));
    }
    selectSource();
}

void UIVdpPaletteViewer::selectSource()
{
    if (!mVdp1)
    {
        const QVariant previous = mBank->currentData();
        QSignalBlocker block(mBank);
        mBank->clear();
        if (mValid && mSource->currentIndex() > 0)
        {
            u32 bases[128];
            int count = Vdp2PaletteCollect(&mRegs, mRam.constData(), mSource->currentIndex() - 1, bases, 128);
            for (int i = 0; i < count; ++i)
                mBank->addItem(tr("CRAM %1").arg(hexValue(bases[i])), bases[i]);
            mBank->setCurrentIndex(qMax(0, mBank->findData(previous)));
        }
        mBank->setEnabled(mBank->count() > 1);
    }
    redraw();
}

void UIVdpPaletteViewer::redraw()
{
    const int oldIndex = mGrid->currentRow() * 16 + mGrid->currentColumn();
    const int scroll = mGrid->verticalScrollBar()->value();
    QSignalBlocker block(mGrid);
    mGrid->clearContents();
    mGrid->setRowCount(0);
    mImage = QImage();
    mSave->setEnabled(false);
    mDetails->clear();
    if (!mValid) { mStatus->setText(tr("Start emulation to view palettes.")); return; }
    const int mode = (mRegs.RAMCTL >> 12) & 3;
    if (mode == 3) { mStatus->setText(tr("Reserved CRAM mode (3); palette colors are unavailable.")); return; }
    VdpPaletteSource source = mSources[0];
    QString info;
    if (mVdp1 && mSource->currentIndex() > 0)
    {
        source = mSources[mSource->currentIndex()];
        info = tr("First command: %1 | SPCTL: %2 | CRAM offset: %3\n")
            .arg(hexValue(source.command, 5)).arg(hexValue(mRegs.SPCTL, 4)).arg(hexValue((mRegs.CRAOFB & 0x70) << 4));
    }
    else if (!mVdp1 && mSource->currentIndex() > 0)
    {
        const int layer = mSource->currentIndex() - 1;
        Vdp2VramConfig c;
        if (Vdp2VramLayerConfig(&mRegs, layer, &c))
        { mStatus->setText(tr("Reserved layer color format; no palette available.")); return; }
        info = layerEnabled(mRegs, layer) ? tr("Enabled in captured registers. ") : tr("Inactive layer settings. ");
        if (c.color >= 3)
        { mStatus->setText(info + tr("Direct RGB%1; this layer does not use a palette.").arg(c.color == 3 ? 555 : 888)); return; }
        source.count = c.color == 0 ? 16 : c.color == 1 ? 256 : 2048;
        source.base = mBank->currentData().toUInt();
        info += (!c.tile_size ? tr("Bitmap palette.") : tr("%1 palettes referenced by the configured maps.").arg(mBank->count())) + "\n";
    }
    info += tr("CRAM mode %1 (%2) | %3 colors").arg(mode).arg(mode == 2 ? "RGB888" : "RGB555").arg(source.count);
    if (mVdp1 && mWalkStatus != VDP1_DEBUG_END)
        info += mWalkStatus == VDP1_DEBUG_LIMIT ? tr(" | Command limit reached; partial list.") : tr(" | Invalid command; partial list.");
    mStatus->setText(info);
    mGrid->setRowCount((source.count + 15) / 16);
    mImage = QImage(16 * 24, mGrid->rowCount() * 24, QImage::Format_ARGB32);
    mImage.fill(Qt::transparent);
    QPainter painter(&mImage);
    for (int row = 0; row < mGrid->rowCount(); ++row)
        mGrid->setVerticalHeaderItem(row, new QTableWidgetItem(hexValue(row * 16)));
    for (int i = 0; i < source.count; ++i)
    {
        VdpPaletteColor color;
        int result = mVdp1 ? Vdp1PaletteReadColor(mRam.constData(), mCram.constData(), &mRegs, &source, i, &color) :
            VdpPaletteReadColor(mCram.constData(), mode, source.base + i, &color);
        if (result) continue;
        const QColor rgb = QColor::fromRgba(color.argb);
        QString text = tr("Palette index %1 | RGB %2, %3, %4 | #%5\n")
            .arg(hexValue(i)).arg(rgb.red()).arg(rgb.green()).arg(rgb.blue())
            .arg(QString::number(color.argb & 0xffffff, 16).rightJustified(6, '0').toUpper());
        if (color.cram_index >= 0)
            text += tr("CRAM entry %1 | Offset %2 | SH2 %3 | Raw %4")
                .arg(hexValue(color.cram_index)).arg(hexValue(color.address))
                .arg(hexValue(0x25f00000 + color.address, 8)).arg(hexValue(color.raw, mode == 2 ? 8 : 4));
        else text += tr("Direct RGB555 | VDP1 VRAM %1 | Raw %2")
                .arg(hexValue(color.address, 5)).arg(hexValue(color.raw, 4));
        if (source.type == VDP_PALETTE_LUT)
        {
            u32 address = (source.base + i * 2) & 0x7ffff;
            u16 raw = (mRam[address] << 8) | mRam[(address + 1) & 0x7ffff];
            text += tr("\nLUT entry: VDP1 VRAM %1 | SH2 %2 | Raw %3")
                .arg(hexValue(address, 5)).arg(hexValue(0x25c00000 + address, 8)).arg(hexValue(raw, 4));
        }
        QTableWidgetItem *item = new QTableWidgetItem(QString::number(i, 16).rightJustified(3, '0').toUpper());
        item->setTextAlignment(Qt::AlignCenter);
        item->setBackground(rgb);
        item->setForeground((rgb.red() * 299 + rgb.green() * 587 + rgb.blue() * 114) > 140000 ? Qt::black : Qt::white);
        item->setToolTip(text);
        mGrid->setItem(i / 16, i % 16, item);
        painter.fillRect((i % 16) * 24, (i / 16) * 24, 24, 24, rgb);
    }
    painter.end();
    const int selected = oldIndex >= 0 && oldIndex < source.count ? oldIndex : 0;
    mGrid->setCurrentCell(selected / 16, selected % 16);
    mGrid->verticalScrollBar()->setValue(scroll);
    selectColor();
    mSave->setEnabled(!mImage.isNull());
}

void UIVdpPaletteViewer::selectColor()
{
    const QTableWidgetItem *item = mGrid->currentItem();
    mDetails->setText(item ? item->toolTip() : QString());
}

void UIVdpPaletteViewer::refresh()
{
    if (!isVisible()) return;
    if (mEmulation)
    {
        // Frames and timers run on the GUI thread. Capture between frames.
        const u8 *ram = mVdp1 ? Vdp1Ram : Vdp2Ram;
        if (mEmulation->init() == 0 && Vdp2Regs && ram && Vdp2ColorRam)
            setSnapshot(Vdp2Regs, ram, Vdp2ColorRam);
        else setSnapshot(NULL, NULL, NULL);
    }
    updateTimer();
}

void UIVdpPaletteViewer::updateTimer()
{
    mTimer->stop();
    if (isVisible() && mEmulation && mAutoRefresh->isChecked()) mTimer->start(500);
}

void UIVdpPaletteViewer::showEvent(QShowEvent *event)
{
    QDialog::showEvent(event);
    emit visibilityChanged(true);
    refresh();
}

void UIVdpPaletteViewer::hideEvent(QHideEvent *event)
{
    mTimer->stop();
    QDialog::hideEvent(event);
    emit visibilityChanged(false);
}

void UIVdpPaletteViewer::saveImage()
{
    if (mImage.isNull()) return;
    const QImage captured = mImage;
    QString path = QFileDialog::getSaveFileName(this, tr("Save palette"),
        mVdp1 ? "vdp1-palette.png" : "vdp2-palette.png", tr("PNG image (*.png)"));
    if (path.isEmpty()) return;
    if (!path.endsWith(".png", Qt::CaseInsensitive)) path += ".png";
    if (!captured.save(path, "PNG")) QMessageBox::warning(this, tr("Save palette"), tr("Could not save the image."));
}
