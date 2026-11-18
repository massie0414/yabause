# VDP2 VRAM decoder regression

No BIOS, game, Qt, or graphics context is required.

```powershell
cmake -S yabause/tests/vdp2vram -B build/tests-vdp2vram -G "Visual Studio 18 2026" -A x64
cmake --build build/tests-vdp2vram --config Release
ctest --test-dir build/tests-vdp2vram -C Release --output-on-failure
```

Checks all five pixel formats, CRAM modes 0/1/2, palette offsets and wrapping,
transparency, grayscale, 8x8 and four-cell 16x16 ordering, bitmap strides,
unaligned VRAM-end wrapping, partial tiles, output bounds, invalid inputs,
and all seven layer/rotation-parameter presets without changing registers.

The optional Qt test checks the actual viewer's controls, copied snapshots,
native pixel colors, zoom, bank boundaries, layer presets, PNG round trips,
empty-state handling and timer shutdown. Run both configurations:

```powershell
cmake -S yabause/tests/vdp2vram -B build/tests-vdp2vram -DVDP2VRAM_TEST_QT=ON -DCMAKE_PREFIX_PATH="$PWD/.deps/Qt/5.15.2/msvc2019_64"
cmake --build build/tests-vdp2vram --config Release
ctest --test-dir build/tests-vdp2vram -C Release --output-on-failure
cmake --build build/tests-vdp2vram --config Debug
ctest --test-dir build/tests-vdp2vram -C Debug --output-on-failure
```

For manual inspection, `vdp2vram_qt.exe --snapshot path/to/state.yss` opens only
the VDP2 chunk of a compatible little-endian save state. It does not run the
emulator or modify the save. This fixture uses the same viewer as the main app.
