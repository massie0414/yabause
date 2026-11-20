/* This file is part of Yabause, licensed under GPL-2.0-or-later. */
#include "UILayerViewer.h"
#include "../YabauseThread.h"

#include <QCheckBox>
#include <QComboBox>
#include <QElapsedTimer>
#include <QGridLayout>
#include <QGroupBox>
#include <QHideEvent>
#include <QImage>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QShowEvent>
#include <QTimer>
#include <QtCore/qmath.h>

class LayerPreviewWidget : public QWidget
{
public:
    LayerPreviewWidget(QWidget *parent) : QWidget(parent)
    {
        setMinimumSize(140, 90);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    }

    void setImage(const QImage &image)
    {
        mImage = image;
        update();
    }

protected:
    QRect imageRect() const
    {
        const QSize size = mImage.size().scaled(this->size(), Qt::KeepAspectRatio);
        return QRect(QPoint((width() - size.width()) / 2,
            (height() - size.height()) / 2), size);
    }

    void paintEvent(QPaintEvent *)
    {
        QPainter painter(this);
        painter.fillRect(rect(), QColor(24, 26, 30));
        if (mImage.isNull()) return;
        const QRect target = imageRect();
        painter.save();
        painter.setClipRect(target);
        for (int y = target.top(); y <= target.bottom(); y += 12)
            for (int x = target.left(); x <= target.right(); x += 12)
                painter.fillRect(x, y, 12, 12,
                    ((x - target.left()) / 12 + (y - target.top()) / 12) % 2
                    ? QColor(62, 65, 70) : QColor(48, 51, 56));
        painter.drawImage(target, mImage);
        painter.restore();
    }

protected:
    QImage mImage;
};

class SpriteOutlineWidget : public LayerPreviewWidget
{
public:
    SpriteOutlineWidget(QWidget *parent) : LayerPreviewWidget(parent), mMode(0)
    {
        setMinimumSize(280, 196);
        setAccessibleName(tr("VDP1 distorted sprite outlines"));
    }

    void setSnapshot(const QImage &image, const QImage &outlines)
    {
        mOriginal = image;
        mOutlines = outlines;
        rebuild();
    }

    void setMode(int mode) { mMode = mode; rebuild(); }

private:
    void rebuild()
    {
        if (mOriginal.isNull() || mOutlines.size() != mOriginal.size() || mMode == 2)
        {
            setImage(mOriginal);
            return;
        }
        QImage image = mOriginal.copy();
        if (mMode == 1) image.fill(Qt::transparent);
        // A dark one-pixel halo keeps the bright green edges readable on
        // both green textures and the checkerboard. Paint all edges last.
        for (int y = 0; y < image.height(); ++y)
        {
            const QRgb *edges = reinterpret_cast<const QRgb *>(mOutlines.constScanLine(y));
            for (int x = 0; x < image.width(); ++x)
            {
                if (!edges[x]) continue;
                for (int dy = -1; dy <= 1; ++dy)
                    for (int dx = -1; dx <= 1; ++dx)
                        if (x + dx >= 0 && x + dx < image.width() && y + dy >= 0 && y + dy < image.height())
                            image.setPixel(x + dx, y + dy, qRgb(0, 24, 0));
            }
        }
        QPainter painter(&image);
        painter.drawImage(0, 0, mOutlines);
        painter.end();
        setImage(image);
    }

    QImage mOriginal, mOutlines;
    int mMode;
};

class Rbg0TransformWidget : public LayerPreviewWidget
{
public:
    Rbg0TransformWidget(QLabel *detail, QWidget *parent)
        : LayerPreviewWidget(parent), mDetail(detail), mMode(0), mSpacing(64), mPoint(-1, -1)
    {
        setMouseTracking(true);
        setMinimumSize(280, 196);
        setAccessibleName(tr("RBG0 transformed source grid"));
    }

    void setSnapshot(const QImage &image, const QVector<VIDSoftRbg0Sample> &samples)
    {
        mOriginal = image;
        mSamples = samples; // Implicit sharing keeps a frozen viewer's capture intact.
        rebuild();
    }

    void setView(int mode, int spacing)
    {
        mMode = mode;
        mSpacing = spacing;
        rebuild();
    }

protected:
    void mouseMoveEvent(QMouseEvent *event) { selectPoint(event->pos()); }
    void mousePressEvent(QMouseEvent *event) { selectPoint(event->pos()); }

