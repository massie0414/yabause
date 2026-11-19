# SCSP mixing, disabled LFO and MADRS regression

Build the main project with Musashi and Play JIT enabled first (the VS2026
configuration does this). No BIOS or game image is required.

```powershell
cmake -S yabause/tests/scspmix -B build/tests-scspmix -G "Visual Studio 18 2026" -A x64 -DSDL2_DIR="$PWD/.deps/SDL2-2.32.10/cmake"
cmake --build build/tests-scspmix --config Release
ctest --test-dir build/tests-scspmix -C Release --output-on-failure
```

Use `Debug` in both build/test commands to check that configuration as well.

The fixture sends known positive and negative samples through a two-instruction
DSP program (MIXS0 multiplied by 0.5, then written to EFREG4). It checks the
actual SCSP stereo output against the expected signed attenuation, for both
the interpreter and JIT, all seven nonzero EFSDL levels, and five pan settings.
This includes a zero sample and the most negative 16-bit input. There are 630
checks; the original code fails 120 negative-sample JIT checks. Full-level
effects and interpreter output also provide unchanged-behavior coverage.

The JIT stores EFREG as an unsigned 16-bit pattern in a 32-bit field, whereas
the interpreter promotes its signed 16-bit field. Shifting the JIT result
before interpreting its sign turns negative PCM into positive PCM when EFSDL
attenuation is enabled. For example, an effect sample of -1 at EFSDL=6 used to
produce +8191 after mixing instead of -1. Interpret EFREG/EXTS as signed 16-bit
PCM before attenuation in the common mixer.

This was reproduced with Grandia disc 1's title BGM and New SCSP + DSP JIT.
The game uses EFSDL=6 for effects 4 and 8. For the same title-screen interval
(emulated frames 1200 through 2399), recorded stereo PCM had 86,448 adjacent
channel-sample differences greater than 8000 before the fix and none afterward;
the maximum difference fell from 8193 to 286. This is a waveform check, not a
hardware-accuracy or listening comparison. Game images, recordings and
diagnostic builds are kept outside version control under `build/`.

The disabled-LFO fixture adds 65,536 checks: a constant PCM wave must retain
its amplitude, and an alternating 22.05 kHz PCM wave must retain its four-sample
output period. It exercises all four LFO waveforms with both sensitivities set
to zero. Before the fix, 41,580 of these checks fail because PLFOS=0 still adds
pitch modulation and ALFOS=0 still adds amplitude modulation. In Grandia this
caused occasional skipped/repeated residual samples, independently of the
CD/SCU transfer bugs. Zero sensitivity means no modulation in
[SEGA's SCSP manual, table 4.17](https://www.infochunk.com/saturn/segahtml_en/hard/scsp/hon/p04_26.htm).
The mixing and disabled-LFO fixtures have 66,166 checks in total.

The MADRS fixture adds 192 checks across the interpreter and JIT. It writes
distinct values to all 32 address registers through SCSP MMIO and checks both
CPU readback and the DSP's register values. A microprogram then reads known
positive/negative samples from sound RAM through every MASA selector and checks
the stereo mixer output. The original code fails 96 of these checks: writes to
MADRS16..31 are discarded and reads incorrectly alias MADRS0..15. The complete
suite now has 66,358 checks.

MADRS occupies 32 words at 0x780..0x7BF, as shown in figure 4.5 of
[SEGA's SCSP user's manual](https://antime.kapsi.fi/sega/files/ST-077-R2-052594.pdf).
Both DSP backends already store all 32 registers; the fix corrects the shared
word-access address decoder.

Integration verification used the actual `C:\apps\SS_DSP\game.cue` image and its
pad-driven footstep probe, linked directly to this checkout's Release library
without the sample's temporary SCSP patch. Before the fix, both backends produce
zero reverb-tail RMS for footstep 1. After the fix, the interpreter/JIT tail RMS
is 19.20/15.72 for footstep 1 and 82.92/73.24 for footstep 2. Dry playback, B-to-A
switching, stopping and HALL-to-CAVE switching pass the sample's PCM assertions;
the 27.3-second captures contain no clipped output samples. Logs, captures and
dry/wet WAV pairs are under `build/ss-dsp-probe/` (ignored by Git). This checks
emulator PCM output, not a listening comparison or equivalence to real hardware.
