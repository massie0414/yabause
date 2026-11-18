/* This file is part of Yabause, licensed under GPL-2.0-or-later. */
#include "UIVdp1VramViewer.h"
#include "../YabauseThread.h"
extern "C" {
#include "../../vdp1debug.h"
}

#include <QAbstractListModel>
#include <QCache>
#include <QCheckBox>
#include <QComboBox>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QGraphicsPixmapItem>
#include <QGraphicsView>
#include <QGridLayout>
#include <QHideEvent>
#include <QIcon>
#include <QLabel>
#include <QListView>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QScrollBar>
#include <QShowEvent>
#include <QSpinBox>
#include <QSplitter>
#include <QTabWidget>
#include <QTimer>
#include <QVector>

static QString hexAddress(u32 address)
{
    return QString::fromLatin1("0x%1").arg(address, 5, 16, QLatin1Char('0')).toUpper();
}

static QString imageKey(const Vdp1DebugImage &image)
{
    return QString::fromLatin1("%1/%2/%3/%4/%5").arg(image.address).arg(image.width)
        .arg(image.height).arg(image.pmod & 0x10F8).arg(image.color);
}

static QString modeName(int mode)
{
    static const char *names[] = { "4 bpp / 16 colors (bank)", "4 bpp / 16 colors (LUT)",
        "8 bpp / 64 colors", "8 bpp / 128 colors", "8 bpp / 256 colors", "16 bpp / RGB555",
        "4 bpp / grayscale", "8 bpp / grayscale" };
    return UIVdp1VramViewer::tr(names[mode]);
}

static QPixmap checkerboard()
{
    QPixmap tile(16, 16);
    tile.fill(QColor(48, 51, 56));
    QPainter painter(&tile);
    painter.fillRect(0, 0, 8, 8, QColor(68, 71, 76));
    painter.fillRect(8, 8, 8, 8, QColor(68, 71, 76));
    return tile;
}

class Vdp1ImageView : public QGraphicsView
{
public:
    Vdp1ImageView(QWidget *parent) : QGraphicsView(parent), mZoom(0)
    {
        setScene(new QGraphicsScene(this));
        setBackgroundBrush(QBrush(checkerboard()));
        setDragMode(QGraphicsView::ScrollHandDrag);
        setMinimumSize(200, 180);
    }
    void setImage(const QImage &image)
    {
        scene()->clear();
        scene()->addPixmap(QPixmap::fromImage(image));
        setSceneRect(QRectF(QPointF(), image.size()));
        applyZoom();
    }
    void setZoom(int zoom) { mZoom = zoom; applyZoom(); }
protected:
    void resizeEvent(QResizeEvent *event)
    {
        QGraphicsView::resizeEvent(event);
        applyZoom();
    }
private:
    void applyZoom()
    {
        resetTransform();
        if (!mZoom && !sceneRect().isEmpty()) fitInView(sceneRect(), Qt::KeepAspectRatio);
        else if (mZoom) scale(mZoom, mZoom);
    }
    int mZoom;
};

// Only visible thumbnails are decoded; a large command list does not allocate
// thousands of full-sized textures. Every image uses the same captured palette.
class Vdp1TextureModel : public QAbstractListModel
{
public:
    Vdp1TextureModel(QObject *parent) : QAbstractListModel(parent), spctl(0), craofb(0),
        walkStatus(VDP1_DEBUG_END), references(0), icons(512) { }

    int rowCount(const QModelIndex &parent = QModelIndex()) const
    { return parent.isValid() ? 0 : images.size(); }

    QVariant data(const QModelIndex &index, int role) const
    {
        if (!index.isValid() || index.row() >= images.size()) return QVariant();
        const Vdp1DebugImage &image = images[index.row()];
        if (role == Qt::DisplayRole)
            return QString::fromLatin1("%1\n%2 x %3").arg(hexAddress(image.address))
                .arg(image.width).arg(image.height);
        if (role == Qt::ToolTipRole)
            return modeName((image.pmod >> 3) & 7) + QString::fromLatin1("\nCMD: %1\n%2 bytes")
                .arg(hexAddress(image.command_address)).arg(Vdp1DebugImageBytes(&image));
        if (role == Qt::DecorationRole)
        {
            if (!icons.contains(index.row()))
            {
                QPixmap thumbnail(112, 88);
                QPainter painter(&thumbnail);
                painter.fillRect(thumbnail.rect(), QBrush(checkerboard()));
                const QImage decoded = decode(image);
                const QSize size = decoded.size().scaled(thumbnail.size(), Qt::KeepAspectRatio);
                painter.drawImage(QRect(QPoint((112 - size.width()) / 2, (88 - size.height()) / 2), size), decoded);
                painter.end();
                icons.insert(index.row(), new QIcon(thumbnail));
            }
            return *icons.object(index.row());
        }
        return QVariant();
    }

