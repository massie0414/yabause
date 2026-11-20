# VDP palette viewer tests

Snapshot-only tests require no BIOS, disc, emulation instance or video backend.

```powershell
cmake -S yabause/tests/vdppalette -B build/tests-vdppalette -G "Visual Studio 18 2026" -A x64 `
  -DVDPPALETTE_TEST_QT=ON -DCMAKE_PREFIX_PATH=C:/apps/yabause/.deps/Qt/5.15.2/msvc2019_64
cmake --build build/tests-vdppalette --config Release --parallel 1
ctest --test-dir build/tests-vdppalette -C Release --output-on-failure
```

Omit `VDPPALETTE_TEST_QT` to build only the C tests. Repeat with `Debug` for the
debug configuration. On the local MSVC installation, `$env:_CL_='/Z7'` avoids
the existing PDB tool mismatch when building Debug.

- C regression: CRAM RGB555/RGB888 decoding, raw values, address wrapping,
  all 16 sprite-type masks, sprite CRAM offsets, mixed LUT RGB/index entries,
  LUT wrapping, flat colors, bank sizes, skip/jump/call/return/end commands,
  bounded malformed command lists and output capacity.
- VDP2 cases: bitmap palettes, direct RGB, reserved formats, 2048-color mode,
  one/two-word pattern names, supplementary palette bits, all normal layers,
  16x16 characters, plane alignment, map offsets, rotation A/B and RBG1.
- Qt integration: independent source selection, palette deduplication, native
  ARGB swatches and address details, frozen snapshots, refreshed selection,
  RGB/no-data/reserved states, and hidden refresh timers.

Run `build/tests-vdppalette/Release/vdppalette_qt.exe` without arguments for
two interactive windows with synthetic palettes. `--test -platform offscreen`
runs the assertions and exits, as used by CTest.
