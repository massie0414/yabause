/* This file is part of Yabause, licensed under GPL-2.0-or-later. */
#ifndef UIVDP2VRAMVIEWER_H
#define UIVDP2VRAMVIEWER_H

#include <QDialog>
#include <QImage>
#include <QVector>
extern "C" {
#include "../../vdp2vram.h"
}

class YabauseThread;
class QCheckBox;
class QComboBox;
class QGraphicsView;
class QGraphicsPixmapItem;
class QGraphicsRectItem;
class QLabel;
class QPushButton;
class QSpinBox;
class QTimer;
class QTabWidget;

class UIVdp2VramViewer : public QDialog
{
    Q_OBJECT
public:
    UIVdp2VramViewer(YabauseThread *emulation, QWidget *parent = 0);
    // Copies memory so controls can inspect a stable capture or an offline snapshot.
    void setSnapshot(const Vdp2 *regs, const u8 *ram, const u8 *cram);

signals:
    void visibilityChanged(bool visible);

protected:
    void showEvent(QShowEvent *event);
    void hideEvent(QHideEvent *event);
    void resizeEvent(QResizeEvent *event);
    bool eventFilter(QObject *object, QEvent *event);

private slots:
    void refresh();
    void redraw();
    void applyLayer();
    void selectBank();
    void addressChanged();
    void updateTimer();
    void updateZoom();
    void saveImage();
    void redrawCells();
    void updateCellZoom();
    void saveCellImage();

private:
    Vdp2VramConfig config() const;
    void createCellTab();
    QString cellDescription(const Vdp2Cell &cell) const;
    YabauseThread *mEmulation;
    Vdp2 mRegs;
    QVector<u8> mRam, mCram;
    bool mValid, mUpdating, mFirstCapture;
    QImage mImage;
    QComboBox *mLayer, *mLayout, *mColor, *mBank, *mWidth, *mHeight, *mZoom;
    QSpinBox *mAddress, *mPalette;
    QCheckBox *mAutoRefresh, *mTransparency, *mGrayscale;
    QGraphicsView *mView;
    QGraphicsPixmapItem *mPixmap;
    QLabel *mStatus, *mHover, *mPresetInfo;
    QPushButton *mSave;
    QTimer *mTimer;
    QTabWidget *mTabs;
    QComboBox *mPlane, *mMapZoom, *mTileZoom;
    QCheckBox *mCellGrid, *mCellTransparency;
    QGraphicsView *mMapView, *mTileView;
    QGraphicsPixmapItem *mMapPixmap, *mTilePixmap;
    QGraphicsRectItem *mTileSelection;
    QLabel *mCellInfo, *mCellHover;
    QPushButton *mSaveMap, *mSaveTiles;
    Vdp2CellMap mCellMap;
    QImage mMapImage, mTileImage;
    QVector<Vdp2Cell> mCells;
    QVector<int> mCellIndices;
    int mMapOriginX, mMapOriginY;
};

#endif
