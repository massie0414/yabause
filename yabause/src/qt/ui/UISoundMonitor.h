/* This file is part of Yabause, licensed under GPL-2.0-or-later. */
#ifndef UISOUNDMONITOR_H
#define UISOUNDMONITOR_H

#include <QDialog>
#include <QElapsedTimer>

class YabauseThread;
class QLabel;
class QProgressBar;
class QTimer;

class UISoundMonitor : public QDialog
{
    Q_OBJECT
public:
    UISoundMonitor(YabauseThread *emulation, QWidget *parent = 0);

signals:
    void visibilityChanged(bool visible);

protected:
    void showEvent(QShowEvent *event);
    void hideEvent(QHideEvent *event);

private slots:
    void refresh();

private:
    void updateMeter(int index, unsigned int peak, bool available, double seconds);
    YabauseThread *mEmulation;
    QTimer *mTimer;
    QElapsedTimer mElapsed;
    QProgressBar *mMeters[3];
    QLabel *mActivity[3];
    QLabel *mDspState, *mDspProgram, *mStatus;
    double mLevels[3];
};

#endif
