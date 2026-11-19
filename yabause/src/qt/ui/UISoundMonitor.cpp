/* This file is part of Yabause, licensed under GPL-2.0-or-later. */
#include "UISoundMonitor.h"
#include "../YabauseThread.h"

#include <QGridLayout>
#include <QHideEvent>
#include <QLabel>
#include <QProgressBar>
#include <QShowEvent>
#include <QTimer>
#include <cmath>

UISoundMonitor::UISoundMonitor(YabauseThread *emulation, QWidget *parent)
    : QDialog(parent, Qt::Window), mEmulation(emulation)
{
    setObjectName("soundMonitor");
    setWindowTitle(tr("Sound Monitor"));
    resize(520, 270);
    QVBoxLayout *layout = new QVBoxLayout(this);
    QLabel *title = new QLabel(tr("SCSP sound activity"), this);
    QFont font = title->font();
    font.setBold(true);
    title->setFont(font);
    layout->addWidget(title);

    QGridLayout *grid = new QGridLayout;
    const char *names[] = { "PCM", "FM", "DSP output" };
    const char *objects[] = { "pcmMeter", "fmMeter", "dspMeter" };
    const char *colors[] = { "#29965c", "#397fcb", "#9562bd" };
    for (int i = 0; i < 3; ++i)
    {
        grid->addWidget(new QLabel(tr(names[i]), this), i, 0);
        mMeters[i] = new QProgressBar(this);
        mMeters[i]->setObjectName(objects[i]);
        mMeters[i]->setRange(0, 960);
        mMeters[i]->setMinimumWidth(190);
        mMeters[i]->setStyleSheet(QString("QProgressBar { border: 1px solid #929292; border-radius: 3px; text-align: center; } "
            "QProgressBar::chunk { background-color: %1; }").arg(colors[i]));
        grid->addWidget(mMeters[i], i, 1);
        mActivity[i] = new QLabel(this);
        mActivity[i]->setObjectName(QString(objects[i]) + "Activity");
        mActivity[i]->setMinimumWidth(75);
        grid->addWidget(mActivity[i], i, 2);
        mLevels[i] = -96.0;
        updateMeter(i, 0, false, 0);
    }
    grid->setColumnStretch(1, 1);
    layout->addLayout(grid);

    QHBoxLayout *dsp = new QHBoxLayout;
    dsp->addWidget(new QLabel(tr("DSP:"), this));
    mDspState = new QLabel(tr("Waiting"), this);
    mDspState->setObjectName("dspState");
    dsp->addWidget(mDspState);
    mDspProgram = new QLabel(this);
    mDspProgram->setObjectName("dspProgram");
    dsp->addWidget(mDspProgram, 1);
    layout->addLayout(dsp);

    QLabel *description = new QLabel(tr("PCM: slots without waveform modulation. FM: slots with waveform modulation.\n"
        "Source levels before master volume / mute; CD audio is excluded."), this);
    description->setWordWrap(true);
    layout->addWidget(description);
    mMeters[0]->setToolTip(tr("Peak of the loudest unmodulated slot after envelope and send levels. Includes DSP sends."));
    mMeters[1]->setToolTip(tr("Peak of the loudest modulated slot (MDL > 0), including DSP sends. This is a register-based classification."));
    mMeters[2]->setToolTip(tr("Peak of the 16 DSP effect returns after effect send level. External CD audio inputs are excluded."));
    mDspState->setToolTip(tr("Enabled means the sound core executes the DSP. A loaded program and nonzero output are shown separately."));
    mStatus = new QLabel(tr("Start emulation to see sound activity."), this);
    mStatus->setObjectName("soundMonitorStatus");
    mStatus->setWordWrap(true);
    layout->addWidget(mStatus);
    layout->addStretch();

    mTimer = new QTimer(this);
    mTimer->setInterval(100);
    connect(mTimer, SIGNAL(timeout()), this, SLOT(refresh()));
    if (mEmulation)
    {
        connect(mEmulation, SIGNAL(pause(bool)), this, SLOT(refresh()));
        connect(mEmulation, SIGNAL(reset()), this, SLOT(refresh()));
    }
}

