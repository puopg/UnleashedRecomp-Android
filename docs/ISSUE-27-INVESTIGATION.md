# Issue #27 ("ring crash"): root cause and fix

## Summary

The ring crash, and the broken gameplay around it on the AYN Thor, came from one line of host glue: the game's memory copy routines were mapped to the host `memcpy`.
The game relies on its own copy routines handling overlapping ranges, and the Thor's libc `memcpy` does not.
The corruption started in Havok's collision broadphase on the first object removal, a second after launch, and everything else followed from there.
Havok and the game code were never at fault.

The fix maps every guest copy routine to `memmove` (`UnleashedRecomp/misc_impl.cpp`).

## Symptoms on the Thor

- The ring crash (a guest assertion in the collision filter's category lookup, or a fault on freed memory), usually within the first minute.
- Flat dash panels, the loop's camera trigger, rings, bounce pads and breakable pots that did nothing.
- Sonic falling off the first loop instead of staying on it.
- Freezes, and objects that stopped reacting altogether after a while.
- The stock 0.5.2 release felt "off" on the same device (clipping, missed triggers), which the same corruption explains.

## Root cause

The game's CRT copy routines (`sub_831B0ED0`, `sub_831B1358` and four more) copy forwards.
Guest code relies on that when the ranges overlap with `dst < src`.
The game's own `memmove` (`sub_831B5E00`) calls its `memcpy` for exactly that case.

Havok's `hkp3AxisSweep` broadphase keeps three sorted arrays of AABB endpoints, one per axis.
Removing an object (`sub_82F6DD20`) deletes its two endpoints from each axis by shifting the rest of the array down in place with `sub_831B1358`: an overlapping copy with `dst < src`.

The port mapped these routines to host `memcpy`, which C leaves undefined for overlapping ranges.
MSVC's, glibc's and macOS's `memcpy` are overlap-safe in practice, so the PC builds never showed the problem.
The Thor's libc (bionic, from the `com.android.runtime` APEX) gets 659 of 960 small overlapping copies wrong, measured on the device, and that corrupts an overlapping shift.

With the axes damaged, the sweep reports some overlaps twice, misses others entirely, and pairs objects with the sentinel node.
Missed overlaps are the dead triggers and pickups.
Duplicate and stale pairs leave collision agents behind for freed objects, and the next contact callback through one of them is the ring crash.

## Evidence

- A per-call consistency check on the broadphase axes (diagnostic builds 12-17) found the first damage in a single `removeObject` at 1.4 s, while the start-up world removed its border phantoms.
  The axes were consistent before the call and not after: on x and y one endpoint named a node index past the end of the node array, and z was fine.
  The same call broke the same way in every run.
- The complete state before that call was dumped from the device and replayed offline on a Mac, running the recompiled broadphase code.
  With an overlap-safe copy the removal is correct.
  With a copy that does the head of a small range first, then the tail, it reproduces the device's damage exactly: one bad endpoint on x, one on y, and z identical to the correct result.
  The other copy shapes tried (backward chunks, tail first) also damage z, which the device did not.
- `libmain.so` imports `memcpy@LIBC`, so on the device it is the system libc's implementation.
- A startup probe on the Thor compared `memcpy` with `memmove` on 960 small overlapping copies (1 to 96 bytes, shifted by 1 to 16 bytes either way): 659 came out different.
- With the copy routines mapped to `memmove` and none of the investigation's Havok hooks, the tester played through the first stage on the Thor with no crash, and gameplay felt correct.
  In three minutes of play the broadphase check covered 104,695 method calls with no damage, no collision agent was ever created twice (the release build had duplicates within 32 s), and the release's own ring-crash patches never fired.

Ruled out along the way, each by a device experiment: races and thread scheduling (all guest threads pinned to one core changed nothing), FPCR state, Havok memory limits, out-of-range AABBs, the optimiser (`-O0`), and a misaligned guest stack.

## The fix

`UnleashedRecomp/misc_impl.cpp` maps all eight guest copy routines, including the game's `memmove`, to host `memmove`.
`memmove` gives the guest routines' result for every copy they are used for, and matches what the PC builds get in practice.

## Other fixes kept

These are correctness fixes found during the investigation.
None of them was the cause, but each fixes real behaviour on ARM.

- **PowerPC barriers** (`tools/XenonRecomp`): `sync`, `lwsync`, `eieio` and `isync` were emitted as nothing.
  They now lower to `std::atomic_thread_fence`, which is free on x86-64 and a real `dmb ish` on AArch64.
  `lwarx`/`ldarx` load through `volatile`, so a retry loop re-reads the reserved word.
- **FPSCR rounding modes on ARM** (`ppc_context.h`): the ARM table had +inf and -inf swapped, and `setcsr` now passes a 64-bit operand to `msr fpcr`.
- **Guest stack alignment**: `guest_stack_var` and the aspect-ratio CSD patch keep `r1` 16-byte aligned.
  Guest VMX code addresses stack slots with `lvx`/`stvx`, which ignore the low four bits.
- **Crash and hang reports** (`os/android/logger_android.cpp`): the crash report is written in one `write` so other threads cannot interleave into it, it includes a frame-pointer backtrace, and a hang samples the registers and stacks of running threads.
- **Build**: the vendored zstd `build/` directory is tracked (XenosRecomp cannot configure without it), and `gradlew` and `dxc-macos` are executable.

## Removed

- The 0.5.0-0.5.2 ring-crash patches in `patches/misc_patches.cpp`: small-block free quarantine and poisoning (`sub_82EA8AB0`), dead-child repair in `sub_82F768E0`, and skipping agents in `sub_82F77188`.
  They read the Havok collision agents as animation nodes and compensated for the damage this bug caused.
  Their own comment noted that some devices crashed on every ring pickup while others never did.
- Everything the investigation added to detect or survive the corruption: Havok ordering, agent and pair repair hooks, heap quarantine and pointer validation, null-handle guards in kernel imports, stale vertex and index buffer tracking, the unaligned store-conditional handler, and the diagnostic logging.
  The full history is in this file's history in git.

## Lessons

- The collision structures were first read as animation nodes, and many rounds went into repairing their symptoms.
  Checking class names and vtables against the executable's `.rdata` identified them as Havok.
- When the anomalies were the same on every run and with every guest thread on one core, they were not races.
  Deterministic corruption points at a deterministic cause.
- Dumping the state before the first bad call and replaying it offline turned a four-minute device cycle into a seconds-long experiment, and the replay's one difference from the device (its copy stub) was the answer.
- Undefined behaviour in host glue can look like a bug in the guest: the same code works wherever the host library happens to be forgiving.

## Building

```sh
export ANDROID_NDK_HOME="$HOME/Library/Android/sdk/ndk/29.0.14206865"
cmake --build out/build/android-arm64 --target UnleashedRecomp main_hook file_redirect_hook gsl_alloc_hook hook_impl
cp out/build/android-arm64/UnleashedRecomp/libmain.so android-apk/app/src/main/jniLibs/arm64-v8a/
cd android-apk && ./gradlew --no-daemon clean assembleDebug
```

Editing `tools/XenonRecomp/**` or `UnleashedRecompLib/config/SWA.toml` also requires regenerating the recompiled sources, run from `UnleashedRecompLib/config`:

```sh
cmake --build out/build/host-tools --target XenonRecomp
../../out/build/host-tools/tools/XenonRecomp/XenonRecomp/XenonRecomp SWA.toml ../../tools/XenonRecomp/XenonUtils/ppc_context.h
```

The build needs NDK r29; older NDKs lack `std::atomic_ref`.
Use `clean assembleDebug`: incremental packaging can leave a stale `libmain.so` in the APK.

Crash lines print `libmain.so+OFFSET`; resolve them against the unstripped library of the exact build that produced the log:

```sh
"$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/darwin-x86_64/bin/llvm-symbolizer" \
    --obj=out/build/android-arm64/UnleashedRecomp/libmain.so --demangle 0x<OFFSET>
```
