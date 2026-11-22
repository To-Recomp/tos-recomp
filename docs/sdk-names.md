# SDK names merged from sdk-identify v2

`symbols/GQSF.csv` after merging `tools/sdk-identify/results/GQSF.v2.csv` with `scripts/merge-sdk-names.py`. A bound name that has a gcrecomp HLE replaces the recompiled function (docs/DEVELOPING.md, "Merging SDK names"); the other names only label.

## Verification (new-game script, `scripts/try-symbols.sh`, one run per group)

Baseline (main): `wait-file map/isa_t02` at frame 2776, `shot field` at 8533, 7225 presented frames at 190 s, classroom with Lloyd rendered. Merged CSV (below): frames 2775 / 8529, 7222 presented frames, same picture (RMSE 3.6 against the baseline classroom shot, an animated scene).

| Group | Result |
| --- | --- |
| EXI/SI/DSP/DB (65 names) | works, except `DSPInit`: its HLE is a stub that skips the real DSP initialisation and the game stops before the title (476 frames) |
| DVD (20 names, incl. `DVDInit` HLE stub) | works, same timeline |
| OS (48 names) | `OSInit` (HLE stub skips the real init) 0 frames; `OSCreateHeap`+`OSSetCurrentHeap` (HLE heap table vs recompiled OSAllocFromHeap) stalls at 477 frames; arena names OK; with `OSCreateAlarm` plus the thread names the opening movie s01 is cut after ~200 frames instead of ~3900 (reproduced twice; without `OSCreateAlarm` the timeline is normal, so it is left unnamed) |
| renamed + moved starts (47) | works |
| misc VI/PAD/AI/AR/CARD/GX/MTX (58) | works |

Excluded (kept unnamed): `OSInit`, `OSCreateHeap`, `OSSetCurrentHeap`, `DSPInit`, `OSCreateAlarm`.

## OSReport / OSPanic

`0x800F94A8` is `OSReport`: it saves the variadic registers, builds a `va_list` and calls `vprintf` (0x8012503C); callers pass a printf format in r3. `0x800F9528` is `OSPanic`: every caller passes (file string, line number, message string) in r3/r4/r5. The hand name `OSReport` at 0x800F9528 was wrong (`scripts/dol-disasm.py --callers`).

## Names

"HLE" is the gcrecomp function bound to the name. All applied rows were exercised by the merged run above (a label-only name is only verified not to break the game).

