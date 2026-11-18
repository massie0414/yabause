/* This file is part of Yabause, licensed under GPL-2.0-or-later. */
#include "UIVdp2VramViewer.h"
#include "../YabauseThread.h"

#include <QCheckBox>
#include <QComboBox>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QGraphicsPixmapItem>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QGridLayout>
#include <QHideEvent>
#include <QLabel>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QShowEvent>
#include <QSpinBox>
#include <QTimer>

static QString hexAddress(u32 address, int digits = 5)
{
    return QString("0x%1").arg(address, digits, 16, QLatin1Char('0')).toUpper();
}

UIVdp2VramViewer::UIVdp2VramViewer(YabauseThread *emulation, QWidget *parent)
    : QDialog(parent, Qt::Window), mEmulation(emulation), mValid(false),
      mUpdating(false), mFirstCapture(true)
{
    setObjectName("vdp2VramViewer");
    setWindowTitle(tr("VDP2 VRAM Viewer"));
    resize(1000, 740);
    QVBoxLayout *layout = new QVBoxLayout(this);
    QLabel *description = new QLabel(tr("Browse image data in the 512 KiB VDP2 VRAM. Tiles are arranged in memory order; bitmap mode shows a continuous image."), this);
    description->setWordWrap(true);
    layout->addWidget(description);

    QHBoxLayout *presets = new QHBoxLayout;
    presets->addWidget(new QLabel(tr("Layer preset:"), this));
    mLayer = new QComboBox(this);
    mLayer->setObjectName("vramLayer");
    mLayer->addItems(QStringList() << "NBG0" << "NBG1" << "NBG2" << "NBG3"
        << "RBG0 (A)" << "RBG0 (B)" << "RBG1");
    presets->addWidget(mLayer);
    QPushButton *apply = new QPushButton(tr("Use layer settings"), this);
    apply->setObjectName("vramApplyLayer");
    presets->addWidget(apply);
    mPresetInfo = new QLabel(this);
    presets->addWidget(mPresetInfo, 1);
    layout->addLayout(presets);

    QGridLayout *controls = new QGridLayout;
    mLayout = new QComboBox(this);
    mLayout->setObjectName("vramLayout");
    mLayout->addItems(QStringList() << tr("8 x 8 tiles") << tr("16 x 16 tiles") << tr("Bitmap"));
    mColor = new QComboBox(this);
    mColor->setObjectName("vramColor");
    mColor->addItems(QStringList() << tr("16 colors (4 bpp)") << tr("256 colors (8 bpp)")
        << tr("2048 colors (16-bit index)") << tr("RGB555 (16 bpp)") << tr("RGB888 (32 bpp)"));
    mBank = new QComboBox(this);
    mBank->setObjectName("vramBank");
    mBank->addItem(tr("All VRAM (512 KiB)"));
    for (int i = 0; i < 4; ++i)
        mBank->addItem(hexAddress(i * 0x20000) + " - " + hexAddress((i + 1) * 0x20000 - 1));
    mAddress = new QSpinBox(this);
    mAddress->setObjectName("vramAddress");
    mAddress->setDisplayIntegerBase(16);
    mAddress->setPrefix("0x");
    mAddress->setRange(0, 0x7ffff);
    mAddress->setSingleStep(32);
    mAddress->setKeyboardTracking(false);
    mPalette = new QSpinBox(this);
    mPalette->setObjectName("vramPalette");
    mPalette->setDisplayIntegerBase(16);
    mPalette->setPrefix("0x");
    mPalette->setRange(0, 0x7ff);
    mPalette->setSingleStep(16);
    mPalette->setKeyboardTracking(false);
    mPalette->setToolTip(tr("CRAM entry added to each pixel index. Tile palettes are selected by the map; adjust this to inspect different palettes."));
    mWidth = new QComboBox(this);
    mWidth->setObjectName("vramWidth");
    mWidth->addItems(QStringList() << "128" << "256" << "512" << "1024");
    mWidth->setCurrentIndex(1);
    mHeight = new QComboBox(this);
    mHeight->setObjectName("vramHeight");
    mHeight->addItems(QStringList() << "256" << "512");
    mZoom = new QComboBox(this);
    mZoom->setObjectName("vramZoom");
    mZoom->addItems(QStringList() << tr("Fit") << "1x" << "2x" << "4x" << "8x");
    mZoom->setCurrentIndex(1);
    const char *labels[] = { "Layout:", "Color format:", "Range:", "Start address:",
        "Palette / CRAM offset:", "Width:", "Bitmap height:", "Zoom:" };
    QWidget *widgets[] = { mLayout, mColor, mBank, mAddress, mPalette, mWidth, mHeight, mZoom };
    for (int i = 0; i < 8; ++i)
    {
        controls->addWidget(new QLabel(tr(labels[i]), this), i / 4 * 2, i % 4);
        controls->addWidget(widgets[i], i / 4 * 2 + 1, i % 4);
        controls->setColumnStretch(i % 4, 1);
    }
    layout->addLayout(controls);

    QHBoxLayout *options = new QHBoxLayout;
    mTransparency = new QCheckBox(tr("Show transparency"), this);
    mTransparency->setObjectName("vramTransparency");
    mGrayscale = new QCheckBox(tr("Grayscale indices"), this);
    mGrayscale->setObjectName("vramGrayscale");
    options->addWidget(mTransparency);
    options->addWidget(mGrayscale);
    options->addStretch();
    layout->addLayout(options);

    mView = new QGraphicsView(this);
    mView->setObjectName("vramImage");
    mView->setScene(new QGraphicsScene(mView));
    mPixmap = mView->scene()->addPixmap(QPixmap());
    mPixmap->setTransformationMode(Qt::FastTransformation);
    QPixmap checker(16, 16);
    checker.fill(QColor(48, 51, 56));
    QPainter painter(&checker);
    painter.fillRect(0, 0, 8, 8, QColor(65, 68, 73));
    painter.fillRect(8, 8, 8, 8, QColor(65, 68, 73));
    painter.end();
    mView->setBackgroundBrush(checker);
    mView->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    mView->setDragMode(QGraphicsView::ScrollHandDrag);
    mView->viewport()->setMouseTracking(true);
    mView->viewport()->installEventFilter(this);
    layout->addWidget(mView, 1);
    mHover = new QLabel(tr("Point at an image pixel to inspect its VRAM address. Drag to pan."), this);
    mHover->setObjectName("vramHover");
    layout->addWidget(mHover);
    QLabel *note = new QLabel(tr("VRAM also contains maps and tables, which may look like noise. Tile presets use the layer's CRAM offset; individual tiles can need different palettes. Checkerboard = transparent."), this);
    note->setWordWrap(true);
    layout->addWidget(note);

    QHBoxLayout *actions = new QHBoxLayout;
    mAutoRefresh = new QCheckBox(tr("Auto refresh"), this);
    mAutoRefresh->setObjectName("vramAutoRefresh");
    mAutoRefresh->setChecked(true);
    actions->addWidget(mAutoRefresh);
    QPushButton *refreshButton = new QPushButton(tr("Refresh"), this);
    refreshButton->setObjectName("vramRefresh");
    actions->addWidget(refreshButton);
    mSave = new QPushButton(tr("Save PNG..."), this);
    mSave->setObjectName("vramSave");
    mSave->setEnabled(false);
    actions->addWidget(mSave);
    mStatus = new QLabel(tr("Start emulation to view VRAM."), this);
    mStatus->setObjectName("vramStatus");
    actions->addWidget(mStatus, 1);
    layout->addLayout(actions);

    mTimer = new QTimer(this);
    mTimer->setSingleShot(true);
    connect(mTimer, SIGNAL(timeout()), this, SLOT(refresh()));
    connect(refreshButton, SIGNAL(clicked()), this, SLOT(refresh()));
    connect(apply, SIGNAL(clicked()), this, SLOT(applyLayer()));
    connect(mBank, SIGNAL(currentIndexChanged(int)), this, SLOT(selectBank()));
    connect(mAutoRefresh, SIGNAL(toggled(bool)), this, SLOT(updateTimer()));
    connect(mZoom, SIGNAL(currentIndexChanged(int)), this, SLOT(updateZoom()));
    connect(mSave, SIGNAL(clicked()), this, SLOT(saveImage()));
    QComboBox *combos[] = { mLayout, mColor, mWidth, mHeight };
    for (int i = 0; i < 4; ++i)
        connect(combos[i], SIGNAL(currentIndexChanged(int)), this, SLOT(redraw()));
    connect(mAddress, SIGNAL(valueChanged(int)), this, SLOT(addressChanged()));
    connect(mPalette, SIGNAL(valueChanged(int)), this, SLOT(redraw()));
    connect(mTransparency, SIGNAL(toggled(bool)), this, SLOT(redraw()));
    connect(mGrayscale, SIGNAL(toggled(bool)), this, SLOT(redraw()));
    redraw();
}