    void paintEvent(QPaintEvent *event)
    {
        LayerPreviewWidget::paintEvent(event);
        if (!validPoint()) return;
        const QRect target = imageRect();
        QPoint center(target.x() + (mPoint.x() + 0.5) * target.width() / mImage.width(),
            target.y() + (mPoint.y() + 0.5) * target.height() / mImage.height());
        QPainter painter(this);
        painter.setClipRect(target);
        for (int pass = 0; pass < 2; ++pass)
        {
            painter.setPen(QPen(pass ? Qt::white : Qt::black, pass ? 1 : 3));
            painter.drawLine(center + QPoint(-7, 0), center + QPoint(7, 0));
            painter.drawLine(center + QPoint(0, -7), center + QPoint(0, 7));
        }
    }

private:
    bool validPoint() const
    {
        return !mImage.isNull() && mPoint.x() >= 0 && mPoint.x() < mImage.width()
            && mPoint.y() >= 0 && mPoint.y() < mImage.height()
            && mSamples.size() >= mImage.width() * mImage.height();
    }

    void selectPoint(const QPoint &point)
    {
        if (mImage.isNull()) return;
        const QRect target = imageRect();
        if (!target.contains(point)) return;
        mPoint = QPoint((point.x() - target.x()) * mImage.width() / target.width(),
            (point.y() - target.y()) * mImage.height() / target.height());
        updateDetail();
        update();
    }

    int cell(int value) const { return int(qFloor(double(value) / mSpacing)); }

    bool crossesGrid(const VIDSoftRbg0Sample &a, const VIDSoftRbg0Sample &b) const
    {
        return a.parameter == b.parameter &&
            (cell(a.x) != cell(b.x) || cell(a.y) != cell(b.y));
    }

    void rebuild()
    {
        if (mOriginal.isNull() || mSamples.size() < mOriginal.width() * mOriginal.height())
        {
            setImage(QImage());
            updateDetail();
            return;
        }
        QImage image(mOriginal.size(), QImage::Format_ARGB32);
        const int width = image.width(), height = image.height();
        for (int y = 0; y < height; ++y)
        {
            QRgb *row = reinterpret_cast<QRgb *>(image.scanLine(y));
            const QRgb *original = reinterpret_cast<const QRgb *>(mOriginal.constScanLine(y));
            for (int x = 0; x < width; ++x)
            {
                const int index = y * width + x;
                const VIDSoftRbg0Sample &sample = mSamples[index];
                row[x] = mMode == 0 ? original[x] : 0;
                if (!sample.parameter) continue;
                const QColor color = sample.parameter == 1 ? QColor(64, 218, 242) : QColor(255, 174, 74);
                if (mMode == 2) row[x] = color.rgb();
                else
                {
                    const bool edge = (x > 0 && crossesGrid(sample, mSamples[index - 1]))
                        || (y > 0 && crossesGrid(sample, mSamples[index - width]))
                        || sample.x % mSpacing == 0 || sample.y % mSpacing == 0;
                    if (mMode == 1)
                        row[x] = color.darker(((cell(sample.x) + cell(sample.y)) & 1) ? 350 : 220).rgb();
                    if (edge) row[x] = color.rgb();
                }
            }
        }
        setImage(image);
        updateDetail();
    }

    void updateDetail()
    {
        if (!validPoint())
        {
            mDetail->setText(tr("Point at the image to inspect screen and source coordinates."));
            return;
        }
        const int index = mPoint.y() * mImage.width() + mPoint.x();
        const VIDSoftRbg0Sample &sample = mSamples[index];
        const QString screen = tr("Screen (%1, %2)").arg(mPoint.x()).arg(mPoint.y());
        if (!sample.parameter)
        {
            mDetail->setText(screen + tr(" | No source sample (disabled or clipped)."));
            return;
        }
        mDetail->setText(tr("%1 -> Source (%2, %3) -> Map (%4, %5) | Parameter %6\nkx = %7, ky = %8 | %9")
            .arg(screen).arg(sample.x).arg(sample.y).arg(sample.mapx).arg(sample.mapy)
            .arg(sample.parameter == 1 ? "A" : "B")
            .arg(sample.kx / 65536.0, 0, 'f', 4).arg(sample.ky / 65536.0, 0, 'f', 4)
            .arg(qAlpha(mOriginal.pixel(mPoint)) ? tr("Visible layer pixel") : tr("Transparent / priority 0")));
    }

    QLabel *mDetail;
    QImage mOriginal;
    QVector<VIDSoftRbg0Sample> mSamples;
    int mMode, mSpacing;
    QPoint mPoint;
};

UILayerPreviewSource::UILayerPreviewSource(YabauseThread *emulation, QObject *parent)
    : QObject(parent), mEmulation(emulation), mDistortedSpriteCount(0), mTimer(new QTimer(this))
{
    memset(&mRotation, 0, sizeof(mRotation));
    mTimer->setSingleShot(true);
    connect(mTimer, SIGNAL(timeout()), this, SLOT(refreshAutomatic()));
}

