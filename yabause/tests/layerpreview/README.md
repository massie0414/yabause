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

Distorted sprite counts are checked against the preview: one per executed draw,
including command type 3 and repeated CALL/RETURN visits. Other primitives,
skipped/unreachable/end commands are excluded. Each capture resets the count,
including display-off, empty-list, and unavailable captures.

RBG1 fixtures also check its independent display bit (0x20), its parameter-B
preview while main display switches are off, and simultaneous RBG0/RBG1 images.
The seven outputs have fixed identities: VDP1, NBG0..3, RBG0, RBG1. Mode changes
must clear unused outputs instead of placing a rotation background in NBG0.

Live VDP1 framebuffer snapshots are also checked: distinct front/back buffers,
swapping, retained pixels, CPU word/byte writes, RGB555 and 16/24-bit CRAM,
priority-bit removal and palette offsets, shadow/window markers, all three native
buffer layouts, output bounds, disabled display switches, register/buffer
preservation, active VDP1 workers, unsupported backends, invalid arguments and
teardown. These snapshots read actual memory without replaying the command list.

RBG0 coordinate tracing (`VIDSoftRenderLayerPreviewsWithRbg0`) is checked against
known source coordinates for identity, translation, negative wrapping, scale,
90-degree rotation, horizontal/per-line coefficients, 16-bit coefficients,
rotation modes 0..3, layer windows, screen-over clipping and 704 x 512 pixel
duplication. Transparent texels retain their geometry; disabled layers clear it,
and RBG1 cannot overwrite it. Capture preservation checks run with tracing enabled,
including capture without initializing the software backend. Both the legacy
image-only entry point and the extended entry point remain covered.
Combined captures also check that distorted sprite counts and images remain correct
while RBG0 source coordinates are collected in the same snapshot.

Distorted-sprite outlines (`VIDSoftRenderLayerPreviewsWithDebug`) are checked
against slanted A-B-C-D edges, transparent interiors, type 3 commands, excluded
primitives/skips/end commands, transparent textures, priority zero, system and
inside/outside user clipping, repeated CALL/RETURN with signed local origins,
high-resolution/interlaced readout, 8-bit framebuffer scaling, and rotated
framebuffer readout. Bounds and stale-image clearing are checked too. State
preservation and active-worker checks enable both outlines and RBG0 tracing.