Vdp2VramConfig UIVdp2VramViewer::config() const
{
    Vdp2VramConfig c = {};
    c.color = mColor->currentIndex();
    c.tile_size = mLayout->currentIndex() == 2 ? 0 : mLayout->currentIndex() == 1 ? 16 : 8;
    c.width = mWidth->currentText().toInt();
    c.height = mHeight->currentText().toInt();
    c.start = mAddress->value();
    c.length = (mBank->currentIndex() ? mBank->currentIndex() * 0x20000 : 0x80000) - c.start;
    c.palette = mPalette->value();
    c.transparency = mTransparency->isChecked();
    c.grayscale = mGrayscale->isChecked();
    return c;
}

void UIVdp2VramViewer::refresh()
{
    if (!isVisible() || !mEmulation) return;
    QElapsedTimer elapsed;
    elapsed.start();
    // Like emulation, this slot runs on the GUI thread, between frames.
    if (mEmulation->init() == 0) setSnapshot(Vdp2Regs, Vdp2Ram, Vdp2ColorRam);
    else setSnapshot(NULL, NULL, NULL);
    mTimer->stop();
    if (mAutoRefresh->isChecked()) mTimer->start(qMax(500, int(elapsed.elapsed() * 4)));
}

void UIVdp2VramViewer::setSnapshot(const Vdp2 *regs, const u8 *ram, const u8 *cram)
{
    mValid = regs && ram && cram;
    if (mValid)
    {
        mRegs = *regs;
        mRam.resize(0x80000);
        mCram.resize(0x1000);
        memcpy(mRam.data(), ram, 0x80000);
        memcpy(mCram.data(), cram, 0x1000);
        if (mFirstCapture)
        {
            mFirstCapture = false;
            applyLayer();
        }
    }
    else mPresetInfo->clear();
    redraw();
}

