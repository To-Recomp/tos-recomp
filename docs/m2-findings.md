# M2 findings: recompiling main.dol with gcrecomp

Reference: PAL French disc 1 (`GQSF`), `sys/main.dol` extracted with `torecomp-disc`. `main.dol` is identical on disc 2.

## What works (verified on Linux, GCC 16)

- `gcrecomp_recompiler` builds on Linux and parses the DOL: 8 sections, entry `0x80003154`, 1.5 MB of code, BSS `0x802C25E0-0x8036AC5C`.
- Control-flow analysis finds 4 346 functions, 122 472 basic blocks, 830 191 instructions, in 0.16 s. No symbol map was supplied.
- Code generation writes 22 C++ files (about 66 MB) in 0.3 s.
- All 22 generated files compile at `-O0` in 11 s on 16 cores, once `recomp_common.h` includes `<cmath>` (fixed in the gcrecomp fork).
- The runtime libraries (`gcrecomp_runtime`, `_gx`, `_audio`, `_input`) compile on Linux. `gcrecomp_gx` has a Direct3D 11 backend only; graphics on Linux come from the new `gcrecomp_gp` (below).

## What does not exist yet

1. **Audio** (AI, DSP, AX).
2. **Guest threads.** The runtime does not run them: `OSCreateThread` only fills the structure and the scheduler calls (`OSSuspendThread`, `OSDisableScheduler`, ...) are no-ops, because the SDK's scheduler spins when nothing is runnable. A game whose logic needs its other threads (the CRI ADX audio library does) will not get past that point.
3. **A memory card.** `CARDProbeEx` reports no card. The card functions that remain are stubs that report success without storing anything. Saves in the console's own format (a memory card image) are not implemented.
4. **Unnamed hardware-touching SDK code**: EXI, SI, DSP, and short leaf functions that no signature matched.
5. **GP features**: lighting (vertex colors are used as they are), palette textures with 14-bit indices (C14X2), EFB-to-texture copies, indirect TEV stages, fog, mipmaps. The renderer is OpenGL 3.3 through SDL3 and has only been run on Linux (Mesa, AMD).

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
| `GXDrawDone` | waits for the GP "draw done" interrupt | replaced in `gx_funcs.cpp`; see the next section |

## Graphics: the GP module (`gcrecomp/src/gp`)

The SDK's GX functions only encode commands into the write-gather pipe at `0xCC008000`. Instead of replacing each GX function, the recompiled SDK code runs unchanged and `gcrecomp_gp` consumes the stream it writes: it parses CP/XF/BP loads and draw commands, keeps the register state, decodes vertices (all component formats, indexed arrays, display-list calls), transforms them with the XF matrices, and draws through an OpenGL fragment shader that implements up to 16 TEV stages from uniforms. A copy to the external frame buffer presents a frame. Only `GXDrawDone` is replaced (it must not wait for an interrupt).

Four bugs stood between the first draw and the first correct picture; none was visible from the GP side:

| Symptom | Cause | Fix |
| --- | --- | --- |
| garbage BP state, NaN projection | the game records display lists by pointing the CPU FIFO away from the GP FIFO (PI vs CP registers); the data was executed instead of stored | `wgp_write` stores into memory at the PI write pointer when the FIFOs differ |
| projection built from zeros | `load_dol` cleared BSS after copying sections, but the BSS range spans the small-data sections (`.sdata2` constants) | clear first, then copy |
| viewport width 0 | `fsubs` rounded its operands to single before subtracting; the int-to-float trick (`2^52 + n - 2^52`) gave 0 | single-precision ops compute in double, then round |
| second element of `psq_st` lost | paired stores advanced the address, but the write-gather pipe is a single address | `psq_next_addr` |

Result: with `TOS_SCREENSHOT_DIR` set, `tos_boot` saves presented frames; `TOS_SCRIPT="150:U,175:A"` scripts controller input (a frame number and a button, held five frames). About 58 frames per second are presented, with no unknown command in the stream.

