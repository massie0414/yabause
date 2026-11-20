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
#include <QHash>
#include <QHideEvent>
#include <QLabel>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QShowEvent>
#include <QSpinBox>
#include <QSplitter>
#include <QTabWidget>
#include <QTimer>

static QString hexAddress(u32 address, int digits = 5)
{
    return QString("0x%1").arg(address, digits, 16, QLatin1Char('0')).toUpper();
}

class CellMapView : public QGraphicsView
{
public:
    CellMapView(QWidget *parent) : QGraphicsView(parent), tileSize(8), grid(false) {}
    int tileSize;
    bool grid;
protected:
    void drawForeground(QPainter *painter, const QRectF &rect)
    {
        if (!grid) return;
        const QRectF area = rect.intersected(sceneRect());
        const int step = transform().m11() >= 1 ? tileSize : 512;
        painter->save();
        for (int axis = 0; axis < 2; ++axis)
        {
            const int begin = int(axis ? area.top() : area.left()) / step * step;
            const int end = int(axis ? area.bottom() : area.right());
            for (int p = begin; p <= end; p += step)
            {
                QPen pen(p % 512 ? QColor(255, 255, 255, 60) : QColor(70, 200, 255, 190));
                pen.setCosmetic(true);
                painter->setPen(pen);
                if (axis) painter->drawLine(QPointF(area.left(), p), QPointF(area.right(), p));
                else painter->drawLine(QPointF(p, area.top()), QPointF(p, area.bottom()));
            }
        }
        painter->restore();
    }
};

UIVdp2VramViewer::UIVdp2VramViewer(YabauseThread *emulation, QWidget *parent)
    : QDialog(parent, Qt::Window), mEmulation(emulation), mValid(false),
      mUpdating(false), mFirstCapture(true)
{
    setObjectName("vdp2VramViewer");
    setWindowTitle(tr("VDP2 VRAM Viewer"));
    resize(1000, 740);
    QVBoxLayout *layout = new QVBoxLayout(this);
    QLabel *description = new QLabel(tr("Inspect raw VRAM, or select Cell map to see a layer's tile images and map layout."), this);
    description->setWordWrap(true);
    layout->addWidget(description);

    QHBoxLayout *presets = new QHBoxLayout;
    presets->addWidget(new QLabel(tr("Layer:"), this));
    mLayer = new QComboBox(this);
    mLayer->setObjectName("vramLayer");
    mLayer->addItems(QStringList() << "NBG0" << "NBG1" << "NBG2" << "NBG3"
        << "RBG0 (A)" << "RBG0 (B)" << "RBG1");
    presets->addWidget(mLayer);
    QPushButton *apply = new QPushButton(tr("Use layer settings"), this);
    apply->setObjectName("vramApplyLayer");
    presets->addWidget(apply);
    mPresetInfo = new QLabel(this);
    mPresetInfo->setWordWrap(true);
    presets->addWidget(mPresetInfo, 1);
    layout->addLayout(presets);

    mTabs = new QTabWidget(this);
    mTabs->setObjectName("vramTabs");
    QWidget *rawTab = new QWidget(mTabs);
    QVBoxLayout *rawLayout = new QVBoxLayout(rawTab);
    mTabs->addTab(rawTab, tr("Raw VRAM"));
    layout->addWidget(mTabs, 1);

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
    rawLayout->addLayout(controls);

    QHBoxLayout *options = new QHBoxLayout;
    mTransparency = new QCheckBox(tr("Show transparency"), this);
    mTransparency->setObjectName("vramTransparency");
    mGrayscale = new QCheckBox(tr("Grayscale indices"), this);
    mGrayscale->setObjectName("vramGrayscale");
    options->addWidget(mTransparency);
    options->addWidget(mGrayscale);
    options->addStretch();
    rawLayout->addLayout(options);

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
    rawLayout->addWidget(mView, 1);
    mHover = new QLabel(tr("Point at an image pixel to inspect its VRAM address. Drag to pan."), this);
    mHover->setObjectName("vramHover");
    mHover->setWordWrap(true);
    rawLayout->addWidget(mHover);
    QLabel *note = new QLabel(tr("VRAM also contains maps and tables, which may look like noise. Tile presets use the layer's CRAM offset; individual tiles can need different palettes. Checkerboard = transparent."), this);
    note->setWordWrap(true);
    rawLayout->addWidget(note);
    createCellTab();

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
    connect(mTabs, SIGNAL(currentChanged(int)), this, SLOT(redraw()));
    connect(mLayer, SIGNAL(currentIndexChanged(int)), this, SLOT(redraw()));
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
    const bool cells = mTabs->currentIndex() == 1;
    mSave->setVisible(!cells);
    mPresetInfo->setVisible(!cells);
    findChild<QPushButton *>("vramApplyLayer")->setVisible(!cells);
    if (cells)
    {
        redrawCells();
        return;
    }
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
    updateCellZoom();
}

