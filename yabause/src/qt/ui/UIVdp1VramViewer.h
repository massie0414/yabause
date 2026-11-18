/* This file is part of Yabause, licensed under GPL-2.0-or-later. */
#ifndef UIVDP1VRAMVIEWER_H
#define UIVDP1VRAMVIEWER_H

#include <QDialog>
#include <QImage>

class YabauseThread;
class Vdp1TextureModel;
class Vdp1ImageView;
class QListView;
class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;
class QSpinBox;
class QTabWidget;
class QTimer;

class UIVdp1VramViewer : public QDialog
{
    Q_OBJECT
public:
    UIVdp1VramViewer(YabauseThread *emulation, QWidget *parent = 0);

signals:
    void visibilityChanged(bool visible);

protected:
    void showEvent(QShowEvent *event);
    void hideEvent(QHideEvent *event);

private slots:
    void refresh();
    void updateTimer();
    void selectTexture();
    void updateRaw();
    void previousPage();
    void nextPage();
    void inspectTexture();
    void saveImage();

private:
    YabauseThread *mEmulation;
    Vdp1TextureModel *mModel;
    QListView *mTextures;
    Vdp1ImageView *mTextureView;
    Vdp1ImageView *mRawView;
    QLabel *mDetails;
    QLabel *mStatus;
    QLabel *mRawStatus;
    QTabWidget *mTabs;
    QCheckBox *mAutoRefresh;
    QCheckBox *mTransparency;
    QComboBox *mMode;
    QSpinBox *mAddress;
    QSpinBox *mWidth;
    QSpinBox *mHeight;
    QSpinBox *mColor;
    QPushButton *mSave;
    QPushButton *mInspect;
    QTimer *mTimer;
    QImage mTextureImage;
    QImage mRawImage;
};

#endif
