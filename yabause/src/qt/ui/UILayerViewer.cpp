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

UILayerViewer::UILayerViewer(YabauseThread *emulation, QWidget *parent)
    : QDialog(parent, Qt::Window), mEmulation(emulation)
{
    setObjectName("layerViewer");
    setWindowTitle(tr("Layer Viewer"));
    setModal(false);
    resize(960, 620);

    QVBoxLayout *layout = new QVBoxLayout(this);
    QLabel *description = new QLabel(tr("Each layer is shown independently of View > Layer. Checkerboard = transparent."), this);
    description->setWordWrap(true);
    layout->addWidget(description);

    QGridLayout *grid = new QGridLayout;
    const char *names[6] = { "VDP1", "NBG0", "NBG1", "NBG2", "NBG3", "RBG0" };
    for (int i = 0; i < 6; ++i)
    {
        QGroupBox *group = new QGroupBox(QString::fromLatin1(names[i]), this);
        mGroups[i] = group;
        group->setObjectName(QString::fromLatin1("layerGroup%1").arg(i));
        QVBoxLayout *groupLayout = new QVBoxLayout(group);
        mViews[i] = new LayerPreviewWidget(group);
        mViews[i]->setObjectName(QString::fromLatin1("preview%1").arg(names[i]));
        groupLayout->addWidget(mViews[i]);
        grid->addWidget(group, i / 3, i % 3);
    }
    for (int i = 0; i < 3; ++i) grid->setColumnStretch(i, 1);
    for (int i = 0; i < 2; ++i) grid->setRowStretch(i, 1);
    layout->addLayout(grid, 1);

    QLabel *note = new QLabel(tr("Software preview before layer blending. VDP1 shows the current command list."), this);
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

    mTimer = new QTimer(this);
    mTimer->setSingleShot(true);
    connect(mTimer, SIGNAL(timeout()), this, SLOT(refresh()));
    connect(refreshButton, SIGNAL(clicked()), this, SLOT(refresh()));
    connect(mAutoRefresh, SIGNAL(toggled(bool)), this, SLOT(refresh()));
}

void UILayerViewer::showEvent(QShowEvent *event)
{
    QDialog::showEvent(event);
    emit visibilityChanged(true);
    refresh();
}

void UILayerViewer::hideEvent(QHideEvent *event)
{
    mTimer->stop();
    QDialog::hideEvent(event);
    emit visibilityChanged(false);
}

void UILayerViewer::refresh()
{
    mTimer->stop();
    if (!isVisible()) return;
    QElapsedTimer elapsed;
    elapsed.start();
    int width = 0, height = 0;
    bool available = mEmulation->init() == 0;
    if (available)
    {
        u32 *layers[6];
        for (int i = 0; i < 6; ++i)
        {
            mPixels[i].resize(704 * 512);
            layers[i] = mPixels[i].data();
        }
        // Emulation and this timer run on the GUI thread. Capture between
        // frames, not from swapBuffers while a renderer is still on the stack.
        available = VIDSoftRenderLayerPreviews(layers, &width, &height) == 0;
    }
    // RBG1 shares NBG0's renderer, but belongs in the rotation preview slot.
    // NBG0 is unavailable in RBG1 mode, so its slot can show a concurrent RBG0.
    int sources[6] = { 0, 1, 2, 3, 4, 5 };
    const char *names[6] = { "VDP1", "NBG0", "NBG1", "NBG2", "NBG3", "RBG0" };
    if (available && (Vdp2Regs->BGON & 0x20))
    {
        sources[1] = 5;
        sources[5] = 1;
        names[1] = (Vdp2Regs->BGON & 0x10) ? "RBG0" : "NBG0";
        names[5] = "RBG1";
    }
    for (int i = 0; i < 6; ++i)
    {
        mGroups[i]->setTitle(QString::fromLatin1(names[i]));
        mViews[i]->setObjectName(QString::fromLatin1("preview%1").arg(names[i]));
        mViews[i]->setImage(available
            ? QImage(reinterpret_cast<const uchar *>(mPixels[sources[i]].constData()),
                width, height, width * 4, QImage::Format_ARGB32).copy()
            : QImage());
    }
    mStatus->setText(available ? tr("%1 x %2 per layer").arg(width).arg(height)
                              : tr("Start emulation to view layers."));
    // At most five captures per second; slow captures reduce their own rate.
    if (mAutoRefresh->isChecked())
        mTimer->start(qMax(200, int(elapsed.elapsed() * 4)));
}
