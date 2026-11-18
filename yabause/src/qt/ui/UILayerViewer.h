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
class QCheckBox;
class QLabel;
class QTimer;
class UILayerViewer;

// One capture supplies both windows on each automatic refresh.
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
    QTimer *mTimer;
};

class UILayerViewer : public QDialog
{
    Q_OBJECT
public:
    enum Kind { VDP1, VDP2 };
    UILayerViewer(UILayerPreviewSource *source, Kind kind, QWidget *parent = 0);

signals:
    void visibilityChanged(bool visible);

protected:
    void showEvent(QShowEvent *event);
    void hideEvent(QHideEvent *event);

private slots:
    void refresh();

private:
    friend class UILayerPreviewSource;
    bool autoRefresh() const;
    void displayImages(const QImage *images);
    QPointer<UILayerPreviewSource> mSource;
    Kind mKind;
    LayerPreviewWidget *mViews[6];
    QCheckBox *mAutoRefresh;
    QLabel *mStatus;
};

#endif
