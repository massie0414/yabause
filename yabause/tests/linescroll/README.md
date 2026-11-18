# Line and vertical cell scroll regression

Build the main VS2026 project first, then run:

```powershell
cmake -S yabause/tests/linescroll -B build/tests-linescroll -G "Visual Studio 18 2026" -A x64 -DSDL2_DIR="$PWD/.deps/SDL2-2.32.10/cmake"
cmake --build build/tests-linescroll --config Release -- /m:1 /nr:false
ctest --test-dir build/tests-linescroll -C Release --output-on-failure
```

The synthetic fixture needs no BIOS, game assets, or OpenGL context. It renders
through the software VDP2 renderer and Titan compositor and checks:

- A 352-pixel-wide image packed into a 512-pixel-wide bitmap, reconstructed by
  horizontal/vertical line scroll and fractional vertical cell scroll. Every
  output pixel is checked against the original synthetic image.
- NBG0 and NBG1, including the alternate table entries when both cell-scroll
  flags are enabled, progressive display, and both double-density fields.
- All seven combinations of horizontal scroll, vertical scroll, and horizontal
  zoom, at table intervals of 1, 2, 4, and 8 lines, including table address wrap.
- Fractional register, line, and cell offsets; vertical coordinate increments
  between table entries; per-line cell table snapshots; and 512-row bitmap wrap.
- Mosaic priority over vertical cell scroll, and layer/compositor worker threads.

The previous renderer used the first cell's integer offset for the entire line,
dropped the fractional line offset, wrapped bitmap X at a fixed 1024 pixels, and
advanced enabled line-table fields inconsistently. The unchanged regression
executable built with the previous renderer reports 2,041,100 pixel mismatches;
the corrected renderer passes in Release and Debug. Existing layer-preview and
rotation line-color regressions also pass in both configurations.

Hardware reference: Sega's [VDP2 User's Manual, section 5.3](https://www.infochunk.com/saturn/segahtml_en/hard/vdp2/hon/p05_30.htm)
specifies the packed table fields, fractional scroll values, cell ordering,
interval handling, and mosaic priority.

Game verification: Grandia DISC1, software renderer, New Game opening movie.
With identical automatic button input, the original renderer reproduces the
horizontal stripes in the water and floating-city scenes. The corrected build
removes them. A 7,201-frame run also checks later credit and interior scenes.
The 3,601-frame before/after audio captures are byte-identical. Diagnostic images,
recordings, and the local game copy are under the ignored
`build/grandia-investigation` directory and are not distributed with this test.
