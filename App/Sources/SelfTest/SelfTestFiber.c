// ucontext fiber self-test. See SelfTest.h.
// Darwin exposes the ucontext functions only with an X/Open feature level, as ReXGlue's
// fiber_posix.cpp does, and marks them deprecated; this test checks that they really work.
#define _XOPEN_SOURCE 600

#include "SelfTest.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ucontext.h>

#pragma clang diagnostic ignored "-Wdeprecated-declarations"

static ucontext_t g_caller;
static ucontext_t g_fiber;
static volatile int g_counter;
static volatile double g_float_check;

static void fiber_entry(void) {
  g_counter += 1;
  g_float_check = g_float_check * 2.5;  // floating-point state across the switch
  swapcontext(&g_fiber, &g_caller);
  g_counter += 10;
  swapcontext(&g_fiber, &g_caller);
  // not resumed again
}

int selftest_fibers(selftest_log_fn log) {
  char line[256];
  const size_t stack_size = 256 * 1024;
  void *stack = malloc(stack_size);
  if (!stack) {
    log("FAIL: fiber stack allocation");
    return 0;
  }
  if (getcontext(&g_fiber) != 0) {
    snprintf(line, sizeof line, "FAIL: getcontext: %s (errno %d)", strerror(errno), errno);
    log(line);
    free(stack);
    return 0;
  }
  g_fiber.uc_stack.ss_sp = stack;
  g_fiber.uc_stack.ss_size = stack_size;
  g_fiber.uc_link = &g_caller;
  makecontext(&g_fiber, fiber_entry, 0);
  log("getcontext and makecontext: ok");

  g_counter = 0;
  g_float_check = 2.0;
  if (swapcontext(&g_caller, &g_fiber) != 0) {
    snprintf(line, sizeof line, "FAIL: first swapcontext: %s (errno %d)", strerror(errno), errno);
    log(line);
    free(stack);
    return 0;
  }
  const int first = g_counter;
  const double float_value = g_float_check;
  if (swapcontext(&g_caller, &g_fiber) != 0) {
    snprintf(line, sizeof line, "FAIL: second swapcontext: %s (errno %d)", strerror(errno), errno);
    log(line);
    free(stack);
    return 0;
  }
  const int second = g_counter;
  free(stack);

  const int ok = first == 1 && second == 11 && float_value == 5.0;
  snprintf(line, sizeof line, "Round trips: counter %d then %d (expected 1, 11), float %.1f (expected 5.0)",
           first, second, float_value);
  log(line);
  log(ok ? "PASS: ucontext fibers" : "FAIL: ucontext fibers");
  return ok;
}
