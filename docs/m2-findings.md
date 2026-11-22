# M2 findings: recompiling main.dol with gcrecomp

Reference: PAL French disc 1 (`GQSF`), `sys/main.dol` extracted with `torecomp-disc`. `main.dol` is identical on disc 2.

## What works (verified on Linux, GCC 16)

- `gcrecomp_recompiler` builds on Linux and parses the DOL: 8 sections, entry `0x80003154`, 1.5 MB of code, BSS `0x802C25E0-0x8036AC5C`.
- Control-flow analysis finds 4 346 functions, 122 472 basic blocks, 830 191 instructions, in 0.16 s. No symbol map was supplied.
- Code generation writes 22 C++ files (about 66 MB) in 0.3 s.
- All 22 generated files compile at `-O0` in 11 s on 16 cores, once `recomp_common.h` includes `<cmath>` (fixed in the gcrecomp fork).
- The runtime libraries (`gcrecomp_runtime`, `_gx`, `_audio`, `_input`) compile on Linux. The GX backend is Direct3D 11, so it does nothing useful there.

## What does not exist yet

1. **No Linux graphics backend.** `gcrecomp_gx` only has Direct3D 11, and its GX functions are a C++ API, not bound to the guest calling convention. Reaching the Namco logo needs GX bound to guest calls and a Vulkan or OpenGL backend through SDL3.
2. **Audio and input backends** are Windows-only (XAudio2, XInput).
3. **Unnamed hardware-touching SDK code**: EXI, SI, DSP, and short leaf functions that no signature matched.

## First boot (`src/main.cpp`, `tos_boot`)

`tos_boot` loads `main.dol` into emulated RAM, registers the recompiled functions and calls `__start` (`0x80003154`). The recompiler now emits, in every function that has a symbol-map name, a check for a native replacement (`lookup_os_func`) before the recompiled body, so direct calls reach the HLE.

Observed on the real disc 1, in order: `__OSInitSystemCall`, `CARDInit`, `VIInit`, three `OSCreateHeap` calls with the game's own sizes (2.5 MB, 6 MB, 5 MB), `ARInit`, `ARQInit`, `VIConfigure`, GX initialization, `VISetBlack`, `VISetNextFrameBuffer`, `VIFlush`, then the first disc access: `DVDOpen("pal_fontb0.dat")`, a real file of the extracted disc.

Each stall was a function waiting for hardware that does not exist, and each fix was one HLE or one recompiler fix:

| Stall | Cause | Fix |
| --- | --- | --- |
| `__ARChecksize` | probes ARAM size through DMA registers | `ar_funcs.cpp`: ARAM as a host buffer, immediate DMA and ARQ |
| `OSReport` (EXI UART) | unnamed variadic function writing to the EXI UART | named by hand in `symbols/GQSF.manual.csv`, bound to the existing HLE |
| SIGFPE in a GX function | `divw` by zero traps on x86 | recompiler emits the Gekko result (`-1` or `0`) |
| DVD status loop | driver state machine driven by DI interrupts | `dvd_funcs.cpp`: idle drive, disc present, immediate async reads |
| spin in `DVDCancel` | `DVDClose` was unnamed, so the raw driver ran | named by hand |
| **now**: `GXDrawDone` | waits for the GP "draw done" interrupt | not done: needs GX bound to the guest |

Not reached: the Namco logo, the title screen. No picture, no sound. The first milestone of Q40 (M2) is **not** met.

## SDK identification (decided: identify by signature)

`main.dol` carries the *Dolphin SDK* of April 2003 (OS `Apr 17 2003`, GX `Apr 21 2003`, DVD `Apr 22 2003`). Reference games whose decompilation projects published symbol maps (CC0) use other builds: Wind Waker PAL `GZLP01` (Sep 2002) and Twilight Princess PAL `GZ2P01` (Apr and Nov 2004). Symphonia sits between them.

`tools/sdk-identify/identify.py` builds masked signatures (addresses and immediates removed) of each reference SDK function, matches them against the 4 346 functions of `main.dol`, keeps the matches that are consistent with the SDK's source-file link order, and pairs the functions in between when counts agree.

Result (`symbols/GQSF.csv`, details in `symbols/GQSF.matches.tsv`):

- 499 functions named out of about 800 reference SDK functions: 163 exact, 274 fuzzy, 62 by layout.
- 436 entries in the CSV: those whose first instructions equal the reference's (start verified), or that both references name identically.
- Covered: OS (threads, alarms, heap, context), DVD, VI, PAD, AI, AR, CARD, GX (init, FIFO, textures, TEV, transform), MTX.
- Not found: EXI, SI, DSP, most of OSMutex/OSMessage, `OSReport`, `DVDRead`. Their code changed between builds or is too short to match.

How far to trust it:

- The two references, independent, name the same address identically for 354 of the 357 addresses both name (99.2%).
- `OSInit` at `0x800F6F00` is named by Wind Waker only (fuzzy, 0.84). It is also the function that loads the `Dolphin SDK - OS` version string, which is independent evidence. It is not in the CSV because its start is not verified.
- Fuzzy matching can name an address that lies inside the real function. The start check and the agreement rule exist for that reason. Names that are in `.matches.tsv` but not in the CSV have not been confirmed this way and must not be bound to the runtime yet.
- Nothing here has been run. A wrong name bound to the HLE would replace the wrong function.

## Next

1. Bind the GX functions to guest calls (`GXDrawDone` first), with a Linux backend behind them (Vulkan or OpenGL through SDL3).
2. Continue the stall-and-fix loop: PAD, then the audio path (AI, DSP, AX), then the remaining DVD and CARD functions.
3. Name the SDK functions that no signature matched, from their behavior, each with its evidence in `symbols/GQSF.manual.csv`.
