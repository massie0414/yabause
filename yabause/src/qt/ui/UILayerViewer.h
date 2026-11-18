/* This file is part of Yabause, licensed under GPL-2.0-or-later. */
#ifndef UILAYERVIEWER_H
#define UILAYERVIEWER_H

#include <QDialog>
#include <QImage>
#include <QList>
#include <QPointer>
#include <QVector>

extern "C" {
#include "../../vidsoft.h"
}

class YabauseThread;
class LayerPreviewWidget;
class Rbg0TransformWidget;
class QCheckBox;
class QComboBox;
class QLabel;
class QTimer;
class UILayerViewer;

// One capture supplies all windows on each automatic refresh.
class UILayerPreviewSource : public QObject
{
    Q_OBJECT
public:
    UILayerPreviewSource(YabauseThread *emulation, QObject *parent);
    void addViewer(UILayerViewer *viewer);
    void refreshViewer(UILayerViewer *viewer);
    void updateTimer(int delay = 200);

private slots:
    void refreshAutomatic();

private:
    int capture();
    bool needsRefresh() const;
    YabauseThread *mEmulation;
    QList<QPointer<UILayerViewer> > mViewers;
    QVector<quint32> mPixels[VIDSOFT_LAYER_PREVIEW_COUNT];
    QImage mImages[VIDSOFT_LAYER_PREVIEW_COUNT];
    u32 mDistortedSpriteCount;
    QVector<VIDSoftRbg0Sample> mRotationSamples;
    VIDSoftRbg0Preview mRotation;
    QTimer *mTimer;
};

class UILayerViewer : public QDialog
{
    Q_OBJECT
public:
    enum Kind { VDP1, VDP2, RBG0Transform };
    UILayerViewer(UILayerPreviewSource *source, Kind kind, QWidget *parent = 0);

signals:
    void visibilityChanged(bool visible);

protected:
    void showEvent(QShowEvent *event);
    void hideEvent(QHideEvent *event);

private slots:
    void refresh();
    void updateTransformView();

private:
    friend class UILayerPreviewSource;
    bool autoRefresh() const;
    void displayImages(const QImage *images, u32 distortedSpriteCount, const VIDSoftRbg0Preview &rotation,
        const QVector<VIDSoftRbg0Sample> &samples);
    QPointer<UILayerPreviewSource> mSource;
    Kind mKind;
    LayerPreviewWidget *mViews[6];
    QCheckBox *mAutoRefresh;
    QLabel *mSpriteCount;
    QLabel *mStatus;
    Rbg0TransformWidget *mTransform;
    QComboBox *mTransformMode;
    QComboBox *mGridSpacing;
};

#endif
