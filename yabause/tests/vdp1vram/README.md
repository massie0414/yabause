# VDP1 VRAM preview regression

Standalone tests; no BIOS, game, Qt, SDL, or emulator build required.

```powershell
cmake -S yabause/tests/vdp1vram -B build/tests-vdp1vram -G "Visual Studio 18 2026" -A x64
cmake --build build/tests-vdp1vram --config Release
ctest --test-dir build/tests-vdp1vram -C Release --output-on-failure
```

Repeat with `Debug` to test that configuration. Checks cover command branching,
CALL/RETURN, skipped commands, invalid/truncated/cyclic lists, capacity bounds,
all six documented color modes, CRAM formats, LUT and bank colors, sprite-type
palette masks, palette offset, SPD/ECD, per-row end codes, VRAM wrap, unaligned
reads, raw grayscale, odd widths, high-speed shrink, and input-memory preservation.

End-code values are checked before masking the 64/128-color indices, following
the [VDP1 manual, ECD description](https://www.infochunk.com/saturn/segahtml_en/hard/vdp1/hon/p06_34.htm).