    QImage decode(const Vdp1DebugImage &image) const
    {
        if (ram.isEmpty()) return QImage();
        QImage result(image.width, image.height, QImage::Format_ARGB32);
        if (result.isNull() || Vdp1DebugDecodeImage(
                reinterpret_cast<const u8 *>(ram.constData()), palette, spctl, craofb,
                &image, reinterpret_cast<u32 *>(result.bits()), image.width * image.height) != 0)
            return QImage();
        return result;
    }

    void capture(bool available)
    {
        beginResetModel();
        images.clear();
        icons.clear();
        references = 0;
        walkStatus = VDP1_DEBUG_END;
        ram.clear();
        if (available)
        {
            // Yabause runs frames on the GUI thread. This timer copies VRAM
            // between frames, and never retains pointers into live memory.
            ram = QByteArray(reinterpret_cast<const char *>(Vdp1Ram), VDP1_DEBUG_VRAM_SIZE);
            Vdp1DebugBuildPalette(reinterpret_cast<const u16 *>(Vdp2ColorRam),
                                  Vdp2Internal.ColorMode, palette);
            spctl = Vdp2Regs->SPCTL;
            craofb = Vdp2Regs->CRAOFB;
            QVector<Vdp1DebugImage> commands(VDP1_DEBUG_MAX_COMMANDS);
            references = Vdp1DebugCollectImages(reinterpret_cast<const u8 *>(ram.constData()),
                commands.data(), commands.size(), &walkStatus);
            QMap<QString, bool> seen;
            for (int i = 0; i < references; ++i)
            {
                const QString key = imageKey(commands[i]);
                if (!seen.contains(key)) { seen.insert(key, true); images.append(commands[i]); }
            }
        }
        endResetModel();
    }

    QByteArray ram;
    u32 palette[2048];
    u16 spctl, craofb;
    int walkStatus, references;
    QVector<Vdp1DebugImage> images;
    mutable QCache<int, QIcon> icons;
};

static QSpinBox *spinBox(QWidget *parent, int minimum, int maximum, int value, bool hex = false)
{
    QSpinBox *spin = new QSpinBox(parent);
    spin->setRange(minimum, maximum);
    spin->setValue(value);
    spin->setKeyboardTracking(false);
    if (hex) { spin->setDisplayIntegerBase(16); spin->setPrefix("0x"); }
    return spin;
}

static QComboBox *zoomControl(Vdp1ImageView *view, QWidget *parent)
{
    QComboBox *zoom = new QComboBox(parent);
    zoom->addItems(QStringList() << UIVdp1VramViewer::tr("Fit") << "1x" << "2x" << "4x" << "8x");
    QObject::connect(zoom, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
        view, [view](int index) { view->setZoom(index ? 1 << (index - 1) : 0); });
    return zoom;
}

