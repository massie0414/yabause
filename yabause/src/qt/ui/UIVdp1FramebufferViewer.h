/* This file is part of Yabause, licensed under GPL-2.0-or-later. */
#ifndef UIVDP1FRAMEBUFFERVIEWER_H
#define UIVDP1FRAMEBUFFERVIEWER_H

#include <QDialog>

class YabauseThread;
class Vdp1FramebufferView;
class QCheckBox;
class QLabel;
class QPushButton;
class QTimer;

class UIVdp1FramebufferViewer : public QDialog
{
    Q_OBJECT
public:
    UIVdp1FramebufferViewer(YabauseThread *emulation, QWidget *parent = 0);

signals:
    void visibilityChanged(bool visible);

protected:
    void showEvent(QShowEvent *event);
    void hideEvent(QHideEvent *event);

private slots:
    void refresh();
    void updateTimer();
    void saveDisplay();
    void saveDrawing();

private:
    void saveBuffer(int buffer);
    YabauseThread *mEmulation;
    Vdp1FramebufferView *mViews[2];
    QPushButton *mSave[2];
    QCheckBox *mAutoRefresh;
    QLabel *mStatus;
    QTimer *mTimer;
};

#endif
