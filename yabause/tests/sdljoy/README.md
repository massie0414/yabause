# SDL joystick regression test

This test uses SDL 2.0.14 or newer virtual joysticks and links the real
`persdljoy.c` and `peripheral.c`. It needs no BIOS or game image. Run it with
physical controllers at rest so they do not interfere with input scanning.

From the repository root, in a Visual Studio developer command prompt:

```powershell
cmake -S yabause/tests/sdljoy -B build/tests-sdljoy -G "Visual Studio 18 2026" -A x64 -DSDL2_DIR="$PWD/.deps/SDL2-2.32.10/cmake"
cmake --build build/tests-sdljoy --config Release
ctest --test-dir build/tests-sdljoy -C Release --output-on-failure
```

The test assigns inputs through `PERSDLJoy.Scan`, binds the returned values with
`PerSetKey`, and checks the Saturn controller bytes after `HandleEvents`:

- Four axis directions, held input, release, diagonals, reversal, and dead zone.
- Hat directions and diagonals, plus button press/release.
- Analog-only assignments for the 3D pad, including both extremes and center.

The digital scan mask matches the Qt controller settings dialog. Before the
fix, it returned the same analog key for opposite directions; twelve checks
failed even though input assignment detected the joystick. With the fix,
digital assignments prefer directional keys and analog-only assignments retain
their full axis range.
