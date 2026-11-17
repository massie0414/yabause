/* This file is part of Yabause, licensed under GPL-2.0-or-later. */
#ifndef UILAYERVIEWER_H
#define UILAYERVIEWER_H

#include <QDialog>
#include <QVector>

class YabauseThread;
class LayerPreviewWidget;
class QCheckBox;
class QLabel;
class QTimer;

class UILayerViewer : public QDialog
{
    Q_OBJECT
public:
    UILayerViewer(YabauseThread *emulation, QWidget *parent = 0);

signals:
    void visibilityChanged(bool visible);

protected:
    void showEvent(QShowEvent *event);
    void hideEvent(QHideEvent *event);

private slots:
    void refresh();

private:
    YabauseThread *mEmulation;
    LayerPreviewWidget *mViews[6];
    QVector<quint32> mPixels[6];
    QCheckBox *mAutoRefresh;
    QLabel *mStatus;
    QTimer *mTimer;
};

#endif