bool UIVdp2VramViewer::eventFilter(QObject *object, QEvent *event)
{
    if ((object == mMapView->viewport() || object == mTileView->viewport()) &&
        (event->type() == QEvent::MouseMove || event->type() == QEvent::MouseButtonPress) &&
        !mMapImage.isNull())
    {
        const bool map = object == mMapView->viewport();
        QGraphicsView *view = map ? mMapView : mTileView;
        const QPointF p = view->mapToScene(static_cast<QMouseEvent *>(event)->pos());
        const QImage &img = map ? mMapImage : mTileImage;
        const int size = mCellMap.tile.tile_size;
        if (p.x() >= 0 && p.y() >= 0 && p.x() < img.width() && p.y() < img.height())
        {
            int index = int(p.y()) / size * (img.width() / size) + int(p.x()) / size;
            if (map)
            {
                Vdp2Cell cell;
                if (!Vdp2CellMapRead(&mCellMap, mRam.constData(), mMapOriginX + int(p.x()),
                    mMapOriginY + int(p.y()), &cell))
                    mCellHover->setText(tr("Cell (%1, %2) | %3")
                        .arg((mMapOriginX + int(p.x())) / size).arg((mMapOriginY + int(p.y())) / size)
                        .arg(cellDescription(cell)));
                index = mCellIndices.value(index, -1);
            }
            else if (index < mCells.size())
                mCellHover->setText(tr("Tile %1 | First reference: %2").arg(index).arg(cellDescription(mCells[index])));
            else mCellHover->setText(tr("Outside tile data."));
            if (index >= 0 && index < mCells.size())
            {
                const int columns = mTileImage.width() / size;
                mTileSelection->setRect((index % columns) * size, (index / columns) * size, size, size);
                mTileSelection->show();
                if (map && event->type() == QEvent::MouseButtonPress)
                    mTileView->ensureVisible(mTileSelection, 20, 20);
            }
        }
        else mCellHover->setText(tr("Outside image data."));
    }
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

void UIVdp2VramViewer::createCellTab()
{
    QWidget *tab = new QWidget(mTabs);
    QVBoxLayout *layout = new QVBoxLayout(tab);
    QHBoxLayout *controls = new QHBoxLayout;
    controls->addWidget(new QLabel(tr("Map area:"), tab));
    mPlane = new QComboBox(tab);
    mPlane->setObjectName("cellPlane");
    mPlane->addItem(tr("Whole map"));
    controls->addWidget(mPlane);
    mMapZoom = new QComboBox(tab);
    mMapZoom->setObjectName("cellMapZoom");
    mTileZoom = new QComboBox(tab);
    mTileZoom->setObjectName("cellTileZoom");
    const QStringList zooms = QStringList() << tr("Fit") << "1x" << "2x" << "4x" << "8x";
    mMapZoom->addItems(zooms);
    mTileZoom->addItems(zooms);
    mTileZoom->setCurrentIndex(3);
    controls->addWidget(new QLabel(tr("Map zoom:"), tab));
    controls->addWidget(mMapZoom);
    controls->addWidget(new QLabel(tr("Tile zoom:"), tab));
    controls->addWidget(mTileZoom);
    mCellGrid = new QCheckBox(tr("Cell grid"), tab);
    mCellGrid->setObjectName("cellGrid");
    mCellGrid->setChecked(true);
    controls->addWidget(mCellGrid);
    mCellTransparency = new QCheckBox(tr("Show transparency"), tab);
    mCellTransparency->setObjectName("cellTransparency");
    mCellTransparency->setChecked(true);
    controls->addWidget(mCellTransparency);
    controls->addStretch();
    layout->addLayout(controls);
    mCellInfo = new QLabel(tab);
    mCellInfo->setObjectName("cellInfo");
    mCellInfo->setWordWrap(true);
    layout->addWidget(mCellInfo);
    QSplitter *splitter = new QSplitter(Qt::Horizontal, tab);
    mTileView = new QGraphicsView(splitter);
    mTileView->setObjectName("cellTiles");
    mMapView = new CellMapView(splitter);
    mMapView->setObjectName("cellMap");
    QGraphicsView *views[] = { mTileView, mMapView };
    for (int i = 0; i < 2; ++i)
    {
        QGraphicsView *view = views[i];
        QWidget *panel = new QWidget(splitter);
        QVBoxLayout *panelLayout = new QVBoxLayout(panel);
        panelLayout->setContentsMargins(0, 0, 0, 0);
        panelLayout->addWidget(new QLabel(i ? tr("Map layout (before scrolling / rotation)")
            : tr("Referenced tiles (original orientation)"), panel));
        view->setScene(new QGraphicsScene(view));
        view->setBackgroundBrush(mView->backgroundBrush());
        view->setAlignment(Qt::AlignLeft | Qt::AlignTop);
        view->setDragMode(QGraphicsView::ScrollHandDrag);
        view->viewport()->setMouseTracking(true);
        view->viewport()->installEventFilter(this);
        QGraphicsPixmapItem *pixmap = view->scene()->addPixmap(QPixmap());
        pixmap->setTransformationMode(Qt::FastTransformation);
        if (i) mMapPixmap = pixmap; else mTilePixmap = pixmap;
        panelLayout->addWidget(view, 1);
        QPushButton *save = new QPushButton(i ? tr("Save map PNG...") : tr("Save tiles PNG..."), panel);
        save->setObjectName(i ? "cellSaveMap" : "cellSaveTiles");
        if (i) mSaveMap = save; else mSaveTiles = save;
        panelLayout->addWidget(save);
        connect(save, SIGNAL(clicked()), this, SLOT(saveCellImage()));
        splitter->addWidget(panel);
    }
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 2);
    QPen selectionPen(Qt::yellow);
    selectionPen.setCosmetic(true);
    selectionPen.setWidth(2);
    mTileSelection = mTileView->scene()->addRect(QRectF(), selectionPen);
    mTileSelection->hide();
    layout->addWidget(splitter, 1);
    mCellHover = new QLabel(tr("Point at a cell to inspect it. Click a map cell to locate its tile. Drag to pan."), tab);
    mCellHover->setObjectName("cellHover");
    mCellHover->setWordWrap(true);
    mCellHover->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(mCellHover);
    QLabel *note = new QLabel(tr("Tiles with the same image address and palette are grouped. Map palettes and flips are applied; scrolling, rotation, priority and color effects are not. Blue grid lines mark 512-pixel pages."), tab);
    note->setWordWrap(true);
    layout->addWidget(note);
    mTabs->addTab(tab, tr("Cell map"));
    connect(mPlane, SIGNAL(currentIndexChanged(int)), this, SLOT(redrawCells()));
    connect(mCellTransparency, SIGNAL(toggled(bool)), this, SLOT(redrawCells()));
    connect(mCellGrid, SIGNAL(toggled(bool)), this, SLOT(updateCellZoom()));
    connect(mMapZoom, SIGNAL(currentIndexChanged(int)), this, SLOT(updateCellZoom()));
    connect(mTileZoom, SIGNAL(currentIndexChanged(int)), this, SLOT(updateCellZoom()));
    connect(splitter, SIGNAL(splitterMoved(int,int)), this, SLOT(updateCellZoom()));
    mSaveMap->setEnabled(false);
    mSaveTiles->setEnabled(false);
}

