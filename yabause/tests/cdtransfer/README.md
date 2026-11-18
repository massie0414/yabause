# Movie streaming transfer regressions

Build the main project first. These tests use synthetic sectors and sound RAM;
no BIOS, disc image, or copyrighted audio is needed.

```powershell
cmake -S yabause/tests/cdtransfer -B build/tests-cdtransfer -G "Visual Studio 18 2026" -A x64 -DSDL2_DIR="$PWD/.deps/SDL2-2.32.10/cmake"
cmake --build build/tests-cdtransfer --config Release
ctest --test-dir build/tests-cdtransfer -C Release --output-on-failure
```

Repeat the build and test with `Debug` to check both configurations.

- `cdtransfer_regression`: 96 checks covering normal longword reads, bulk T1
  and T2 reads, nonzero starting sectors (including 16), partial reads, sector
  boundaries, byte counts, and deletion of only the requested sectors.
  The original code fails 18 checks because it reads from partition offset zero
  even when the command requests a different starting sector.
- `scudma_regression`: 64 cases covering B-bus reads with all eight increment
  settings, immediate/register counts, address hold, CT wraparound, and timed
  and untimed execution. Each read must advance four bytes regardless of the
  instruction's increment field. The original code fails 46 cases.

Grandia DISC1's New Game movie exposed both bugs. A CD transfer starting at
sector 16 returned video data 0x8000 bytes before the requested audio chunk.
After correcting that, periodic short glitches remained where SCU DSP DMA
repeated one compressed audio word instead of advancing through sound RAM.
The independent [Mednafen changelog (2017-09-15)](https://mednafen.github.io/documentation/ChangeLog.txt)
also identifies B-bus DSP reads as the cause of periodic Grandia FMV pops;
its [SCU implementation](https://github.com/libretro/beetle-saturn-libretro/blob/master/mednafen/ss/scu.inc)
documents the fixed four-byte read increment.

Validation used a clean boot, Start at frame 600, Start at 1200 and A at 1260.
The attached blue movie scene is reproduced near frame 2100. Its DSP clipping
is eliminated. Over a longer recording, 1,459,720 residual samples per channel
match an independent demux/decode of MOV00.MOV within two integer units
(existing driver rounding); there are no remaining erroneous source samples.
DSP effects 12/13 have no clipping across captured frames 1200 through 5399.
The movie was checked as generated PCM, not by speaker listening.

The diagnostic recordings, local disc copy, and reference decoder are excluded
from version control under `build/grandia-investigation/`. The machine-readable
comparison is `movie-validation.json` in that directory.
