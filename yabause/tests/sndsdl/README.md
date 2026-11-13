# SDL audio regression tests

These tests compile the production `sndsdl.c` backend and do not require a game
or BIOS. From the repository root in a Visual Studio developer command prompt:

```powershell
cmake -S yabause/tests/sndsdl -B build/tests-sndsdl -G "Visual Studio 18 2026" -A x64 -DSDL2_DIR="$PWD/.deps/SDL2-2.32.10/cmake"
cmake --build build/tests-sndsdl --config Release
ctest --test-dir build/tests-sndsdl -C Release --output-on-failure
```

`sndsdl_regression` supplies deterministic SDL device functions using SDL's real
headers. It checks empty/full accounting, ordered playback across ring wrap,
silence on underrun, buffered recovery, reset, NTSC/PAL changes, volume scaling,
clipping, mute and initialization cleanup. The original backend failed 24
checks; the corrected backend passes them.

`sndsdl_threaded` uses SDL's real audio thread with the dummy audio driver. It
consumes a filled queue and repeatedly changes video format, resets, adjusts
volume, mutes, and closes/reopens the device. It does not play audible sound.

The queue now holds at least two SDL callback blocks and waits for that reserve
at startup and after starvation. At the normal 44.1 kHz / 2048-frame callback
size this reserve is about 93 ms. Only submitted samples are consumed; missing
samples are silence instead of stale audio from an earlier trip around the
buffer. This handles scheduling jitter, but cannot compensate for sustained
emulation slower than real time. These tests verify the output backend, not
the accuracy of the emulated SCSP or subjective game audio quality.
