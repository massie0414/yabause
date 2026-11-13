# SCSP save-state regression

Build the main project first with Musashi and Play JIT enabled (the VS2026
configuration does this). No BIOS or game image is needed for these tests.

```powershell
cmake -S yabause/tests/scspstate -B build/tests-scspstate -G "Visual Studio 18 2026" -A x64 -DSDL2_DIR="$PWD/.deps/SDL2-2.32.10/cmake"
cmake --build build/tests-scspstate --config Release
ctest --test-dir build/tests-scspstate -C Release --output-on-failure
```

The fixture plays a looping PCM voice through an active DSP program, saves it,
resets the chip and changes its program, then reloads it. Both output channels
must match the uninterrupted reference exactly for 512 samples. Tests cover
the interpreter, the JIT, an uploaded but not yet activated JIT program, legacy
version-2 chunk consumption, and truncated version-3 data. The original code
failed ten register/DSP/PCM checks in the first two round trips.

New-SCSP saves now use SCSP chunk version 3. Fields are explicitly serialized
at fixed widths; pointers, compiler padding, and machine code are not saved.
The DSP backend used when saving is restored. Its JIT blocks are rebuilt from
the saved instructions. Old-SCSP saves retain chunk version 2.

Legacy files remain loadable, but files made with New SCSP previously omitted
the voices and DSP state. Missing information cannot be recovered by resaving
such a file. Start the game normally, load an in-game save, and make a new
emulator state. New version-3 states require the updated executable.
