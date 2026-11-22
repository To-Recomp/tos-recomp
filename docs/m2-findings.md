# M2 findings: recompiling main.dol with gcrecomp

Reference: PAL French disc 1 (`GQSF`), `sys/main.dol` extracted with `torecomp-disc`. `main.dol` is identical on disc 2.

## What works (verified on Linux, GCC 16)

- `gcrecomp_recompiler` builds on Linux and parses the DOL: 8 sections, entry `0x80003154`, 1.5 MB of code, BSS `0x802C25E0-0x8036AC5C`.
- Control-flow analysis finds 4 346 functions, 122 472 basic blocks, 830 191 instructions, in 0.16 s. No symbol map was supplied.
- Code generation writes 22 C++ files (about 66 MB) in 0.3 s.
- All 22 generated files compile at `-O0` in 11 s on 16 cores, once `recomp_common.h` includes `<cmath>` (fixed in the gcrecomp fork).
- The runtime libraries (`gcrecomp_runtime`, `_gx`, `_audio`, `_input`) compile on Linux. The GX backend is Direct3D 11, so it does nothing useful there.

## What does not exist yet

Nothing has been linked or executed. Not done:

1. **No symbol map for Symphonia.** The Dolphin OS/SDK is statically linked into `main.dol`. gcrecomp replaces SDK functions (OSInit, DVDRead, VIWaitForRetrace, GX*, PAD*, CARD*...) by *address*, taken from a symbol map. Without one, the recompiled SDK code runs as is. About 2 000 sites in the generated code load hardware-register bases (`0xCC000000`, `0xCC010000`, ...) and touch them directly.
2. **No Linux graphics backend.** `gcrecomp_gx` only has Direct3D 11. Reaching the Namco logo needs a GX-to-Vulkan or OpenGL backend through SDL3.
3. **Audio and input backends** are Windows-only (XAudio2, XInput).
4. **No boot code**: nothing maps `main.dol` and the disc files, sets the SDA bases, registers the generated functions, or calls the entry point.

## Decision needed before going further

The wall is item 1. Two ways through it:

- **Identify the SDK by signature**: match functions in the DOL against known SDK code (libogc is zlib-licensed, but it is a different SDK from Nintendo's; matches may be partial) to build a symbol map, then bind them to the runtime's HLE.
- **Emulate the hardware registers** the SDK touches, so the SDK code runs unchanged. That is closer to an emulator and abandons the HLE approach gcrecomp is built around.

No decision has been made.