UIVdp1VramViewer::UIVdp1VramViewer(YabauseThread *emulation, QWidget *parent)
    : QDialog(parent, Qt::Window), mEmulation(emulation), mModel(new Vdp1TextureModel(this)),
      mTimer(new QTimer(this))
{
    setObjectName("vdp1VramViewer");
    setWindowTitle(tr("VDP1 VRAM Viewer"));
    resize(1000, 700);
    QVBoxLayout *layout = new QVBoxLayout(this);
    QLabel *description = new QLabel(tr("VDP1 VRAM: 512 KiB. Textures shows images referenced by the current command list. "
        "Use Raw VRAM to inspect other addresses. Checkerboard = transparent."), this);
    description->setWordWrap(true);
    layout->addWidget(description);
    mTabs = new QTabWidget(this);
    layout->addWidget(mTabs, 1);

    QSplitter *textures = new QSplitter(this);
    mTextures = new QListView(textures);
    mTextures->setObjectName("vramTextures");
    mTextures->setModel(mModel);
    mTextures->setViewMode(QListView::IconMode);
    mTextures->setResizeMode(QListView::Adjust);
    mTextures->setMovement(QListView::Static);
    mTextures->setIconSize(QSize(112, 88));
    mTextures->setGridSize(QSize(140, 138));
    mTextures->setUniformItemSizes(true);
    mTextures->setSelectionMode(QAbstractItemView::SingleSelection);
    QWidget *detail = new QWidget(textures);
    QVBoxLayout *detailLayout = new QVBoxLayout(detail);
    mTextureView = new Vdp1ImageView(detail);
    detailLayout->addWidget(mTextureView, 1);
    detailLayout->addWidget(zoomControl(mTextureView, detail));
    mDetails = new QLabel(detail);
    mDetails->setObjectName("vramTextureDetails");
    mDetails->setWordWrap(true);
    mDetails->setTextInteractionFlags(Qt::TextSelectableByMouse);
    detailLayout->addWidget(mDetails);
    mInspect = new QPushButton(tr("Open in Raw VRAM"), detail);
    detailLayout->addWidget(mInspect);
    textures->setSizes(QList<int>() << 560 << 400);
    mTabs->addTab(textures, tr("Textures"));

    QWidget *raw = new QWidget(this);
    QVBoxLayout *rawLayout = new QVBoxLayout(raw);
    QGridLayout *fields = new QGridLayout;
    mAddress = spinBox(raw, 0, 0x7FFFF, 0, true);
    mAddress->setObjectName("vramAddress");
    mWidth = spinBox(raw, 8, 1024, 512);
    mWidth->setSingleStep(8);
    mWidth->setObjectName("vramWidth");
    mHeight = spinBox(raw, 1, 2048, 256);
    mHeight->setObjectName("vramHeight");
    mColor = spinBox(raw, 0, 0xFFFF, 0, true);
    mColor->setObjectName("vramColor");
    mColor->setToolTip(tr("CMDCOLR value. In LUT mode the table address is this value multiplied by 8."));
    mMode = new QComboBox(raw);
    mMode->setObjectName("vramMode");
    for (int i = 0; i < 8; ++i) mMode->addItem(modeName(i));
    mMode->setCurrentIndex(7);
    fields->addWidget(new QLabel(tr("VRAM offset"), raw), 0, 0);
    fields->addWidget(mAddress, 0, 1);
    fields->addWidget(new QLabel(tr("Width"), raw), 0, 2);
    fields->addWidget(mWidth, 0, 3);
    fields->addWidget(new QLabel(tr("Height"), raw), 0, 4);
    fields->addWidget(mHeight, 0, 5);
    fields->addWidget(mMode, 1, 0, 1, 2);
    fields->addWidget(new QLabel(tr("Bank / LUT (CMDCOLR)"), raw), 1, 2, 1, 2);
    fields->addWidget(mColor, 1, 4, 1, 2);
    rawLayout->addLayout(fields);
    mRawView = new Vdp1ImageView(raw);
    rawLayout->addWidget(mRawView, 1);
    QHBoxLayout *rawControls = new QHBoxLayout;
    rawControls->addWidget(zoomControl(mRawView, raw));
    QPushButton *previous = new QPushButton(tr("Previous page"), raw);
    QPushButton *next = new QPushButton(tr("Next page"), raw);
    rawControls->addWidget(previous);
    rawControls->addWidget(next);
    mTransparency = new QCheckBox(tr("Apply transparency / end codes"), raw);
    rawControls->addWidget(mTransparency);
    rawControls->addStretch();
    rawLayout->addLayout(rawControls);
    mRawStatus = new QLabel(raw);
    mRawStatus->setWordWrap(true);
    rawLayout->addWidget(mRawStatus);
    QLabel *rawNote = new QLabel(tr("VRAM also contains commands and tables. Width and color format cannot be inferred "
        "for unreferenced data. Grayscale shows pixel indices without a palette."), raw);
    rawNote->setWordWrap(true);
    rawLayout->addWidget(rawNote);
    mTabs->addTab(raw, tr("Raw VRAM"));

    QLabel *note = new QLabel(tr("Source images before flipping, distortion, shading and layer blending. "
        "Palette colors use the captured VDP2 color RAM."), this);
    note->setWordWrap(true);
    layout->addWidget(note);
    QHBoxLayout *controls = new QHBoxLayout;
    mAutoRefresh = new QCheckBox(tr("Auto refresh"), this);
    mAutoRefresh->setChecked(true);
    mAutoRefresh->setObjectName("vramAutoRefresh");
    controls->addWidget(mAutoRefresh);
    QPushButton *refreshButton = new QPushButton(tr("Refresh"), this);
    controls->addWidget(refreshButton);
    mSave = new QPushButton(tr("Save PNG..."), this);
    controls->addWidget(mSave);
    mStatus = new QLabel(this);
    mStatus->setObjectName("vramStatus");
    mStatus->setWordWrap(true);
    controls->addWidget(mStatus, 1);
    layout->addLayout(controls);
    mTimer->setSingleShot(true);
    connect(mTimer, SIGNAL(timeout()), this, SLOT(refresh()));
    connect(refreshButton, SIGNAL(clicked()), this, SLOT(refresh()));
    connect(mAutoRefresh, SIGNAL(toggled(bool)), this, SLOT(updateTimer()));
    connect(mTextures->selectionModel(), SIGNAL(currentChanged(QModelIndex,QModelIndex)), this, SLOT(selectTexture()));
    connect(mInspect, SIGNAL(clicked()), this, SLOT(inspectTexture()));
    connect(mAddress, SIGNAL(valueChanged(int)), this, SLOT(updateRaw()));
    connect(mWidth, SIGNAL(valueChanged(int)), this, SLOT(updateRaw()));
    connect(mHeight, SIGNAL(valueChanged(int)), this, SLOT(updateRaw()));
    connect(mColor, SIGNAL(valueChanged(int)), this, SLOT(updateRaw()));
    connect(mMode, SIGNAL(currentIndexChanged(int)), this, SLOT(updateRaw()));
    connect(mTransparency, SIGNAL(toggled(bool)), this, SLOT(updateRaw()));
    connect(mTabs, SIGNAL(currentChanged(int)), this, SLOT(updateRaw()));
    connect(previous, SIGNAL(clicked()), this, SLOT(previousPage()));
    connect(next, SIGNAL(clicked()), this, SLOT(nextPage()));
    connect(mSave, SIGNAL(clicked()), this, SLOT(saveImage()));
    selectTexture();
    updateRaw();
}

