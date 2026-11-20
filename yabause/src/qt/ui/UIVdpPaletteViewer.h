/* This file is part of Yabause, licensed under GPL-2.0-or-later. */
#ifndef UIVDPPALETTEVIEWER_H
#define UIVDPPALETTEVIEWER_H

#include <QDialog>
#include <QImage>
#include <QVector>
extern "C" {
#include "../../vdppalette.h"
}

class YabauseThread;
class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;
class QTableWidget;
class QTimer;

class UIVdpPaletteViewer : public QDialog
{
    Q_OBJECT
public:
    UIVdpPaletteViewer(YabauseThread *emulation, bool vdp1, QWidget *parent = 0);
    // Also supports offline snapshots. All memory is copied before returning.
    void setSnapshot(const Vdp2 *regs, const u8 *ram, const u8 *cram);

signals:
    void visibilityChanged(bool visible);

protected:
    void showEvent(QShowEvent *event);
    void hideEvent(QHideEvent *event);

private slots:
    void refresh();
    void updateTimer();
    void selectSource();
    void redraw();
    void selectColor();
    void saveImage();

private:
    YabauseThread *mEmulation;
    bool mVdp1, mValid, mFirstCapture;
    int mWalkStatus;
    Vdp2 mRegs;
    QVector<u8> mRam, mCram;
    QVector<VdpPaletteSource> mSources;
    QComboBox *mSource, *mBank;
    QTableWidget *mGrid;
    QLabel *mStatus, *mDetails;
    QCheckBox *mAutoRefresh;
    QPushButton *mSave;
    QTimer *mTimer;
    QImage mImage;
};

#endif
