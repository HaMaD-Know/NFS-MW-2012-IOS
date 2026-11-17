// Minimal arm64 fiber context switch for iOS, where getcontext/makecontext/swapcontext are not
// supported (device result 2026-11-17: getcontext -> ENOTSUP). The same code is used by the
// ReXGlue patch patches/rexglue-ios/0005-ios-arm64-fibers.patch.
//
// A switch saves and restores only what the AArch64 calling convention requires a called
// function to preserve: x19-x28, the frame pointer x29, the link register x30, sp, and the low
// 64 bits of v8-v15 (d8-d15). x18 is reserved by Apple's platform and is never touched.
#ifndef NFSMW_REX_FIBER_ARM64_H
#define NFSMW_REX_FIBER_ARM64_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct rex_fiber_context {
  uint64_t x[12];  // x19..x28, x29 (fp), x30 (lr)   offsets 0..88
  uint64_t sp;     //                                offset 96
  uint64_t d[8];   // d8..d15                        offsets 104..160
} rex_fiber_context;

/** Saves the current context into `from` and resumes `to`. Returns when something switches back. */
void rex_fiber_switch(rex_fiber_context *from, const rex_fiber_context *to);

/** Prepares `ctx` so that the first switch to it calls entry(arg) on the given stack. */
void rex_fiber_prepare(rex_fiber_context *ctx, void *stack, size_t stack_size,
                       void (*entry)(void *), void *arg);

#ifdef __cplusplus
}
#endif

#endif
