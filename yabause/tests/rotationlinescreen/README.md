# Rotation line-color regression

Build the main VS2026 project first, then run:

```powershell
cmake -S yabause/tests/rotationlinescreen -B build/tests-rotationlinescreen -G "Visual Studio 18 2026" -A x64 -DSDL2_DIR="$PWD/.deps/SDL2-2.32.10/cmake"
cmake --build build/tests-rotationlinescreen --config Release
ctest --test-dir build/tests-rotationlinescreen -C Release --output-on-failure
```

The synthetic fixture needs no BIOS, game assets, or OpenGL context. It renders
through the software VDP2 renderer and Titan compositor, checking:

- Horizontal coefficient changes, including the first row, and replacement of
  the low seven palette-address bits with the current coefficient's line color.
- Rotation modes 0 and 1, mode 2's use of A's line color for both A and B images,
  and mode 3's separate coefficient line colors across the parameter window.
- Ordinary line-color fallback when coefficient line colors are disabled or
  16-bit coefficients are used, plus coefficients that change only per line.
- Doubled horizontal pixels, interlaced field addressing, and the last pixel
  at 704 x 512.
- Layer/compositor worker threads and preservation of the live line-color
  buffers when the unblended layer previews are captured.

The original code fails the first-row, horizontal-color, mode-2, and high-resolution
checks because it samples the previous row's final coefficient for an entire line.
Coefficient colors must instead follow the coefficient selected for each pixel.
The ordinary line screen remains indexed by display line.

Hardware reference: Sega's [VDP2 User's Manual, section 6.4](https://www.infochunk.com/saturn/segahtml_en/hard/vdp2/hon/p06_40.htm)
describes the coefficient line-color bits and their selection in rotation modes
0 through 3. [Section 7.1](https://www.infochunk.com/saturn/segahtml_en/hard/vdp2/hon/p07_10.htm)
describes rotation of the line-color screen using coefficient data.

Game verification: Panzer Dragoon, first stage, software renderer. Replaying the
same state and right-turn input with the old and corrected renderer reproduced
the bright horizontal band across the sky before the change and removed it after.
Left turns and level flight were also compared. Local diagnostic images and game
copies are under the ignored `build/panzer-investigation` directory; they are not
part of this test or distributed with it.