void UIVdp2VramViewer::applyLayer()
{
    if (!mValid) return;
    Vdp2VramConfig c;
    if (Vdp2VramLayerConfig(&mRegs, mLayer->currentIndex(), &c))
    {
        mPresetInfo->setText(tr("Reserved color format; choose a format manually."));
        return;
    }
    mUpdating = true;
    mLayout->setCurrentIndex(!c.tile_size ? 2 : c.tile_size == 16 ? 1 : 0);
    mColor->setCurrentIndex(c.color);
    mBank->setCurrentIndex(0);
    mAddress->setValue(c.start);
    mPalette->setValue(c.palette);
    mWidth->setCurrentIndex(mWidth->findText(QString::number(c.width)));
    mHeight->setCurrentIndex(c.height == 512 ? 1 : 0);
    mUpdating = false;
    mPresetInfo->setText(tr("Copied %1 settings from the snapshot.").arg(mLayer->currentText()));
    redraw();
}

void UIVdp2VramViewer::selectBank()
{
    if (mUpdating) return;
    mUpdating = true;
    mAddress->setValue(qMax(0, mBank->currentIndex() - 1) * 0x20000);
    mUpdating = false;
    redraw();
}

void UIVdp2VramViewer::addressChanged()
{
    if (mUpdating) return;
    mUpdating = true;
    if (mBank->currentIndex()) mBank->setCurrentIndex(mAddress->value() / 0x20000 + 1);
    mUpdating = false;
    redraw();
}