void UIVdp1VramViewer::showEvent(QShowEvent *event)
{
    QDialog::showEvent(event);
    emit visibilityChanged(true);
    refresh();
}

void UIVdp1VramViewer::hideEvent(QHideEvent *event)
{
    QDialog::hideEvent(event);
    mTimer->stop();
    emit visibilityChanged(false);
}

void UIVdp1VramViewer::updateTimer()
{
    mTimer->stop();
    if (isVisible() && mAutoRefresh->isChecked()) mTimer->start(500);
}

void UIVdp1VramViewer::refresh()
{
    if (!isVisible()) return;
    QElapsedTimer elapsed;
    elapsed.start();
    const int oldRow = mTextures->currentIndex().row();
    const QString selected = oldRow >= 0 && oldRow < mModel->images.size()
        ? imageKey(mModel->images[oldRow]) : QString();
    const int scroll = mTextures->verticalScrollBar()->value();
    const bool available = mEmulation->init() == 0 && Vdp1Ram && Vdp2ColorRam && Vdp2Regs;
    mModel->capture(available);
    int row = 0;
    for (int i = 0; i < mModel->images.size(); ++i)
        if (imageKey(mModel->images[i]) == selected) { row = i; break; }
    mTextures->setCurrentIndex(mModel->index(row));
    mTextures->verticalScrollBar()->setValue(scroll);
    selectTexture();
    updateRaw();
    QString status = available ? tr("%1 textures / %2 sprite references")
        .arg(mModel->images.size()).arg(mModel->references) : tr("Start emulation to view VRAM.");
    if (mModel->walkStatus != VDP1_DEBUG_END)
        status += mModel->walkStatus == VDP1_DEBUG_LIMIT
            ? tr(" - command limit reached (possible loop).") : tr(" - invalid command list; partial results.");
    mStatus->setText(status);
    updateTimer();
    if (mTimer->isActive()) mTimer->start(qMax(500, int(elapsed.elapsed() * 4)));
}

