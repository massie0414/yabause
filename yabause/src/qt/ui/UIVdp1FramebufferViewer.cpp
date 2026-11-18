/* This file is part of Yabause, licensed under GPL-2.0-or-later. */
#include "UIVdp1FramebufferViewer.h"
#include "../YabauseThread.h"
#include "../CommonDialogs.h"

#include <QCheckBox>
#include <QFileDialog>
#include <QGroupBox>
#include <QHelpEvent>
#include <QHideEvent>
#include <QImage>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QShowEvent>
#include <QTimer>
#include <QToolTip>
#include <QVBoxLayout>
#include <QVector>

extern "C" {
#include "../../vidsoft.h"
}

class Vdp1FramebufferView : public QWidget
{
public:
    Vdp1FramebufferView(QWidget *parent) : QWidget(parent), mPixelSize(0)
    {
        setMinimumSize(256, 160);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    }

    void capture(int back)
    {
        QVector<quint32> pixels(VIDSOFT_VDP1_FRAMEBUFFER_MAX_PIXELS);
        mRaw.resize(VIDSOFT_VDP1_FRAMEBUFFER_MAX_PIXELS);
        int width, height;
        if (VIDSoftGetVdp1Framebuffer(back, pixels.data(), mRaw.data(),
            &width, &height, &mPixelSize) == 0)
            mImage = QImage(reinterpret_cast<const uchar *>(pixels.constData()),
                width, height, width * 4, QImage::Format_ARGB32).copy();
        else
            clear();
        update();
    }

    void clear()
    {
        mImage = QImage();
        mRaw.clear();
        mPixelSize = 0;
        update();
    }

    const QImage &image() const { return mImage; }
    int pixelSize() const { return mPixelSize; }

protected:
    void paintEvent(QPaintEvent *)
    {
        QPainter painter(this);
        painter.fillRect(rect(), QColor(24, 26, 30));
        if (mImage.isNull()) return;
        const QRect target = imageRect();
        painter.setClipRect(target);
        for (int y = target.top(); y <= target.bottom(); y += 12)
            for (int x = target.left(); x <= target.right(); x += 12)
                painter.fillRect(x, y, 12, 12,
                    ((x - target.left()) / 12 + (y - target.top()) / 12) % 2
                    ? QColor(62, 65, 70) : QColor(48, 51, 56));
        painter.drawImage(target, mImage);
    }

    bool event(QEvent *event)
    {
        if (event->type() == QEvent::ToolTip)
        {
            QHelpEvent *help = static_cast<QHelpEvent *>(event);
            const QRect target = imageRect();
            if (!mImage.isNull() && target.contains(help->pos()))
            {
                const int x = (help->pos().x() - target.x()) * mImage.width() / target.width();
                const int y = (help->pos().y() - target.y()) * mImage.height() / target.height();
                const int index = y * mImage.width() + x;
                const QString tip = tr("(%1, %2)\nBuffer offset: 0x%3\nPixel: 0x%4")
                    .arg(x).arg(y)
                    .arg(index * mPixelSize, 5, 16, QLatin1Char('0'))
                    .arg(mRaw[index], mPixelSize * 2, 16, QLatin1Char('0'));
                QToolTip::showText(help->globalPos(), tip, this);
            }
            else
                QToolTip::hideText();
            return true;
        }
        return QWidget::event(event);
    }

private:
    QRect imageRect() const
    {
        const QSize size = mImage.size().scaled(this->size(), Qt::KeepAspectRatio);
        return QRect(QPoint((width() - size.width()) / 2,
                           (height() - size.height()) / 2), size);
    }
    QImage mImage;
    QVector<quint16> mRaw;
    int mPixelSize;
};