void UIVdp2VramViewer::redrawCells()
{
    if (mUpdating || mTabs->currentIndex() != 1) return;
    mMapImage = QImage(); mTileImage = QImage();
    mMapPixmap->setPixmap(QPixmap()); mTilePixmap->setPixmap(QPixmap());
    mCells.clear(); mCellIndices.clear();
    mTileSelection->hide();
    mMapOriginX = mMapOriginY = 0;
    int result = mValid ? Vdp2CellMapConfig(&mRegs, mLayer->currentIndex(), &mCellMap) : -1;
    QString info;
    if (!mValid) info = tr("Start emulation to view a cell map.");
    else if (result == 1) info = tr("%1 uses bitmap mode. Use Raw VRAM and 'Use layer settings' to inspect its image.").arg(mLayer->currentText());
    else if (result) info = tr("Reserved color format; this cell map cannot be decoded.");
    else
    {
        const int count = mCellMap.map_columns * mCellMap.map_columns;
        int selected = qMin(mPlane->currentIndex(), count);
        mPlane->blockSignals(true);
        mPlane->clear();
        mPlane->addItem(tr("Whole map"));
        for (int i = 0; i < count; ++i)
            mPlane->addItem(tr("Plane %1 (%2, %3) - %4").arg(QChar('A' + i))
                .arg(i % mCellMap.map_columns).arg(i / mCellMap.map_columns)
                .arg(hexAddress(mCellMap.plane_address[i])));
        mPlane->setCurrentIndex(selected);
        mPlane->blockSignals(false);
        int width = mCellMap.plane_width, height = mCellMap.plane_height;
        if (selected)
        {
            mMapOriginX = (selected - 1) % mCellMap.map_columns * width;
            mMapOriginY = (selected - 1) / mCellMap.map_columns * height;
        }
        else { width *= mCellMap.map_columns; height *= mCellMap.map_columns; }
        mCellMap.tile.transparency = mCellMap.tile.transparency && mCellTransparency->isChecked();
        const int size = mCellMap.tile.tile_size;
        QHash<quint64, int> unique;
        QVector<QImage> tiles;
        mMapImage = QImage(width, height, QImage::Format_ARGB32);
        bool valid = !mMapImage.isNull();
        for (int y = 0; valid && y < height; y += size)
            for (int x = 0; valid && x < width; x += size)
            {
                Vdp2Cell cell;
                if (Vdp2CellMapRead(&mCellMap, mRam.constData(), x + mMapOriginX, y + mMapOriginY, &cell))
                { valid = false; break; }
                const quint64 key = (quint64(cell.character_address) << 11) | cell.palette;
                int index = unique.value(key, -1);
                if (index < 0)
                {
                    QImage tile(size, size, QImage::Format_ARGB32);
                    if (tile.isNull() || Vdp2CellRender(&mCellMap, &cell, mRam.constData(), mCram.constData(),
                        (mRegs.RAMCTL >> 12) & 3, reinterpret_cast<u32 *>(tile.bits()), size * size))
                    { valid = false; break; }
                    index = tiles.size();
                    unique.insert(key, index);
                    tiles.append(tile);
                    mCells.append(cell);
                }
                mCellIndices.append(index);
                const QImage &tile = tiles[index];
                for (int ty = 0; ty < size; ++ty)
                {
                    u32 *dest = reinterpret_cast<u32 *>(mMapImage.scanLine(y + ty)) + x;
                    const u32 *source = reinterpret_cast<const u32 *>(tile.constScanLine(cell.flip & 2 ? size - 1 - ty : ty));
                    if (cell.flip & 1)
                        for (int tx = 0; tx < size; ++tx) dest[tx] = source[size - 1 - tx];
                    else memcpy(dest, source, size * sizeof(u32));
                }
            }
        int columns = 32;
        while ((tiles.size() + columns - 1) / columns * size > 4096) columns *= 2;
        if (valid)
        {
            mTileImage = QImage(qMin(columns, tiles.size()) * size,
                (tiles.size() + columns - 1) / columns * size, QImage::Format_ARGB32);
            valid = !mTileImage.isNull();
        }
        if (valid)
        {
            mTileImage.fill(Qt::transparent);
            QPainter painter(&mTileImage);
            for (int i = 0; i < tiles.size(); ++i)
                painter.drawImage((i % columns) * size, (i / columns) * size, tiles[i]);
            info = tr("%1 | %2 | %3 x %3 tiles | %4 | %5-word names | Map: %6 x %7 planes, %8 x %9 px | %10 unique tiles in this area")
                .arg(mLayer->currentText()).arg(mCellMap.enabled ? tr("Enabled") : tr("Disabled (showing stored map)"))
                .arg(size).arg(mColor->itemText(mCellMap.tile.color)).arg(mCellMap.pattern_bytes / 2)
                .arg(mCellMap.map_columns).arg(mCellMap.map_columns)
                .arg(mCellMap.plane_width * mCellMap.map_columns).arg(mCellMap.plane_height * mCellMap.map_columns)
                .arg(tiles.size());
        }
        else
        {
            mMapImage = QImage(); mTileImage = QImage();
            mCells.clear(); mCellIndices.clear();
            info = tr("Cannot decode this VRAM / CRAM format or allocate the map image.");
        }
    }
    mCellInfo->setText(info);
    mCellHover->setText(tr("Point at a cell to inspect it. Click a map cell to locate its tile. Drag to pan."));
    mPlane->setEnabled(mValid && !result);
    mMapPixmap->setPixmap(QPixmap::fromImage(mMapImage));
    mTilePixmap->setPixmap(QPixmap::fromImage(mTileImage));
    mMapView->scene()->setSceneRect(QRectF(mMapImage.rect()));
    mTileView->scene()->setSceneRect(QRectF(mTileImage.rect()));
    mSaveMap->setEnabled(!mMapImage.isNull());
    mSaveTiles->setEnabled(!mTileImage.isNull());
    mStatus->setText(mMapImage.isNull() ? tr("No cell image.") : tr("%1 x %2 map | %3 tiles")
        .arg(mMapImage.width()).arg(mMapImage.height()).arg(mCells.size()));
    updateCellZoom();
}

