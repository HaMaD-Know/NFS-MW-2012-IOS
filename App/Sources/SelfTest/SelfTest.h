// Runtime self-tests for Milestone N1-b (step B2).
// They reproduce, with plain system calls, the three mechanisms the ReXGlue runtime relies on,
// so that their behaviour on a real iPad is known before ReXGlue itself is ported:
//   1. address space: the 4.5 GB guest reservation and its nine shared-memory views
//      (copied from ReXGlue ebac1cf, src/kernel/memory.cpp kViewRanges / MapViewsMac)
//   2. memory-fault handling: SIGBUS/SIGSEGV on a protected shared page, handled and resumed
//      (as src/base/exception_handler_posix.cpp does for MMIO and write watches)
//   3. fibers: getcontext / makecontext / swapcontext (src/base/fiber_posix.cpp)
// No game code or data is involved.
#ifndef NFSMW_SELFTEST_H
#define NFSMW_SELFTEST_H

#ifdef __cplusplus
extern "C" {
#endif

/** Receives one human-readable result line. Called synchronously on the test thread. */
typedef void (*selftest_log_fn)(const char *line);

/** Largest single PROT_NONE reservation and total reservable address space. 1 = can hold 4.5 GB. */
int selftest_address_space(selftest_log_fn log);

/** ReXGlue's guest layout: 4.5 GB shm_open object, 4.5 GB reservation, nine MAP_FIXED views, alias checks. */
int selftest_guest_memory(selftest_log_fn log);

/** Write and read faults on a protected shared page: caught, page unprotected, access resumed. */
int selftest_fault_handling(selftest_log_fn log);

/** ucontext fibers: two round trips between the calling thread and a fiber. */
int selftest_fibers(selftest_log_fn log);

// --- Replacements (added after the first device run: shm_open EPERM, getcontext ENOTSUP) ---

/** Guest layout backed by a deleted temporary file in TMPDIR (drop-in for shm_open). */
int selftest_backing_file(selftest_log_fn log);

/** Guest layout backed by a Mach named memory entry, views mapped with vm_map. */
int selftest_backing_mach_entry(selftest_log_fn log);

/** Guest layout from plain memory, mirrors created with vm_remap. */
int selftest_backing_vm_remap(selftest_log_fn log);

/** arm64 fiber switch (RexFiberArm64.c): round trips, fiber-to-fiber, deep stack, speed. */
int selftest_fibers_arm64(selftest_log_fn log);

// --- Packaging probe (RuntimeProbe.c) ---

/** Sets the app's Frameworks folder, where CI places the ReXGlue libraries and MoltenVK. */
void selftest_set_frameworks_dir(const char *path);

/** dlopens every ReXGlue library in Frameworks and reads each module's build identity. */
int selftest_runtime_libraries(selftest_log_fn log);

/** Loads MoltenVK, creates a Vulkan instance and lists the physical devices. */
int selftest_moltenvk(selftest_log_fn log);

/** Sets the app's Documents folder; the ReXGlue runtime keeps its files and log there. */
void selftest_set_documents_dir(const char *path);

/**
 * Starts the real ReXGlue app framework with no game, no window and no drivers, then stops it
 * (step B7). Calls libnfsmwhost.dylib from Frameworks, built only for the runtime IPA.
 */
int selftest_rexglue_headless(selftest_log_fn log);

/**
 * Step B7 part 2: starts ReXGlue with the Vulkan graphics driver, an SDL3 window and SDL3
 * controller input, runs the window for `seconds`, then stops. Must run on the main thread.
 */
int selftest_rexglue_window(selftest_log_fn log, int seconds);

/**
 * Everything in one ReXGlue start (main thread): window, Vulkan graphics, controller, audio tone,
 * guest memory, fibers, shutdown. Reports "RESULT|PASS|name|detail" lines through log.
 */
int selftest_rexglue_all(selftest_log_fn log, int seconds);

/** 1 when this app was built with the game's code (the private build only). */
int selftest_rexglue_has_game(void);

/**
 * Starts the game (private build only): the launcher from code_folder, the game's files from the
 * disc image iso. Main thread; returns when the game ends.
 */
int selftest_rexglue_play(selftest_log_fn log, const char *iso, const char *code_folder);

// --- Diagnostics (CrashLog.c) ---

/** Records signal, address and call stack of a crash into the log file at log_path. */
void crashlog_install(const char *log_path);

/** Sends stderr (e.g. MoltenVK's own messages) into the same log file. */
void crashlog_redirect_stderr(void);

#ifdef __cplusplus
}
#endif

#endif