void UILayerPreviewSource::addViewer(UILayerViewer *viewer)
{
    mViewers.append(QPointer<UILayerViewer>(viewer));
}

bool UILayerPreviewSource::needsRefresh() const
{
    foreach (const QPointer<UILayerViewer> &viewer, mViewers)
        if (viewer && viewer->autoRefresh()) return true;
    return false;
}

void UILayerPreviewSource::updateTimer(int delay)
{
    mTimer->stop();
    if (needsRefresh()) mTimer->start(delay);
}

int UILayerPreviewSource::capture()
{
    QElapsedTimer elapsed;
    elapsed.start();
    int width = 0, height = 0;
    mDistortedSpriteCount = 0;
    bool available = mEmulation->init() == 0;
    bool rotationVisible = false;
    bool outlinesVisible = false;
    foreach (const QPointer<UILayerViewer> &viewer, mViewers)
    {
        if (viewer && viewer->isVisible() && viewer->mKind == UILayerViewer::RBG0Transform)
            rotationVisible = true;
        if (viewer && viewer->isVisible() && viewer->mKind == UILayerViewer::VDP1Outlines)
            outlinesVisible = true;
    }
    memset(&mRotation, 0, sizeof(mRotation));
    if (!rotationVisible || !available) mRotationSamples.clear();
    if (!outlinesVisible || !available) mOutlinePixels.clear();
    mOutlineImage = QImage();
    if (available)
    {
        u32 *layers[VIDSOFT_LAYER_PREVIEW_COUNT];
        for (int i = 0; i < VIDSOFT_LAYER_PREVIEW_COUNT; ++i)
        {
            mPixels[i].resize(704 * 512);
            layers[i] = mPixels[i].data();
        }
        // Emulation and this timer run on the GUI thread. Capture between
        // frames, not from swapBuffers while a renderer is still on the stack.
        if (rotationVisible)
        {
            mRotationSamples.resize(704 * 512);
            mRotation.samples = mRotationSamples.data();
        }
        if (outlinesVisible) mOutlinePixels.resize(704 * 512);
        available = VIDSoftRenderLayerPreviewsWithDebug(layers, &width, &height,
            &mDistortedSpriteCount, rotationVisible ? &mRotation : NULL,
            outlinesVisible ? mOutlinePixels.data() : NULL) == 0;
        if (available && outlinesVisible)
            mOutlineImage = QImage(reinterpret_cast<const uchar *>(mOutlinePixels.constData()),
                width, height, width * 4, QImage::Format_ARGB32).copy();
    }
    for (int i = 0; i < VIDSOFT_LAYER_PREVIEW_COUNT; ++i)
        mImages[i] = available
            ? QImage(reinterpret_cast<const uchar *>(mPixels[i].constData()),
                width, height, width * 4, QImage::Format_ARGB32).copy()
            : QImage();
    return qMax(200, int(elapsed.elapsed() * 4));
}

void UILayerPreviewSource::refreshAutomatic()
{
    if (!needsRefresh()) return;
    const int delay = capture();
    foreach (const QPointer<UILayerViewer> &viewer, mViewers)
        if (viewer && viewer->autoRefresh()) viewer->displayImages(mImages, mDistortedSpriteCount, mRotation, mRotationSamples, mOutlineImage);
    updateTimer(delay);
}

void UILayerPreviewSource::refreshViewer(UILayerViewer *viewer)
{
    mTimer->stop();
    const int delay = capture();
    viewer->displayImages(mImages, mDistortedSpriteCount, mRotation, mRotationSamples, mOutlineImage);
    updateTimer(delay);
}