void UIVdp1VramViewer::selectTexture()
{
    const int row = mTextures->currentIndex().row();
    const bool valid = row >= 0 && row < mModel->images.size();
    mInspect->setEnabled(valid);
    mTextureImage = valid ? mModel->decode(mModel->images[row]) : QImage();
    mTextureView->setImage(mTextureImage);
    if (valid)
    {
        const Vdp1DebugImage &image = mModel->images[row];
        mDetails->setText(tr("VRAM %1 (CPU %2)\n%3 x %4 - %5 bytes\n%6\nFirst command: %7\nCMDCOLR: %8  CMDPMOD: %9")
            .arg(hexAddress(image.address)).arg(hexAddress(0x25C00000 + image.address))
            .arg(image.width).arg(image.height).arg(Vdp1DebugImageBytes(&image))
            .arg(modeName((image.pmod >> 3) & 7)).arg(hexAddress(image.command_address))
            .arg(hexAddress(image.color)).arg(hexAddress(image.pmod)));
    }
    else mDetails->setText(tr("No referenced textures. Use Raw VRAM to inspect memory."));
    mSave->setEnabled(!(mTabs->currentIndex() == 0 ? mTextureImage : mRawImage).isNull());
}

void UIVdp1VramViewer::updateRaw()
{
    mColor->setEnabled(mMode->currentIndex() < 5);
    mTransparency->setEnabled(mMode->currentIndex() < 6);
    if (mTabs->currentIndex() == 1)
    {
        Vdp1DebugImage image = {};
        image.address = mAddress->value();
        image.width = mWidth->value();
        image.height = mHeight->value();
        image.pmod = (mMode->currentIndex() << 3) | (mTransparency->isChecked() ? 0 : 0xC0);
        image.color = mColor->value();
        mRawImage = mModel->decode(image);
        mRawView->setImage(mRawImage);
        const u32 bytes = Vdp1DebugImageBytes(&image);
        mRawStatus->setText(mRawImage.isNull() ? tr("Start emulation to view VRAM.")
            : tr("%1 bytes from %2 to %3%4").arg(bytes).arg(hexAddress(image.address))
                .arg(hexAddress((image.address + bytes - 1) & 0x7FFFF))
                .arg(image.address + bytes > VDP1_DEBUG_VRAM_SIZE ? tr(" (wraps at 0x80000)") : QString()));
    }
    mSave->setEnabled(!(mTabs->currentIndex() == 0 ? mTextureImage : mRawImage).isNull());
}

void UIVdp1VramViewer::previousPage()
{
    Vdp1DebugImage image = {};
    image.width = mWidth->value(); image.height = mHeight->value(); image.pmod = mMode->currentIndex() << 3;
    mAddress->setValue(qMax(0, mAddress->value() - int(Vdp1DebugImageBytes(&image))));
}

void UIVdp1VramViewer::nextPage()
{
    Vdp1DebugImage image = {};
    image.width = mWidth->value(); image.height = mHeight->value(); image.pmod = mMode->currentIndex() << 3;
    mAddress->setValue((mAddress->value() + Vdp1DebugImageBytes(&image)) & 0x7FFFF);
}

void UIVdp1VramViewer::inspectTexture()
{
    const int row = mTextures->currentIndex().row();
    if (row < 0 || row >= mModel->images.size()) return;
    const Vdp1DebugImage &image = mModel->images[row];
    mAddress->setValue(image.address);
    mWidth->setValue(image.width);
    mHeight->setValue(image.height);
    mColor->setValue(image.color);
    mMode->setCurrentIndex((image.pmod >> 3) & 7);
    // Raw inspection defaults to exposing every stored pixel, including codes.
    mTransparency->setChecked(false);
    mTabs->setCurrentIndex(1);
}

void UIVdp1VramViewer::saveImage()
{
    const QImage image = mTabs->currentIndex() == 0 ? mTextureImage : mRawImage;
    if (image.isNull()) return;
    const QString path = QFileDialog::getSaveFileName(this, tr("Save VRAM image"), "vdp1-texture.png", tr("PNG images (*.png)"));
    if (!path.isEmpty() && !image.save(path, "PNG"))
        QMessageBox::warning(this, tr("Save VRAM image"), tr("Could not save the image."));
}