void UISoundMonitor::updateMeter(int index, unsigned int peak, bool available, double seconds)
{
    // Decay in real time so brief sounds remain visible even between GUI ticks.
    double level = peak ? 20.0 * std::log10(double(peak) / 32768.0) : -96.0;
    mLevels[index] = available ? qMax(qBound(-96.0, level, 0.0), mLevels[index] - seconds * 60.0) : -96.0;
    bool active = available && (peak || mLevels[index] > -96.0);
    mMeters[index]->setEnabled(available);
    mMeters[index]->setValue(active ? qMax(1, int((mLevels[index] + 96.0) * 10.0)) : 0);
    mMeters[index]->setFormat(!available ? tr("N/A") : active ? QString::number(mLevels[index], 'f', 1) + " dB" : tr("Silent"));
    mActivity[index]->setText(!available ? tr("N/A") : active ? tr("Sound") : tr("Silent"));
}

void UISoundMonitor::refresh()
{
    if (!isVisible()) return;
    double seconds = mElapsed.isValid() ? mElapsed.restart() / 1000.0 : 0.1;
    if (!mElapsed.isValid()) mElapsed.start();
    bool ready = mEmulation && mEmulation->init() == 0;
#ifndef USE_SCSP2
    ScspAudioMonitor monitor = {};
    // The Qt emulator and this timer run on the GUI thread, between frames.
    // Reading only consumes diagnostic peaks; it never advances sound state.
    if (ready) ScspReadAudioMonitor(&monitor);
    bool paused = ready && mEmulation->emulationPaused();
    if (!ready || paused)
    {
        monitor.pcm_peak = monitor.fm_peak = monitor.dsp_peak = 0;
        for (int i = 0; i < 3; ++i) mLevels[i] = -96.0;
    }
    updateMeter(0, monitor.pcm_peak, ready, seconds);
    updateMeter(1, monitor.fm_peak, ready && monitor.fm_supported, seconds);
    updateMeter(2, monitor.dsp_peak, ready && monitor.dsp_enabled, seconds);
    mDspState->setText(!ready ? tr("Waiting") : monitor.dsp_enabled ?
        (monitor.dsp_jit ? tr("Enabled (JIT)") : tr("Enabled (Interpreter)")) : tr("Disabled (legacy core)"));
    mDspState->setStyleSheet(ready && monitor.dsp_enabled ? "color: #25814d; font-weight: bold;" : "");
    mDspProgram->setText(ready && monitor.dsp_enabled ?
        (monitor.dsp_program_loaded ? tr("Program loaded") : tr("No program")) : QString());
    mStatus->setText(!ready ? tr("Start emulation to see sound activity.") : paused ? tr("Paused") :
        monitor.fm_supported ? tr("Live - refreshing 10 times per second") : tr("Legacy SCSP: PCM mix only. FM and DSP are not supported."));
#else
    for (int i = 0; i < 3; ++i) updateMeter(i, 0, false, seconds);
    mDspState->setText(tr("Unavailable"));
    mStatus->setText(tr("Sound monitoring is not supported by this SCSP2 build."));
#endif
}

void UISoundMonitor::showEvent(QShowEvent *event)
{
    QDialog::showEvent(event);
#ifndef USE_SCSP2
    ScspSetAudioMonitorEnabled(1);
#endif
    for (int i = 0; i < 3; ++i) mLevels[i] = -96.0;
    mElapsed.start();
    refresh();
    mTimer->start();
    emit visibilityChanged(true);
}

void UISoundMonitor::hideEvent(QHideEvent *event)
{
    mTimer->stop();
#ifndef USE_SCSP2
    ScspSetAudioMonitorEnabled(0);
#endif
    QDialog::hideEvent(event);
    emit visibilityChanged(false);
}