void UIVdp2VramViewer::redraw()
{
    if (mUpdating) return;
    Vdp2VramConfig c = config();
    int width = 0, height = 0;
    mHeight->setEnabled(!c.tile_size);
    mBank->setEnabled(c.tile_size != 0);
    mPalette->setEnabled(c.color < 3 && !c.grayscale);
    mPalette->setSingleStep(c.color == 0 ? 16 : 256);
    mGrayscale->setEnabled(c.color < 3);
    mImage = QImage();
    if (mValid && !Vdp2VramDimensions(&c, &width, &height))
    {
        mImage = QImage(width, height, QImage::Format_ARGB32);
        if (mImage.isNull() || Vdp2VramRender(&c, mRam.constData(), mCram.constData(),
            (mRegs.RAMCTL >> 12) & 3, reinterpret_cast<u32 *>(mImage.bits()), width * height))
            mImage = QImage();
    }
    mPixmap->setPixmap(QPixmap::fromImage(mImage));
    mView->scene()->setSceneRect(QRectF(mImage.rect()));
    mSave->setEnabled(!mImage.isNull());
    mHover->setText(tr("Point at an image pixel to inspect its VRAM address. Drag to pan."));
    mStatus->setText(!mValid ? tr("Start emulation to view VRAM.") : mImage.isNull()
        ? tr("Cannot decode this VRAM / CRAM format.")
        : tr("%1 x %2 | %3 | CRAM mode %4%5").arg(width).arg(height)
            .arg(hexAddress(c.start)).arg((mRegs.RAMCTL >> 12) & 3)
            .arg(c.tile_size ? QString() : tr(" | wraps at VRAM end")));
    updateZoom();
}

void UIVdp2VramViewer::updateZoom()
{
    if (mZoom->currentIndex() == 0)
    {
        if (!mImage.isNull()) mView->fitInView(mView->sceneRect(), Qt::KeepAspectRatio);
    }
    else
    {
        const int zoom = 1 << (mZoom->currentIndex() - 1);
        mView->setTransform(QTransform::fromScale(zoom, zoom));
    }
}

void UIVdp2VramViewer::updateTimer()
{
    mTimer->stop();
    if (isVisible() && mEmulation && mAutoRefresh->isChecked()) mTimer->start(500);
}

void UIVdp2VramViewer::showEvent(QShowEvent *event)
{
    QDialog::showEvent(event);
    emit visibilityChanged(true);
    refresh();
}

void UIVdp2VramViewer::hideEvent(QHideEvent *event)
{
    mTimer->stop();
    QDialog::hideEvent(event);
    emit visibilityChanged(false);
}

void UIVdp2VramViewer::resizeEvent(QResizeEvent *event)
{
    QDialog::resizeEvent(event);
    updateZoom();
}

bool UIVdp2VramViewer::eventFilter(QObject *object, QEvent *event)
{
    if (object == mView->viewport() && event->type() == QEvent::MouseMove && !mImage.isNull())
    {
        const QPointF p = mView->mapToScene(static_cast<QMouseEvent *>(event)->pos());
        Vdp2VramConfig c = config();
        u32 address;
        int nibble;
        if (p.x() >= 0 && p.y() >= 0 &&
            !Vdp2VramPixelAddress(&c, int(p.x()), int(p.y()), &address, &nibble))
            mHover->setText(tr("Pixel (%1, %2) | VRAM %3 | SH2 %4%5")
                .arg(int(p.x())).arg(int(p.y())).arg(hexAddress(address))
                .arg(hexAddress(0x25e00000 + address, 8))
                .arg(c.color == 0 ? (nibble ? tr(" | low nibble") : tr(" | high nibble")) : QString()));
        else mHover->setText(tr("Outside image data."));
    }
    return QDialog::eventFilter(object, event);
}

void UIVdp2VramViewer::saveImage()
{
    if (mImage.isNull()) return;
    // A native file dialog may process timers; save the image seen on click.
    const QImage image = mImage;
    QString path = QFileDialog::getSaveFileName(this, tr("Save VRAM image"), "vdp2-vram.png", tr("PNG image (*.png)"));
    if (path.isEmpty()) return;
    if (!path.endsWith(".png", Qt::CaseInsensitive)) path += ".png";
    if (!image.save(path, "PNG")) QMessageBox::warning(this, tr("Save VRAM image"), tr("Could not save the image."));
}