UIVdp1FramebufferViewer::UIVdp1FramebufferViewer(YabauseThread *emulation, QWidget *parent)
    : QDialog(parent, Qt::Window), mEmulation(emulation), mTimer(new QTimer(this))
{
    setObjectName("vdp1FramebufferViewer");
    setWindowTitle(tr("VDP1 Framebuffer Viewer"));
    setModal(false);
    resize(1100, 470);
    QVBoxLayout *layout = new QVBoxLayout(this);
    QLabel *description = new QLabel(tr("Live VDP1 memory, including retained pixels and CPU writes. Hover over a pixel to inspect its value."), this);
    description->setWordWrap(true);
    layout->addWidget(description);
    QHBoxLayout *buffers = new QHBoxLayout;
    for (int i = 0; i < 2; ++i)
    {
        QGroupBox *group = new QGroupBox(i == 0
            ? tr("Display buffer (front)") : tr("Drawing buffer (back / CPU)"), this);
        QVBoxLayout *groupLayout = new QVBoxLayout(group);
        mViews[i] = new Vdp1FramebufferView(group);
        mViews[i]->setObjectName(i == 0 ? "displayFramebuffer" : "drawingFramebuffer");
        groupLayout->addWidget(mViews[i], 1);
        mSave[i] = new QPushButton(tr("Save PNG..."), group);
        mSave[i]->setEnabled(false);
        groupLayout->addWidget(mSave[i]);
        buffers->addWidget(group, 1);
    }
    layout->addLayout(buffers, 1);
    QLabel *note = new QLabel(tr("Full buffer, before VDP2 rotation, clipping and blending. Colors use current RGB / palette settings. Checkerboard = zero; magenta = shadow / window marker."), this);
    note->setWordWrap(true);
    layout->addWidget(note);
    QHBoxLayout *controls = new QHBoxLayout;
    mAutoRefresh = new QCheckBox(tr("Auto refresh"), this);
    mAutoRefresh->setChecked(true);
    controls->addWidget(mAutoRefresh);
    QPushButton *refreshButton = new QPushButton(tr("Refresh"), this);
    controls->addWidget(refreshButton);
    mStatus = new QLabel(this);
    mStatus->setObjectName("framebufferStatus");
    mStatus->setWordWrap(true);
    controls->addWidget(mStatus, 1);
    layout->addLayout(controls);
    mTimer->setInterval(200);
    connect(mTimer, SIGNAL(timeout()), this, SLOT(refresh()));
    connect(refreshButton, SIGNAL(clicked()), this, SLOT(refresh()));
    connect(mAutoRefresh, SIGNAL(toggled(bool)), this, SLOT(updateTimer()));
    connect(mSave[0], SIGNAL(clicked()), this, SLOT(saveDisplay()));
    connect(mSave[1], SIGNAL(clicked()), this, SLOT(saveDrawing()));
}

void UIVdp1FramebufferViewer::showEvent(QShowEvent *event)
{
    QDialog::showEvent(event);
    emit visibilityChanged(true);
    refresh();
    updateTimer();
}

void UIVdp1FramebufferViewer::hideEvent(QHideEvent *event)
{
    QDialog::hideEvent(event);
    emit visibilityChanged(false);
    updateTimer();
}

void UIVdp1FramebufferViewer::updateTimer()
{
    if (isVisible() && mAutoRefresh->isChecked()) mTimer->start();
    else mTimer->stop();
}

void UIVdp1FramebufferViewer::refresh()
{
    if (!isVisible()) return;
    // The GUI thread runs emulation and this timer between frames. The snapshot
    // waits for the software VDP1 worker before reading either live buffer.
    const bool running = mEmulation->init() == 0;
    for (int i = 0; i < 2; ++i)
    {
        if (running) mViews[i]->capture(i);
        else mViews[i]->clear();
        mSave[i]->setEnabled(!mViews[i]->image().isNull());
    }
    const QImage &image = mViews[0]->image();
    if (!running)
        mStatus->setText(tr("Start emulation to view framebuffers."));
    else if (VIDCore != &VIDSoft)
        mStatus->setText(tr("Select the Software Video Interface to view live framebuffers."));
    else if (image.isNull())
        mStatus->setText(tr("Framebuffers are unavailable."));
    else
        mStatus->setText(tr("%1 x %2, %3-bit | TVMR: 0x%4  FBCR: 0x%5")
            .arg(image.width()).arg(image.height()).arg(mViews[0]->pixelSize() * 8)
            .arg(Vdp1Regs->TVMR, 4, 16, QLatin1Char('0'))
            .arg(Vdp1Regs->FBCR, 4, 16, QLatin1Char('0')));
}

void UIVdp1FramebufferViewer::saveDisplay() { saveBuffer(0); }
void UIVdp1FramebufferViewer::saveDrawing() { saveBuffer(1); }

void UIVdp1FramebufferViewer::saveBuffer(int buffer)
{
    // Keep the selected snapshot stable while the modal file dialog runs.
    const QImage image = mViews[buffer]->image();
    if (image.isNull()) return;
    QString path = QFileDialog::getSaveFileName(this, tr("Save VDP1 framebuffer"),
        buffer == 0 ? "vdp1-display.png" : "vdp1-drawing.png", tr("PNG Images (*.png)"));
    if (path.isEmpty()) return;
    if (!path.endsWith(".png", Qt::CaseInsensitive)) path += ".png";
    if (!image.save(path, "PNG"))
        CommonDialogs::information(tr("An error occurred while writing the image."));
}