What the game shows, in order, on black: an animated bar (a loading indicator), then its own dialog **"Aucune carte mémoire détectée. Tales of Symphonia nécessite 1 fichier et 3 blocs pour être sauvegardé. Continuer sans sauvegarder / Réessayer"**, with the font drawn from palette textures. Pressing a button through the PAD replacement moves the game to the next screen ("Insérez carte mémoire." with a "Retour" prompt), so input works end to end. After Up then A the game stops presenting frames (178 in total): backtraces show it in functions at `0x8016xxxx`-`0x8017xxxx`, polling `DVDGetDriveStatus` tens of millions of times and printing through an `OSReport`-like function. The `ADXGCSDK` string in the DOL makes the CRI ADX audio library the likely owner of that code (inference, not checked). The Namco logo has not been reached.

The dialog was never drawn twice: that was a bug of the harness's screenshot code (`write_screenshot` read the EFB with the window's pitch). Fixed in gcrecomp (#7). Earlier positions measured from those screenshots were wrong.

### The stall after the dialog: three causes in a row (found with gdb and the game's own error messages)

| Observation | Cause | Fix |
| --- | --- | --- |
| `E0041303: illigal parameter 'ptid' (ADXF_GetPtStat)` printed in a loop; `GCSYS.c` assertions | the game's heap allocator returned NULL for every allocation (`ADXT_Create: parameter error`, work area NULL). gcrecomp's HLE `OSCreateHeap` builds its own heap layout, but `OSAllocFromHeap` is the recompiled SDK function, which reads the SDK's heap array | `OSCreateHeap` is no longer bound (commented in `GQSF.csv`); the SDK code creates the heaps |
| the heap list was walked into code bytes (`Bad address 0x40820010`), `OSAllocFromHeap` looped | the harness set the arena low bound to `0x80400000`; the game hard-codes a heap at `0x804E4000` and its own arena allocations reached `0x8054F000`, overlapping it | `config.arena_lo = 0`: as on a console, `OSInit` derives it from the end of the DOL |
| `[Poll] Read16 0xCC005000 = 0x80F3` forever (`func_8015BA88`, the SDK's DSP boot task) | no DSP: the boot task sends mails to the DSP and waits for it to take them; the game then needs the DSP to answer | open: gcrecomp needs a DSP device (mailboxes, control register, interrupts). The game ships its own microcode (0x19E0 bytes at `0x802B7B40`), so a complete answer needs a DSP interpreter |

After the first two fixes the game loads all the character data (`regal*.bin`, `kratos*.bin`, ... `BTL/btlusual.dat`, `field_sil.so`) and the sound files, then reaches the DSP boot task. No frame is presented after the dialog yet.

Further bugs found on the way: a missing function entry for every address reached only through a function pointer (`symbols/GQSF.extra_funcs.txt`, found by running the game and adding each "unresolved call"), and `PADInit`/`PADRead` being unnamed (named by hand).

### After the DSP: the ADX threads (fixed in gcrecomp)

With a DSP device (mailboxes, ROM handshake, interrupt) and an AX command-list HLE, the game reached `BTL/BTLvoice.afs` and looped waiting for its partition to load. The cause was the scheduler: the CRI ADX library does its DVD reads from its own guest threads (one wakes up each retrace, then wakes the file-system thread), and they never ran because (1) `OSEnableScheduler` (`0x800FE2B0`) was unnamed while `OSDisableScheduler` was replaced, so the scheduler stayed disabled, and (2) a sleeping thread only ran at a switch point, which the main thread, spinning on `DVDGetDriveStatus`, never reached. `sched::poll()` now stands in for the decrementer and retrace interrupts.

Result (checked on Linux, screenshots compared with Dolphin on the same disc): after "Continuer sans sauvegarder" the game shows **LICENSED BY NINTENDO**, **namco Produced by NAMCO**, **NAMCO TALES STUDIO LTD.**, then starts the intro movie, which is garbled. Then it opens `map/tit_t00.bin`, `lloyd_ex.bin`, `title.tpl`. About 1 800 frames in 45 s, 0 unknown GP commands. The Namco logo of M2 is reached; the title screen and sound (the AX voice path is unit-tested only) are not.

Still missing: a real VI retrace interrupt and decrementer time slicing, the intro movie (garbled), the title screen.

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
