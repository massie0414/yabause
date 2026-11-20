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

Cell-map coverage includes all seven layers, normal/rotation plane registers,
512-pixel page ordering in 1x1/2x1/2x2-page planes, plane address alignment and
map offsets, one-/two-word pattern names, both auxiliary character modes,
supplementary bits, palette/CRAM offsets, special flags, NBG0/RBG1 arbitration,
disabled and bitmap layers, VRAM wrapping, bounds and invalid input.

The optional Qt test checks the actual viewer's controls, copied snapshots,
native pixel colors, zoom, bank boundaries, layer presets, PNG round trips,
empty-state handling and timer shutdown.

The Cell map tab is also checked for complete map/plane selection, palette-aware
tile deduplication, original tile orientation versus all four map flip modes,
cell-address hover and tile selection, frozen memory/registers, rotation layers
up to 4096x4096 pixels (262144 cells), transparency overrides, bitmap/invalid-state
transitions and PNG pixels without overlays. It writes
`cell-map-qt.png` for visual inspection. On Windows, run the fixture with
`-platform windows` for a screenshot including native fonts (the offscreen
platform may omit text).

Run both configurations:

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