UILayerViewer::UILayerViewer(UILayerPreviewSource *source, Kind kind, QWidget *parent)
    : QDialog(parent, Qt::Window), mSource(source), mKind(kind), mSpriteCount(NULL), mTransform(NULL),
      mTransformMode(NULL), mGridSpacing(NULL), mOutlines(NULL), mOutlineMode(NULL)
{
    const bool outlines = mKind == VDP1Outlines;
    const bool vdp1 = mKind == VDP1 || outlines;
    const bool rotation = mKind == RBG0Transform;
    setObjectName(outlines ? "vdp1OutlineViewer" : rotation ? "rbg0TransformViewer" : vdp1 ? "vdp1LayerViewer" : "layerViewer");
    setWindowTitle(outlines ? tr("VDP1 Sprite Outline Viewer") : rotation ? tr("RBG0 Transform Viewer") : vdp1 ? tr("VDP1 Layer Viewer") : tr("VDP2 Layer Viewer"));
    setModal(false);
    resize(rotation ? QSize(820, 680) : outlines ? QSize(760, 640) : vdp1 ? QSize(520, 450) : QSize(960, 620));

    QVBoxLayout *layout = new QVBoxLayout(this);
    QLabel *description = new QLabel(outlines
        ? tr("Green = distorted sprite edges (A-B-C-D-A). Independent of View > Layer. Checkerboard = transparent.")
        : rotation
        ? tr("Source-coordinate grid after RBG0 transformation. A = cyan, B = orange. Independent of View > Layer.")
        : tr("Each layer is shown independently of View > Layer. Checkerboard = transparent."), this);
    description->setWordWrap(true);
    layout->addWidget(description);

    if (outlines)
    {
        QHBoxLayout *options = new QHBoxLayout;
        QLabel *modeLabel = new QLabel(tr("Display:"), this);
        mOutlineMode = new QComboBox(this);
        mOutlineMode->setObjectName("vdp1OutlineMode");
        mOutlineMode->addItems(QStringList() << tr("Image + outlines") << tr("Outlines only") << tr("Image only"));
        modeLabel->setBuddy(mOutlineMode);
        options->addWidget(modeLabel);
        options->addWidget(mOutlineMode);
        options->addStretch();
        layout->addLayout(options);
        mOutlines = new SpriteOutlineWidget(this);
        mOutlines->setObjectName("previewVDP1Outlines");
        layout->addWidget(mOutlines, 1);
        connect(mOutlineMode, SIGNAL(currentIndexChanged(int)), this, SLOT(updateOutlineView()));
    }
    else if (rotation)
    {
        QHBoxLayout *options = new QHBoxLayout;
        QLabel *modeLabel = new QLabel(tr("Display:"), this);
        mTransformMode = new QComboBox(this);
        mTransformMode->setObjectName("rbg0DisplayMode");
        mTransformMode->addItems(QStringList() << tr("Image + grid") << tr("Transformed grid") << tr("Parameter A/B"));
        modeLabel->setBuddy(mTransformMode);
        options->addWidget(modeLabel);
        options->addWidget(mTransformMode);
        QLabel *spacingLabel = new QLabel(tr("Source grid:"), this);
        mGridSpacing = new QComboBox(this);
        mGridSpacing->setObjectName("rbg0GridSpacing");
        const int spacings[] = { 16, 32, 64, 128, 256 };
        for (int i = 0; i < 5; ++i) mGridSpacing->addItem(tr("%1 px").arg(spacings[i]), spacings[i]);
        mGridSpacing->setCurrentIndex(2);
        spacingLabel->setBuddy(mGridSpacing);
        options->addWidget(spacingLabel);
        options->addWidget(mGridSpacing);
        options->addStretch();
        layout->addLayout(options);
        QLabel *detail = new QLabel(this);
        detail->setObjectName("rbg0PointDetail");
        detail->setWordWrap(true);
        detail->setMinimumHeight(fontMetrics().height() * 3);
        detail->setTextInteractionFlags(Qt::TextSelectableByMouse);
        mTransform = new Rbg0TransformWidget(detail, this);
        mTransform->setObjectName("previewRBG0Transform");
        layout->addWidget(mTransform, 1);
        layout->addWidget(detail);
        connect(mTransformMode, SIGNAL(currentIndexChanged(int)), this, SLOT(updateTransformView()));
        connect(mGridSpacing, SIGNAL(currentIndexChanged(int)), this, SLOT(updateTransformView()));
    }
    else
    {
        QGridLayout *grid = new QGridLayout;
        const char *names[6] = { "NBG0", "NBG1", "NBG2", "NBG3", "RBG0", "RBG1" };
        const int count = vdp1 ? 1 : 6;
        for (int i = 0; i < count; ++i)
        {
            const QString name = QString::fromLatin1(vdp1 ? "VDP1" : names[i]);
            QGroupBox *group = new QGroupBox(name, this);
            group->setObjectName(QString::fromLatin1("layerGroup%1").arg(i));
            QVBoxLayout *groupLayout = new QVBoxLayout(group);
            mViews[i] = new LayerPreviewWidget(group);
            mViews[i]->setObjectName(QString::fromLatin1("preview%1").arg(name));
            groupLayout->addWidget(mViews[i]);
            grid->addWidget(group, i / 3, i % 3);
        }
        for (int i = 0; i < (vdp1 ? 1 : 3); ++i) grid->setColumnStretch(i, 1);
        for (int i = 0; i < (vdp1 ? 1 : 2); ++i) grid->setRowStretch(i, 1);
        layout->addLayout(grid, 1);
    }

    if (vdp1)
    {
        mSpriteCount = new QLabel(tr("Distorted sprites: -"), this);
        mSpriteCount->setObjectName("vdp1DistortedSpriteCount");
        mSpriteCount->setToolTip(tr("Each distorted sprite draw counts as one. Updated with the preview."));
        layout->addWidget(mSpriteCount);
    }

    QLabel *note = new QLabel(outlines
        ? tr("Edges include transparent and covered sprites, with clipping applied. Normal/scaled sprites and polygons have no outline. Software preview of the current command list.")
        : rotation
        ? tr("Software sampling before blending. Grid includes transparent texels. Source = before wrapping; Map = before tile lookup. kx/ky are source scale factors.")
        : vdp1
        ? tr("Software preview of the current VDP1 command list.")
        : tr("Software preview before layer blending. Unused layers remain transparent."), this);
    note->setWordWrap(true);
    layout->addWidget(note);

    QHBoxLayout *controls = new QHBoxLayout;
    mAutoRefresh = new QCheckBox(tr("Auto refresh"), this);
    mAutoRefresh->setObjectName("layerAutoRefresh");
    mAutoRefresh->setChecked(true);
    controls->addWidget(mAutoRefresh);
    QPushButton *refreshButton = new QPushButton(tr("Refresh"), this);
    controls->addWidget(refreshButton);
    mStatus = new QLabel(this);
    mStatus->setObjectName("layerViewerStatus");
    mStatus->setWordWrap(true);
    controls->addWidget(mStatus, 1);
    layout->addLayout(controls);

    connect(refreshButton, SIGNAL(clicked()), this, SLOT(refresh()));
    connect(mAutoRefresh, SIGNAL(toggled(bool)), this, SLOT(refresh()));
    source->addViewer(this);
}

