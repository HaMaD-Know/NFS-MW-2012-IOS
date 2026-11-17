// Self-test for the arm64 fiber switch (RexFiberArm64.c). See SelfTest.h.
#include "SelfTest.h"
#include "RexFiberArm64.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static void say(selftest_log_fn log, const char *fmt, ...) {
  char buffer[512];
  va_list args;
  va_start(args, fmt);
  vsnprintf(buffer, sizeof buffer, fmt, args);
  va_end(args);
  if (log) log(buffer);
}

static rex_fiber_context g_main, g_a, g_b, g_deep, g_ping;
static volatile int g_counter;
static volatile double g_float;
static volatile uint64_t g_deep_result;
static volatile uint64_t g_ping_count;

static void fiber_a(void *arg) {
  (void)arg;
  g_counter += 1;
  g_float = g_float * 2.5;
  rex_fiber_switch(&g_a, &g_main);
  g_counter += 10;
  rex_fiber_switch(&g_a, &g_b);  // fiber to fiber
  g_counter += 1000;
  for (;;) rex_fiber_switch(&g_a, &g_main);
}

static void fiber_b(void *arg) {
  (void)arg;
  g_counter += 100;
  for (;;) rex_fiber_switch(&g_b, &g_a);
}

// Uses about 1 KB of stack per level, to show the fiber really runs on its own large stack.
static uint64_t deep(int level) {
  volatile uint8_t pad[1000];
  for (int i = 0; i < (int)sizeof pad; i += 64) pad[i] = (uint8_t)level;
  if (level == 0) return pad[0];
  return deep(level - 1) + (uint64_t)level + pad[64];
}

static void fiber_deep(void *arg) {
  const int levels = (int)(intptr_t)arg;
  g_deep_result = deep(levels);
  for (;;) rex_fiber_switch(&g_deep, &g_main);
}

static void fiber_ping(void *arg) {
  (void)arg;
  for (;;) {
    g_ping_count++;
    rex_fiber_switch(&g_ping, &g_main);
  }
}

static double now_ms(void) {
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return (double)t.tv_sec * 1000.0 + (double)t.tv_nsec / 1e6;
}

int selftest_fibers_arm64(selftest_log_fn log) {
  const size_t stack_size = 512 * 1024;
  void *stack_a = malloc(stack_size), *stack_b = malloc(stack_size);
  void *stack_d = malloc(stack_size), *stack_p = malloc(stack_size);
  if (!stack_a || !stack_b || !stack_d || !stack_p) {
    say(log, "FAIL: fiber stack allocation");
    free(stack_a), free(stack_b), free(stack_d), free(stack_p);
    return 0;
  }

  // 1. Round trips, fiber-to-fiber switch, and values the compiler keeps in callee-saved
  //    registers across each switch (locals of this function).
  rex_fiber_prepare(&g_a, stack_a, stack_size, fiber_a, NULL);
  rex_fiber_prepare(&g_b, stack_b, stack_size, fiber_b, NULL);
  g_counter = 0;
  g_float = 2.0;
  volatile double keep_float = 1.25;
  long keep_int = 4242;
  double f1 = keep_float * 3.0;
  rex_fiber_switch(&g_main, &g_a);
  const int after_first = g_counter;
  const double float_value = g_float;
  rex_fiber_switch(&g_main, &g_a);
  const int after_second = g_counter;
  const int registers_ok = f1 == 3.75 && keep_int == 4242 && keep_float == 1.25;
  const int round_trip_ok = after_first == 1 && after_second == 1111 && float_value == 5.0;
  say(log, "Round trips: counter %d then %d (expected 1, 1111), float %.1f (expected 5.0), saved values %s",
      after_first, after_second, float_value, registers_ok ? "intact" : "CORRUPTED");

  // 2. Deep stack use inside a fiber (about 200 KB).
  rex_fiber_prepare(&g_deep, stack_d, stack_size, fiber_deep, (void *)(intptr_t)200);
  g_deep_result = 0;
  rex_fiber_switch(&g_main, &g_deep);
  uint64_t expected = 0;
  for (int level = 1; level <= 200; level++) expected += (uint64_t)level + (uint8_t)level;
  expected += 0;  // level 0 returns pad[0] == 0
  const int deep_ok = g_deep_result == expected;
  say(log, "Deep stack in a fiber (200 levels, ~200 KB): result %llu, expected %llu -> %s",
      (unsigned long long)g_deep_result, (unsigned long long)expected, deep_ok ? "ok" : "WRONG");

  // 3. Speed: one million round trips (two million switches).
  rex_fiber_prepare(&g_ping, stack_p, stack_size, fiber_ping, NULL);
  g_ping_count = 0;
  const int rounds = 1000000;
  const double t0 = now_ms();
  for (int i = 0; i < rounds; i++) rex_fiber_switch(&g_main, &g_ping);
  const double elapsed = now_ms() - t0;
  const int speed_ok = g_ping_count == (uint64_t)rounds;
  say(log, "Speed: %d round trips in %.1f ms (%.1f ns per switch), count %s", rounds, elapsed,
      elapsed * 1e6 / (2.0 * rounds), speed_ok ? "ok" : "WRONG");

  free(stack_a), free(stack_b), free(stack_d), free(stack_p);
  const int ok = round_trip_ok && registers_ok && deep_ok && speed_ok;
  say(log, "%s: arm64 fiber switch (replacement for ucontext)", ok ? "PASS" : "FAIL");
  return ok;
}