| name | address | class | evidence | HLE | applied |
|---|---|---|---|---|---|
| PPCMfmsr | 0x800F6B30 | new (os) | align, score 1.0, start verified, 2 refs | - | yes |
| PPCMtmsr | 0x800F6B38 | new (os) | align, score 1.0, start verified, 2 refs | - | yes |
| PPCMfhid0 | 0x800F6B40 | new (os) | align, score 1.0, start verified, 2 refs | - | yes |
| PPCMthid0 | 0x800F6B48 | new (os) | align, score 1.0, start verified, 2 refs | - | yes |
| PPCMfl2cr | 0x800F6B50 | new (os) | align, score 1.0, start verified, 2 refs | - | yes |
| PPCMtl2cr | 0x800F6B58 | new (os) | align, score 1.0, start verified, 2 refs | - | yes |
| PPCMtdec | 0x800F6B60 | new (os) | align, score 1.0, start verified, 2 refs | - | yes |
| PPCSync | 0x800F6B68 | new (os) | align, score 1.0, start verified, 2 refs | - | yes |
| PPCHalt | 0x800F6B70 | new (os) | align, score 1.0, start verified, 2 refs | - | yes |
| PPCMfhid2 | 0x800F6BFC | new (os) | align, score 1.0, start verified, 2 refs | - | yes |
| PPCMthid2 | 0x800F6C04 | new (os) | align, score 1.0, start verified, 2 refs | - | yes |
| PPCMtwpar | 0x800F6C0C | new (os) | align, score 1.0, start verified, 2 refs | - | yes |
| PPCDisableSpeculation | 0x800F6C14 | new (os) | exact, score 1.0, start verified, 2 refs | - | yes |
| PPCSetFpNonIEEEMode | 0x800F6C3C | new (os) | align, score 1.0, start verified, 2 refs | - | yes |
| ClearArena | 0x800F6D94 | new (os) | exact, score 1.0, start verified, 1 ref | - | yes |
| OSInit | 0x800F6EF8 | new (os) | version, score 1.0, start verified, 2 refs | os_funcs.cpp:os_init_stub | no |
| __OSPSInit | 0x800F769C | new (os) | exact, score 1.0, start verified, 2 refs | - | yes |
| __OSGetDIConfig | 0x800F76F0 | new (os) | align, score 1.0, start verified, 2 refs | - | yes |
| OSRegisterVersion | 0x800F7704 | new (os) | exact, score 1.0, start verified, 2 refs | - | yes |
| OSInitAlarm | 0x800F7730 | new (os) | exact, score 1.0, start verified, 2 refs | - | yes |
| OSCreateAlarm | 0x800F777C | new (os) | align, score 1.0, start verified, 2 refs | thread_funcs.cpp:sched::os_create_alarm | no |
| DecrementerExceptionHandler | 0x800F7D90 | new (os) | align, score 1.0, start verified, 2 refs | - | yes |
| OSSetCurrentHeap | 0x800F8004 | new (os) | align, score 1.0, start verified, 2 refs | os_funcs.cpp:os_set_current_heap | no |
| OSCreateHeap | 0x800F8084 | new (os) | exact, score 1.0, start verified, 2 refs | os_funcs.cpp:os_create_heap | no |
| OSDestroyHeap | 0x800F80F0 | new (os) | align, score 1.0, start verified, 1 ref | - | yes |
| OSGetArenaHi | 0x800F84C4 | new (os) | align, score 1.0, start verified, 2 refs | os_funcs.cpp:os_get_arena_hi | yes |
| OSGetArenaLo | 0x800F84CC | new (os) | align, score 1.0, start verified, 2 refs | os_funcs.cpp:os_get_arena_lo | yes |
| OSSetArenaHi | 0x800F84D4 | new (os) | align, score 1.0, start verified, 2 refs | os_funcs.cpp:os_set_arena_hi | yes |
| OSSetArenaLo | 0x800F84DC | new (os) | align, score 1.0, start verified, 2 refs | os_funcs.cpp:os_set_arena_lo | yes |
| DCEnable | 0x800F87A4 | new (os) | align, score 1.0, start verified, 2 refs | - | yes |
| ICFlashInvalidate | 0x800F88D0 | new (os) | align, score 1.0, start verified, 2 refs | - | yes |
| ICEnable | 0x800F88E0 | new (os) | align, score 1.0, start verified, 2 refs | - | yes |
| DMAErrorHandler | 0x800F89B4 | moved (moved) | exact, score 1.0, start verified, 2 refs; was 0x800f89bc | - | yes |
| OSGetStackPointer | 0x800F901C | new (os) | align, score 1.0, start verified, 2 refs | - | yes |
| OSReport | 0x800F94A8 | moved (moved) | fuzzy, score 0.84, start unverified, 2 refs; was 0x800f9528 | os_funcs.cpp:os_report | yes |
| OSPanic | 0x800F9528 | renamed (renamed) | fuzzy, score 0.65, start unverified, 2 refs; was OSReport | os_funcs.cpp:os_panic | yes |
| SetInterruptMask | 0x800FB320 | new (os) | callgraph, score 0.52, start verified, 2 refs | - | yes |
| OSNotifyLink | 0x800FBA9C | renamed (renamed) | align, score 1.0, start verified, 2 refs; was OSNotifyUnlink | - | yes |
| OSNotifyUnlink | 0x800FBAA0 | moved (moved) | align, score 1.0, start verified, 2 refs; was 0x800fba9c | - | yes |
| OSLink | 0x800FC040 | new (os) | exact, score 1.0, start verified, 2 refs | - | yes |
| MEMIntrruptHandler | 0x800FC4C4 | moved (moved) | exact, score 1.0, start verified, 2 refs; was 0x800fc4d8 | - | yes |
| Config24MB | 0x800FC530 | new (os) | align, score 1.0, start verified, 2 refs | - | yes |
| Config48MB | 0x800FC5B0 | new (os) | align, score 1.0, start verified, 2 refs | - | yes |
| __OSInitMemoryProtection | 0x800FC648 | new (os) | exact, score 1.0, start verified, 2 refs | - | yes |
| __OSCallResetFunctions | 0x800FCBB0 | new (os) | align, score 0.77, start verified, 1 ref | - | yes |
| OSResetSystem | 0x800FCCFC | new (os) | align, score 0.77, start verified, 2 refs | - | yes |
| __OSResetSWInterruptHandler | 0x800FCFE4 | moved (moved) | exact, score 1.0, start verified, 2 refs; was 0x800fcfec | - | yes |
| OSGetResetSwitchState | 0x800FD370 | new (os) | align, score 1.0, start verified, 2 refs | - | yes |
| WriteSramCallback | 0x800FD390 | moved (moved) | exact, score 1.0, start verified, 2 refs; was 0x800fd39c | - | yes |
| __OSUnlockSram | 0x800FDA38 | new (os) | align, score 1.0, start verified, 2 refs | - | yes |
| __OSUnlockSramEx | 0x800FDA5C | new (os) | align, score 1.0, start verified, 2 refs | - | yes |
| __OSSyncSram | 0x800FDA80 | new (os) | align, score 1.0, start verified, 2 refs | - | yes |
| OSGetGbsMode | 0x800FDE74 | new (os) | exact, score 1.0, start verified, 1 ref | - | yes |
| DefaultSwitchThreadCallback | 0x800FE0F8 | new (os) | align, score 1.0, start verified, 2 refs | - | yes |
| OSInitThreadQueue | 0x800FE254 | new (os) | align, score 1.0, start verified, 2 refs | thread_funcs.cpp:sched::os_init_thread_queue | yes |
| OSGetCurrentThread | 0x800FE264 | new (os) | align, score 1.0, start verified, 2 refs | thread_funcs.cpp:sched::os_get_current_thread | yes |
| __OSReschedule | 0x800FE77C | new (os) | exact, score 1.0, start verified, 2 refs | - | yes |
| OSExitThread | 0x800FE994 | moved (moved) | exact, score 1.0, start verified, 2 refs; was 0x800fe99c | thread_funcs.cpp:sched::os_exit_thread | yes |
| OSJoinThread | 0x800FEC34 | new (os) | exact, score 1.0, start verified, 1 ref | thread_funcs.cpp:sched::os_join_thread | yes |
| OSGetThreadPriority | 0x800FF41C | new (os) | align, score 1.0, start verified, 2 refs | thread_funcs.cpp:sched::os_get_thread_priority | yes |
| __init_user | 0x800FF8F8 | new (os) | align, score 1.0, start verified, 2 refs | - | yes |
| _ExitProcess | 0x800FF96C | new (os) | align, score 1.0, start verified, 2 refs | - | yes |
| PSMTXRotAxisRad | 0x801000E8 | new (os) | align, score 1.0, start verified, 2 refs | - | yes |
| C_MTXLightPerspective | 0x801004F8 | new (os) | exact, score 1.0, start verified, 2 refs | - | yes |
| __DVDInitWA | 0x80100EB4 | new (dvd) | exact, score 1.0, start verified, 2 refs | - | yes |
| __DVDInterruptHandler | 0x80100EF4 | moved (moved) | exact, score 1.0, start verified, 2 refs; was 0x80100f00 | - | yes |
| AlarmHandlerForTimeout | 0x80101258 | moved (moved) | exact, score 1.0, start verified, 2 refs; was 0x80101264 | - | yes |
| DVDLowRequestError | 0x801018E0 | new (dvd) | align, score 1.0, start verified, 2 refs | - | yes |
| DVDLowBreak | 0x80101C84 | new (dvd) | align, score 1.0, start verified, 2 refs | - | yes |
| cbForReadAsync | 0x80102468 | new (dvd) | exact, score 1.0, start verified, 2 refs | - | yes |
| cbForReadSync | 0x801025B0 | new (dvd) | align, score 1.0, start verified, 2 refs | - | yes |
| defaultOptionalCommandChecker | 0x80102640 | new (dvd) | align, score 1.0, start verified, 2 refs | - | yes |
| DVDInit | 0x80102644 | new (dvd) | exact, score 1.0, start verified, 2 refs | os_funcs.cpp:dvd_init_stub | yes |
| stateReadingFST | 0x80102710 | moved (moved) | exact, score 1.0, start verified, 2 refs; was 0x80102724 | - | yes |
| cbForStateReadingFST | 0x801027A4 | moved (moved) | exact, score 1.0, start verified, 2 refs; was 0x801027b0 | - | yes |
| stateTimeout | 0x801028DC | new (dvd) | exact, score 1.0, start verified, 2 refs | - | yes |
| stateGettingError | 0x80102910 | new (dvd) | align, score 1.0, start verified, 2 refs | - | yes |
| cbForUnrecoveredError | 0x80102C80 | moved (moved) | exact, score 1.0, start verified, 2 refs; was 0x80102c8c | - | yes |
| cbForUnrecoveredErrorRetry | 0x80102CE8 | new (dvd) | exact, score 1.0, start verified, 2 refs | - | yes |
| stateGoToRetry | 0x80102D80 | new (dvd) | align, score 1.0, start verified, 2 refs | - | yes |
| cbForStateGoToRetry | 0x80102DA8 | moved (moved) | exact, score 1.0, start verified, 2 refs; was 0x80102db4 | - | yes |
| stateCheckID3 | 0x80102FE0 | new (dvd) | align, score 1.0, start verified, 2 refs | - | yes |
| stateCheckID2a | 0x80103014 | new (dvd) | align, score 1.0, start verified, 2 refs | - | yes |
| cbForStateCheckID2a | 0x80103048 | moved (moved) | exact, score 1.0, start verified, 2 refs; was 0x80103054 | - | yes |
| stateCheckID2 | 0x801030BC | new (dvd) | exact, score 1.0, start verified, 2 refs | - | yes |
| cbForStateCheckID2 | 0x80103208 | moved (moved) | exact, score 1.0, start verified, 2 refs; was 0x80103214 | - | yes |
| cbForStateCheckID3 | 0x801032EC | moved (moved) | exact, score 1.0, start verified, 2 refs; was 0x801032f8 | - | yes |
| stateCoverClosed_CMD | 0x801034F8 | new (dvd) | exact, score 1.0, start verified, 2 refs | - | yes |
| cbForStateCoverClosed | 0x80103528 | moved (moved) | exact, score 1.0, start verified, 2 refs; was 0x80103534 | - | yes |
| stateMotorStopped | 0x80103598 | new (dvd) | align, score 1.0, start verified, 2 refs | - | yes |
| cbForStateMotorStopped | 0x801035C0 | moved (moved) | exact, score 1.0, start verified, 2 refs; was 0x801035d4 | - | yes |
| DVDSetAutoInvalidation | 0x80104884 | new (dvd) | align, score 1.0, start verified, 2 refs | - | yes |
| DVDResume | 0x80104894 | new (dvd) | exact, score 1.0, start verified, 1 ref | - | yes |
| cbForCancelSync | 0x80104C0C | new (dvd) | align, score 1.0, start verified, 2 refs | - | yes |
| DVDGetCurrentDiskID | 0x80104C30 | new (dvd) | align, score 1.0, start verified, 2 refs | - | yes |
| cb | 0x80105400 | moved (moved) | exact, score 1.0, start verified, 2 refs; was 0x8010540c | - | yes |
| getTiming | 0x801058B4 | new (misc) | version, score 1.0, start verified, 2 refs | - | yes |
| __VIInit | 0x80105954 | new (misc) | exact, score 1.0, start verified, 2 refs | - | yes |
| VIConfigurePan | 0x80106CF4 | new (misc) | exact, score 1.0, start verified, 1 ref | - | yes |
| VIGetRetraceCount | 0x801072A0 | new (misc) | align, score 1.0, start verified, 2 refs | - | yes |
| VIGetNextField | 0x8010734C | new (misc) | exact, score 1.0, start verified, 2 refs | - | yes |
| PADOriginCallback | 0x80107908 | moved (moved) | exact, score 1.0, start verified, 2 refs; was 0x80107914 | - | yes |
| PADOriginUpdateCallback | 0x801079CC | moved (moved) | exact, score 1.0, start verified, 2 refs; was 0x801079d8 | - | yes |
| PADProbeCallback | 0x80107A8C | moved (moved) | exact, score 1.0, start verified, 2 refs; was 0x80107a98 | - | yes |
| PADTypeAndStatusCallback | 0x80107B64 | moved (moved) | exact, score 1.0, start verified, 2 refs; was 0x80107b74 | - | yes |
| PADReceiveCheckCallback | 0x80107E90 | moved (moved) | exact, score 1.0, start verified, 2 refs; was 0x80107e98 | - | yes |
| AIGetStreamPlayState | 0x801093D8 | new (misc) | align, score 1.0, start verified, 2 refs | ai_funcs.cpp:ai_get_stream_play_state | yes |
| AIGetDSPSampleRate | 0x801094C8 | new (misc) | align, score 1.0, start verified, 2 refs | ai_funcs.cpp:ai_get_dsp_sample_rate | yes |
| AIGetStreamSampleRate | 0x801095B0 | new (misc) | align, score 1.0, start verified, 2 refs | - | yes |
| __AISHandler | 0x80109784 | moved (moved) | exact, score 1.0, start verified, 2 refs; was 0x8010978c | - | yes |
| __AIDHandler | 0x80109800 | moved (moved) | exact, score 1.0, start verified, 2 refs; was 0x80109814 | - | yes |
| ARRegisterDMACallback | 0x80109AE8 | new (misc) | align, score 1.0, start verified, 2 refs | ar_funcs.cpp:ar_register_dma_callback | yes |
| __ARHandler | 0x80109CF0 | moved (moved) | exact, score 1.0, start verified, 2 refs; was 0x80109d04 | - | yes |
| __ARQInterruptServiceRoutine | 0x8010B660 | moved (moved) | exact, score 1.0, start verified, 2 refs; was 0x8010b668 | - | yes |
| __CARDSyncCallback | 0x8010B91C | new (misc) | exact, score 1.0, start verified, 2 refs | - | yes |
| __CARDExtHandler | 0x8010B950 | moved (moved) | exact, score 1.0, start verified, 2 refs; was 0x8010b958 | - | yes |
| __CARDExiHandler | 0x8010BA28 | moved (moved) | exact, score 1.0, start verified, 2 refs; was 0x8010ba30 | - | yes |
| __CARDTxHandler | 0x8010BB40 | moved (moved) | exact, score 1.0, start verified, 2 refs; was 0x8010bb48 | - | yes |
| __CARDUnlockedHandler | 0x8010BBE8 | moved (moved) | exact, score 1.0, start verified, 2 refs; was 0x8010bbf0 | - | yes |
| TimeoutHandler | 0x8010BEC8 | moved (moved) | exact, score 1.0, start verified, 2 refs; was 0x8010bedc | - | yes |
| UnlockedCallback | 0x8010C198 | moved (moved) | exact, score 1.0, start verified, 2 refs; was 0x8010c1a4 | - | yes |
| __CARDEraseSector | 0x8010C6AC | new (misc) | exact, score 1.0, start verified, 2 refs | - | yes |
| __CARDGetFontEncode | 0x8010C838 | new (misc) | align, score 1.0, start verified, 2 refs | - | yes |
| InitCallback | 0x8010DAC8 | moved (moved) | exact, score 1.0, start verified, 2 refs; was 0x8010dadc | - | yes |
| DoneCallback | 0x8010DB38 | moved (moved) | exact, score 1.0, start verified, 2 refs; was 0x8010db4c | - | yes |
| BlockReadCallback | 0x8010DE5C | moved (moved) | exact, score 1.0, start verified, 2 refs; was 0x8010de64 | - | yes |
| BlockWriteCallback | 0x8010DF9C | new (misc) | align, score 1.0, start verified, 2 refs | - | yes |
| __CARDGetFatBlock | 0x8010E0DC | new (misc) | align, score 1.0, start verified, 2 refs | - | yes |
| __CARDGetDirBlock | 0x8010E4E0 | new (misc) | align, score 1.0, start verified, 2 refs | - | yes |
| FormatCallback | 0x801100F8 | moved (moved) | exact, score 1.0, start verified, 2 refs; was 0x80110100 | - | yes |
| __CARDAccess | 0x80110944 | new (misc) | exact, score 1.0, start verified, 2 refs | - | yes |
| __CARDIsWritable | 0x801109D8 | new (misc) | exact, score 1.0, start verified, 1 ref | - | yes |
| __CARDIsReadable | 0x80110B0C | new (misc) | exact, score 1.0, start verified, 1 ref | - | yes |
| CreateCallbackFat | 0x80110EC8 | moved (moved) | exact, score 1.0, start verified, 2 refs; was 0x80110ed0 | - | yes |
| ReadCallback | 0x80111418 | moved (moved) | exact, score 1.0, start verified, 2 refs; was 0x80111420 | - | yes |
| __GXDefaultTexRegionCallback | 0x8011203C | new (misc) | exact, score 1.0, start verified, 1 ref | - | yes |
| __GXShutdown | 0x8011215C | new (misc) | exact, score 1.0, start verified, 1 ref | - | yes |
| GXInit | 0x801122EC | new (misc) | fuzzy, score 0.63, start verified, 1 ref | - | yes |
| __GXInitGX | 0x80112A7C | new (misc) | version, score 1.0, start verified, 2 refs | - | yes |
| GXCPInterruptHandler | 0x801133B4 | moved (moved) | exact, score 1.0, start verified, 2 refs; was 0x801133bc | - | yes |
| GXInitFifoLimits | 0x801135C4 | new (misc) | align, score 1.0, start verified, 2 refs | - | yes |
| GXSetCPUFifo | 0x801135D0 | new (misc) | exact, score 1.0, start verified, 2 refs | - | yes |
| GXSetBreakPtCallback | 0x80113870 | new (misc) | align, score 1.0, start verified, 1 ref | - | yes |
| __GXFifoReadEnable | 0x80113900 | new (misc) | align, score 1.0, start verified, 2 refs | - | yes |
| __GXFifoReadDisable | 0x80113924 | new (misc) | align, score 1.0, start verified, 2 refs | - | yes |
| GXGetCPUFifo | 0x80113ADC | new (misc) | callgraph, score 1.0, start verified, 2 refs | - | yes |
| GXGetGPFifo | 0x80113AE4 | new (misc) | callgraph, score 1.0, start verified, 2 refs | - | yes |
| GXGetVtxAttrFmtv | 0x80114C34 | new (misc) | exact, score 1.0, start verified, 1 ref | - | yes |
| GXInvalidateVtxCache | 0x80114D34 | new (misc) | align, score 1.0, start verified, 2 refs | - | yes |
| __GXAbort | 0x801150F0 | new (misc) | exact, score 1.0, start verified, 1 ref | - | yes |
| GXAbortFrame | 0x8011525C | new (misc) | fuzzy, score 0.78, start verified, 1 ref | - | yes |
| GXPixModeSync | 0x80115500 | new (misc) | align, score 1.0, start verified, 2 refs | - | yes |
| GXPokeAlphaMode | 0x80115524 | new (misc) | align, score 1.0, start verified, 2 refs | - | yes |
| GXSetDrawSyncCallback | 0x80115648 | new (misc) | align, score 1.0, start verified, 2 refs | - | yes |
| GXTokenInterruptHandler | 0x8011568C | moved (moved) | exact, score 1.0, start verified, 2 refs; was 0x80115694 | - | yes |
| GXSetDrawDoneCallback | 0x80115714 | new (misc) | align, score 1.0, start verified, 2 refs | - | yes |
| GXFinishInterruptHandler | 0x80115758 | moved (moved) | exact, score 1.0, start verified, 2 refs; was 0x80115764 | - | yes |
| __GXSetDirtyState | 0x8011584C | new (misc) | exact, score 1.0, start verified, 2 refs | - | yes |
| GXBegin | 0x801158CC | new (misc) | exact, score 1.0, start verified, 2 refs | - | yes |
| GXSetCullMode | 0x80115AEC | new (misc) | align, score 0.76, start verified, 2 refs | - | yes |
| __GXSetGenMode | 0x80115B64 | new (misc) | align, score 1.0, start verified, 2 refs | - | yes |
| GXSetDispCopySrc | 0x80115CA8 | new (misc) | align, score 1.0, start verified, 1 ref | - | yes |
| GXSetTexCopySrc | 0x80115D24 | new (misc) | align, score 1.0, start verified, 1 ref | - | yes |
| GXSetDispCopyYScale | 0x80115F80 | new (misc) | exact, score 1.0, start verified, 2 refs | - | yes |
| GXInitLightPos | 0x80116628 | new (misc) | align, score 1.0, start verified, 2 refs | - | yes |
| GXInitTexObj | 0x80116BD4 | new (misc) | exact, score 1.0, start verified, 2 refs | - | yes |
| GXGetTexObjData | 0x80116FF0 | new (misc) | align, score 1.0, start verified, 1 ref | - | yes |
| GXGetTexObjWidth | 0x80116FFC | new (misc) | align, score 1.0, start verified, 2 refs | - | yes |
| GXGetTexObjHeight | 0x8011700C | new (misc) | align, score 1.0, start verified, 2 refs | - | yes |
| GXSetTexRegionCallback | 0x80117450 | renamed (renamed) | align, score 1.0, start verified, 2 refs; was GXSetTlutRegionCallback | - | yes |
| GXSetTlutRegionCallback | 0x80117464 | moved (moved) | align, score 1.0, start verified, 2 refs; was 0x80117450 | - | yes |
| GXSetTevIndWarp | 0x80117E68 | new (misc) | exact, score 1.0, start verified, 1 ref | - | yes |
| __GXUpdateBPMask | 0x80117ECC | new (misc) | align, score 1.0, start verified, 2 refs | - | yes |
| __GXFlushTextureState | 0x80117ED0 | new (misc) | align, score 1.0, start verified, 2 refs | - | yes |
| GXSetTevColorOp | 0x80118008 | new (misc) | align, score 1.0, start verified, 2 refs | - | yes |
| GXSetTevAlphaOp | 0x80118070 | new (misc) | align, score 1.0, start verified, 2 refs | - | yes |
| GXSetFog | 0x80118560 | new (misc) | fuzzy, score 0.68, start verified, 2 refs | - | yes |
| GXSetCurrentMtx | 0x80119670 | new (misc) | exact, score 1.0, start verified, 2 refs | - | yes |
| GXSetViewport | 0x80119840 | new (misc) | exact, score 1.0, start verified, 1 ref | - | yes |
| GXClearGPMetric | 0x8011A400 | new (misc) | callgraph, score 1.0, start verified, 1 ref | - | yes |
| DSPCheckMailToDSP | 0x8015B250 | new (exidspdb) | align, score 1.0, start verified, 2 refs | - | yes |
| DSPCheckMailFromDSP | 0x8015B260 | new (exidspdb) | align, score 1.0, start verified, 2 refs | - | yes |
| DSPReadMailFromDSP | 0x8015B270 | new (exidspdb) | align, score 1.0, start verified, 2 refs | - | yes |
| DSPSendMailToDSP | 0x8015B288 | new (exidspdb) | align, score 1.0, start verified, 2 refs | - | yes |
| DSPInit | 0x8015B29C | new (exidspdb) | exact, score 1.0, start verified, 2 refs | os_funcs.cpp:dsp_init_stub | no |
| DSPAssertInt | 0x8015B3A8 | new (exidspdb) | exact, score 1.0, start verified, 2 refs | - | yes |
| __DSP_debug_printf | 0x8015B468 | new (exidspdb) | align, score 1.0, start verified, 2 refs | - | yes |
| __DSP_exec_task | 0x8015B8DC | new (exidspdb) | align, score 1.0, start verified, 2 refs | - | yes |
| __DSP_boot_task | 0x8015BA7C | new (exidspdb) | align, score 1.0, start verified, 2 refs | - | yes |
| __DSP_insert_task | 0x8015BC08 | new (exidspdb) | align, score 1.0, start verified, 2 refs | - | yes |
| __DSP_remove_task | 0x8015BCA8 | new (exidspdb) | align, score 1.0, start verified, 2 refs | - | yes |
| DBInit | 0x8015EA48 | new (exidspdb) | exact, score 1.0, start verified, 2 refs | - | yes |
| __DBExceptionDestinationAux | 0x8015EA70 | new (exidspdb) | exact, score 1.0, start verified, 2 refs | - | yes |
| __DBIsExceptionMarked | 0x8015EAC8 | new (exidspdb) | exact, score 1.0, start verified, 2 refs | - | yes |
| DBPrintf | 0x8015EAE4 | new (exidspdb) | align, score 1.0, start verified, 2 refs | - | yes |
| SetExiInterruptMask | 0x8015EB34 | new (exidspdb) | align, score 1.0, start verified, 2 refs | - | yes |
| EXIImm | 0x8015EC28 | new (exidspdb) | callgraph, score 1.0, start verified, 2 refs | - | yes |
| EXIImmEx | 0x8015EE84 | new (exidspdb) | callgraph, score 1.0, start verified, 2 refs | - | yes |
| EXIDma | 0x8015EF24 | new (exidspdb) | callgraph, score 1.0, start verified, 2 refs | - | yes |
| EXISync | 0x8015F010 | new (exidspdb) | callgraph, score 1.0, start verified, 2 refs | - | yes |
| EXIClearInterrupts | 0x8015F25C | new (exidspdb) | align, score 1.0, start verified, 2 refs | - | yes |
| EXISetExiCallback | 0x8015F2A4 | new (exidspdb) | callgraph, score 1.0, start verified, 2 refs | - | yes |
| __EXIProbe | 0x8015F320 | new (exidspdb) | align, score 1.0, start verified, 2 refs | - | yes |
| EXIProbe | 0x8015F494 | new (exidspdb) | callgraph, score 1.0, start verified, 2 refs | - | yes |
| EXIProbeEx | 0x8015F514 | new (exidspdb) | callgraph, score 1.0, start verified, 2 refs | - | yes |
| EXIAttach | 0x8015F5C8 | new (exidspdb) | callgraph, score 1.0, start verified, 2 refs | - | yes |
| EXIDetach | 0x8015F6D4 | new (exidspdb) | callgraph, score 1.0, start verified, 2 refs | - | yes |
| EXISelect | 0x8015F790 | new (exidspdb) | callgraph, score 1.0, start verified, 2 refs | - | yes |
| EXIDeselect | 0x8015F8BC | new (exidspdb) | callgraph, score 1.0, start verified, 2 refs | - | yes |
| EXIIntrruptHandler | 0x8015F9CC | new (exidspdb) | align, score 1.0, start verified, 2 refs | - | yes |
| TCIntrruptHandler | 0x8015FA94 | new (exidspdb) | align, score 1.0, start verified, 2 refs | - | yes |
| EXTIntrruptHandler | 0x8015FCAC | new (exidspdb) | align, score 1.0, start verified, 2 refs | - | yes |
| EXIInit | 0x8015FD7C | new (exidspdb) | align, score 1.0, start verified, 2 refs | - | yes |
| EXILock | 0x8015FF50 | new (exidspdb) | callgraph, score 1.0, start verified, 2 refs | - | yes |
| EXIUnlock | 0x80160044 | new (exidspdb) | callgraph, score 1.0, start verified, 2 refs | - | yes |
| EXIGetState | 0x80160120 | new (exidspdb) | callgraph, score 1.0, start verified, 2 refs | - | yes |
| UnlockedHandler | 0x80160138 | new (exidspdb) | align, score 1.0, start verified, 2 refs | - | yes |
| EXIGetID | 0x80160160 | new (exidspdb) | callgraph, score 1.0, start verified, 2 refs | - | yes |
| ProbeBarnacle | 0x80160510 | new (exidspdb) | align, score 1.0, start verified, 1 ref | - | yes |
| __OSEnableBarnacle | 0x8016069C | new (exidspdb) | align, score 1.0, start verified, 1 ref | - | yes |
| InitializeUART | 0x80160858 | new (exidspdb) | align, score 1.0, start verified, 2 refs | - | yes |
| WriteUARTN | 0x801608C8 | new (exidspdb) | align, score 0.81, start unverified, 2 refs | - | yes |
| SIBusy | 0x80160AE4 | new (exidspdb) | align, score 1.0, start verified, 2 refs | - | yes |
| SIIsChanBusy | 0x80160B04 | new (exidspdb) | align, score 1.0, start verified, 2 refs | - | yes |
| CompleteTransfer | 0x80160C40 | new (exidspdb) | fuzzy, score 0.66, start unverified, 2 refs | - | yes |
| SIInterruptHandler | 0x80160E3C | new (exidspdb) | exact, score 1.0, start verified, 2 refs | - | yes |
| SIEnablePollingInterrupt | 0x80161180 | new (exidspdb) | align, score 1.0, start verified, 2 refs | - | yes |
| SIRegisterPollingHandler | 0x80161218 | new (exidspdb) | align, score 1.0, start verified, 2 refs | - | yes |
| SIUnregisterPollingHandler | 0x801612E4 | new (exidspdb) | align, score 1.0, start verified, 2 refs | - | yes |
| SIInit | 0x801613D8 | new (exidspdb) | align, score 1.0, start verified, 2 refs | - | yes |
| __SITransfer | 0x8016148C | new (exidspdb) | exact, score 1.0, start verified, 2 refs | - | yes |
| SIGetStatus | 0x80161698 | new (exidspdb) | align, score 1.0, start verified, 2 refs | - | yes |
| SISetCommand | 0x80161714 | new (exidspdb) | align, score 1.0, start verified, 2 refs | - | yes |
| SITransferCommands | 0x80161728 | new (exidspdb) | align, score 1.0, start verified, 2 refs | - | yes |
| SISetXY | 0x80161738 | new (exidspdb) | align, score 1.0, start verified, 2 refs | - | yes |
| SIEnablePolling | 0x801617A4 | new (exidspdb) | align, score 1.0, start verified, 2 refs | - | yes |
| SIDisablePolling | 0x80161840 | new (exidspdb) | align, score 1.0, start verified, 2 refs | - | yes |
| SIGetResponseRaw | 0x801618AC | new (exidspdb) | align, score 1.0, start verified, 2 refs | - | yes |
| SIGetResponse | 0x80161980 | new (exidspdb) | align, score 1.0, start verified, 2 refs | - | yes |
| SITransfer | 0x80161AD0 | new (exidspdb) | align, score 1.0, start verified, 2 refs | - | yes |
| GetTypeCallback | 0x80161C3C | new (exidspdb) | align, score 1.0, start verified, 2 refs | - | yes |
| SIGetType | 0x80161ED4 | new (exidspdb) | align, score 1.0, start verified, 2 refs | - | yes |
| SIGetTypeAsync | 0x80162098 | new (exidspdb) | align, score 1.0, start verified, 2 refs | - | yes |
| SISetSamplingRate | 0x801621D4 | new (exidspdb) | align, score 1.0, start verified, 2 refs | - | yes |
| SIRefreshSamplingRate | 0x801622B8 | new (exidspdb) | align, score 1.0, start verified, 2 refs | - | yes |