void UILayerViewer::showEvent(QShowEvent *event)
{
    QDialog::showEvent(event);
    emit visibilityChanged(true);
    refresh();
}

void UILayerViewer::hideEvent(QHideEvent *event)
{
    QDialog::hideEvent(event);
    emit visibilityChanged(false);
    if (mSource) mSource->updateTimer();
}

void UILayerViewer::refresh()
{
    if (isVisible() && mSource) mSource->refreshViewer(this);
}

bool UILayerViewer::autoRefresh() const
{
    return isVisible() && mAutoRefresh->isChecked();
}

void UILayerViewer::updateTransformView()
{
    if (mTransform)
        mTransform->setView(mTransformMode->currentIndex(), mGridSpacing->itemData(mGridSpacing->currentIndex()).toInt());
}

void UILayerViewer::displayImages(const QImage *images, u32 distortedSpriteCount, const VIDSoftRbg0Preview &rotation,
    const QVector<VIDSoftRbg0Sample> &samples, const QImage &outlines)
{
    if (mKind == RBG0Transform)
    {
        mTransform->setSnapshot(images[5], samples);
        const char *modes[] = { "A", "B", "A/B coefficient", "A/B window" };
        mStatus->setText(images[5].isNull() ? tr("Start emulation to view RBG0.")
            : !rotation.enabled ? tr("RBG0 is disabled by the game.")
            : tr("%1 x %2 | RPMD %3 (%4) | Coefficients A: %5, B: %6")
                .arg(images[5].width()).arg(images[5].height()).arg(rotation.mode)
                .arg(QString::fromLatin1(modes[rotation.mode & 3]))
                .arg(rotation.coefficient_enabled[0] ? tr("on") : tr("off"))
                .arg(rotation.coefficient_enabled[1] ? tr("on") : tr("off")));
        return;
    }
    const bool vdp1 = mKind == VDP1 || mKind == VDP1Outlines;
    const int count = vdp1 ? 1 : 6;
    const int first = vdp1 ? 0 : 1;
    if (mOutlines) mOutlines->setSnapshot(images[0], outlines);
    else for (int i = 0; i < count; ++i) mViews[i]->setImage(images[first + i]);
    const QImage &image = images[first];
    if (mSpriteCount)
        mSpriteCount->setText(image.isNull() ? tr("Distorted sprites: -")
            : tr("Distorted sprites: %1").arg(distortedSpriteCount));
    mStatus->setText(image.isNull() ? tr("Start emulation to view layers.")
        : tr("%1 x %2 per layer").arg(image.width()).arg(image.height()));
}

void UILayerViewer::updateOutlineView()
{
    if (mOutlines) mOutlines->setMode(mOutlineMode->currentIndex());
}
