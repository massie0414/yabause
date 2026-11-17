# Layer preview regression

Build the main VS2026 project first, then run the synthetic graphics fixture:

```powershell
cmake -S yabause/tests/layerpreview -B build/tests-layerpreview -G "Visual Studio 18 2026" -A x64 -DSDL2_DIR="$PWD/.deps/SDL2-2.32.10/cmake"
cmake --build build/tests-layerpreview --config Release
ctest --test-dir build/tests-layerpreview -C Release --output-on-failure
```

No BIOS, game, or OpenGL context is needed. Six distinct layer colors, transparent
pixels, display switches, live registers/VRAM/framebuffers/compositor preservation,
active software render workers, maximum interlaced dimensions, display-off clearing,
and teardown are checked. The first capture runs without initializing the software
video backend, as when the main window uses another renderer.
