/* This file is part of Yabause, licensed under GPL-2.0-or-later. */
#include "UILayerViewer.h"
#include "../YabauseThread.h"

#include <QCheckBox>
#include <QElapsedTimer>
#include <QGridLayout>
#include <QGroupBox>
#include <QHideEvent>
#include <QImage>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QShowEvent>
#include <QTimer>

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
    void paintEvent(QPaintEvent *)
    {
        QPainter painter(this);
        painter.fillRect(rect(), QColor(24, 26, 30));
        if (mImage.isNull()) return;
        const QSize size = mImage.size().scaled(this->size(), Qt::KeepAspectRatio);
        const QRect target(QPoint((width() - size.width()) / 2,
                                  (height() - size.height()) / 2), size);
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

private:
    QImage mImage;
};

UILayerPreviewSource::UILayerPreviewSource(YabauseThread *emulation, QObject *parent)
    : QObject(parent), mEmulation(emulation), mDistortedSpriteCount(0), mTimer(new QTimer(this))
{
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
        available = VIDSoftRenderLayerPreviews(layers, &width, &height, &mDistortedSpriteCount) == 0;
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
        if (viewer && viewer->autoRefresh()) viewer->displayImages(mImages, mDistortedSpriteCount);
    updateTimer(delay);
}

void UILayerPreviewSource::refreshViewer(UILayerViewer *viewer)
{
    mTimer->stop();
    const int delay = capture();
    viewer->displayImages(mImages, mDistortedSpriteCount);
    updateTimer(delay);
}

UILayerViewer::UILayerViewer(UILayerPreviewSource *source, Kind kind, QWidget *parent)
    : QDialog(parent, Qt::Window), mSource(source), mKind(kind), mSpriteCount(NULL)
{
    const bool vdp1 = mKind == VDP1;
    setObjectName(vdp1 ? "vdp1LayerViewer" : "layerViewer");
    setWindowTitle(vdp1 ? tr("VDP1 Layer Viewer") : tr("VDP2 Layer Viewer"));
    setModal(false);
    resize(vdp1 ? QSize(520, 450) : QSize(960, 620));

    QVBoxLayout *layout = new QVBoxLayout(this);
    QLabel *description = new QLabel(tr("Each layer is shown independently of View > Layer. Checkerboard = transparent."), this);
    description->setWordWrap(true);
    layout->addWidget(description);

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

    if (vdp1)
    {
        mSpriteCount = new QLabel(tr("Distorted sprites: -"), this);
        mSpriteCount->setObjectName("vdp1DistortedSpriteCount");
        mSpriteCount->setToolTip(tr("Each distorted sprite draw counts as one. Updated with the preview."));
        layout->addWidget(mSpriteCount);
    }

    QLabel *note = new QLabel(vdp1
        ? tr("Software preview of the current VDP1 command list.")
        : tr("Software preview before layer blending. Unused layers remain transparent."), this);
    note->setWordWrap(true);
    layout->addWidget(note);

    QHBoxLayout *controls = new QHBoxLayout;
    mAutoRefresh = new QCheckBox(tr("Auto refresh"), this);
    mAutoRefresh->setChecked(true);
    controls->addWidget(mAutoRefresh);
    QPushButton *refreshButton = new QPushButton(tr("Refresh"), this);
    controls->addWidget(refreshButton);
    mStatus = new QLabel(this);
    mStatus->setObjectName("layerViewerStatus");
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

void UILayerViewer::displayImages(const QImage *images, u32 distortedSpriteCount)
{
    const int count = mKind == VDP1 ? 1 : 6;
    const int first = mKind == VDP1 ? 0 : 1;
    for (int i = 0; i < count; ++i) mViews[i]->setImage(images[first + i]);
    const QImage &image = images[first];
    if (mSpriteCount)
        mSpriteCount->setText(image.isNull() ? tr("Distorted sprites: -")
            : tr("Distorted sprites: %1").arg(distortedSpriteCount));
    mStatus->setText(image.isNull() ? tr("Start emulation to view layers.")
        : tr("%1 x %2 per layer").arg(image.width()).arg(image.height()));
}
