// arm64 fiber context switch. See RexFiberArm64.h.
#include "RexFiberArm64.h"

#include <stdlib.h>
#include <string.h>

#if !defined(__aarch64__) && !defined(__arm64__)
#error "RexFiberArm64.c is for arm64 only"
#endif

// x0 = from, x1 = to. Store callee-saved state into *from, load it from *to, return into to->lr.
__attribute__((naked)) void rex_fiber_switch(rex_fiber_context *from, const rex_fiber_context *to) {
  __asm__ volatile(
      "stp x19, x20, [x0, #0]\n"
      "stp x21, x22, [x0, #16]\n"
      "stp x23, x24, [x0, #32]\n"
      "stp x25, x26, [x0, #48]\n"
      "stp x27, x28, [x0, #64]\n"
      "stp x29, x30, [x0, #80]\n"
      "mov x9, sp\n"
      "str x9, [x0, #96]\n"
      "stp d8, d9, [x0, #104]\n"
      "stp d10, d11, [x0, #120]\n"
      "stp d12, d13, [x0, #136]\n"
      "stp d14, d15, [x0, #152]\n"
      "ldp x19, x20, [x1, #0]\n"
      "ldp x21, x22, [x1, #16]\n"
      "ldp x23, x24, [x1, #32]\n"
      "ldp x25, x26, [x1, #48]\n"
      "ldp x27, x28, [x1, #64]\n"
      "ldp x29, x30, [x1, #80]\n"
      "ldr x9, [x1, #96]\n"
      "mov sp, x9\n"
      "ldp d8, d9, [x1, #104]\n"
      "ldp d10, d11, [x1, #120]\n"
      "ldp d12, d13, [x1, #136]\n"
      "ldp d14, d15, [x1, #152]\n"
      "ret\n");
}

// First instructions of a new fiber: x19 = entry, x20 = arg (set by rex_fiber_prepare).
__attribute__((naked)) static void rex_fiber_start(void) {
  __asm__ volatile(
      "mov x0, x20\n"
      "blr x19\n"
      // A fiber entry must never return: there is no caller frame to return to.
      "brk #0xf1\n");
}

void rex_fiber_prepare(rex_fiber_context *ctx, void *stack, size_t stack_size,
                       void (*entry)(void *), void *arg) {
  memset(ctx, 0, sizeof *ctx);
  const uintptr_t top = ((uintptr_t)stack + stack_size) & ~(uintptr_t)15;  // 16-byte aligned
  ctx->x[0] = (uint64_t)(uintptr_t)entry;            // x19
  ctx->x[1] = (uint64_t)(uintptr_t)arg;              // x20
  ctx->x[10] = 0;                                    // x29: end of the frame chain
  ctx->x[11] = (uint64_t)(uintptr_t)&rex_fiber_start;  // x30: where the first switch "returns"
  ctx->sp = (uint64_t)top;
}
