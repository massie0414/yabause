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
The mixing and disabled-LFO fixtures exercise both DSP backends.

The MADRS fixture adds 192 checks across the interpreter and JIT. It writes
distinct values to all 32 address registers through SCSP MMIO and checks both
CPU readback and the DSP's register values. A microprogram then reads known
positive/negative samples from sound RAM through every MASA selector and checks
the stereo mixer output. The original code fails 96 of these checks: writes to
MADRS16..31 are discarded and reads incorrectly alias MADRS0..15.

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

The audio monitor checks additionally cover PCM/FM classification by MDL,
envelope/TL and send attenuation, DSP-only sends, signed DSP effect peaks on
both cores, absent DSP programs, read-and-clear intervals, reset, hidden-panel
collection, CD audio exclusion and legacy PCM fallback. The existing stereo
sample assertions run with monitoring enabled to check that it does not change
the generated audio. State-load peak clearing is covered by `tests/scspstate`.
The checks above total 67,629 assertions.

## Quiet voices producing clicks

The New SCSP volume stage combines TL, envelope and amplitude-LFO attenuation.
At a combined attenuation of 1600 or more, it previously right-shifted a signed
32-bit product by at least 32 bits. This is undefined C behavior; on the tested
MSVC x64 build, the shift count wrapped and nearly silent voices produced large
samples. Return the arithmetic-shift limiting value (0 or -1) for these shifts,
preserving the rounding of quieter negative samples and all smaller shifts.

The volume fixture compares all 256 TL settings, envelope/LFO attenuation from
0 through 1471, and seven signed PCM inputs with an independent 64-bit division
oracle. A looping voice at TL=255 with a slow attack also exercises the full
audio path on both DSP backends. The complete suite has 2,705,457 checks; the
unmodified Release library fails 543,379, and the corrected library passes.

Panzer Dragoon was booted normally and driven with the same Start inputs for
7200 emulated frames. The original code executed 188,126 excessive shifts. In
the gameplay capture (frames 1200..7199), the maximum adjacent channel-sample
difference fell from 14,589 to 2,936; differences above 8,000 fell from four to
zero. Neither run had mixer overflow, missing generated samples or CDDA
starvation. A separate 7200-frame NAS/WASAPI run with the original code had
zero SDL underruns. These are waveform and timing checks, not a listening
comparison or a claim that every possible source of clicking is fixed.
Diagnostic sources, PCM captures and logs are in `build/panzer-audio/` and are
excluded from version control.