void UIVdp2VramViewer::updateCellZoom()
{
    if (mTabs->currentIndex() != 1) return;
    CellMapView *mapView = static_cast<CellMapView *>(mMapView);
    mapView->tileSize = mMapImage.isNull() ? 8 : mCellMap.tile.tile_size;
    mapView->grid = mCellGrid->isChecked() && !mMapImage.isNull();
    QGraphicsView *views[] = { mMapView, mTileView };
    QComboBox *zooms[] = { mMapZoom, mTileZoom };
    for (int i = 0; i < 2; ++i)
    {
        if (!zooms[i]->currentIndex())
        {
            if (!views[i]->sceneRect().isEmpty()) views[i]->fitInView(views[i]->sceneRect(), Qt::KeepAspectRatio);
        }
        else
        {
            int zoom = 1 << (zooms[i]->currentIndex() - 1);
            views[i]->setTransform(QTransform::fromScale(zoom, zoom));
        }
        views[i]->viewport()->update();
    }
}

QString UIVdp2VramViewer::cellDescription(const Vdp2Cell &c) const
{
    return tr("Plane %1 | Name @ %2 = %3 | Image @ %4 (SH2 %5) | Palette %6 | Flip H:%7 V:%8 | Special priority:%9 color:%10")
        .arg(QChar('A' + c.plane)).arg(hexAddress(c.pattern_address))
        .arg(hexAddress(c.pattern_name, mCellMap.pattern_bytes * 2))
        .arg(hexAddress(c.character_address)).arg(hexAddress(0x25e00000 + c.character_address, 8))
        .arg(hexAddress(c.palette, 3)).arg(c.flip & 1).arg((c.flip >> 1) & 1)
        .arg(c.special_priority).arg(c.special_color);
}

void UIVdp2VramViewer::saveCellImage()
{
    const bool map = sender() == mSaveMap;
    const QImage image = map ? mMapImage : mTileImage;
    if (image.isNull()) return;
    QString path = QFileDialog::getSaveFileName(this, tr("Save cell image"),
        map ? "vdp2-map.png" : "vdp2-tiles.png", tr("PNG image (*.png)"));
    if (path.isEmpty()) return;
    if (!path.endsWith(".png", Qt::CaseInsensitive)) path += ".png";
    if (!image.save(path, "PNG")) QMessageBox::warning(this, tr("Save cell image"), tr("Could not save the image."));
}
