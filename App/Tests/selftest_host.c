// Runs the shell's runtime self-tests on the macOS CI runner (Apple silicon).
// Purpose: catch mistakes in the test code itself before it reaches the iPad.
// It says nothing about iPadOS: macOS allows shm_open and ucontext, which the iPad refused.
#include "../Sources/SelfTest/SelfTest.h"

#include <stdio.h>
#include <string.h>

static char g_buffer[16384];

static void out(const char *line) {
  printf("%s\n", line);
  fflush(stdout);
  // Collect for the annotation; GitHub annotations encode newlines as %0A.
  if (strlen(g_buffer) + 3 * strlen(line) + 4 < sizeof g_buffer) {
    for (const char *c = line; *c; c++) {
      if (*c == '%') {
        strcat(g_buffer, "%25");
      } else {
        strncat(g_buffer, c, 1);
      }
    }
    strcat(g_buffer, "%0A");
  }
}

int main(void) {
  struct {
    const char *name;
    int (*run)(selftest_log_fn);
  } tests[] = {
      {"Address space", selftest_address_space},
      {"Memory A: temporary file", selftest_backing_file},
      {"Memory B: Mach memory entry", selftest_backing_mach_entry},
      {"Memory C: vm_remap", selftest_backing_vm_remap},
      {"Fibers: new arm64 switch", selftest_fibers_arm64},
      {"Original shm_open method", selftest_guest_memory},
      {"Original ucontext fibers", selftest_fibers},
      {"Original fault handling (shm)", selftest_fault_handling},
  };
  // On macOS every test is expected to pass, including the original methods that iPadOS refuses.
  int failures = 0;
  for (size_t i = 0; i < sizeof tests / sizeof tests[0]; i++) {
    printf("--- %s\n", tests[i].name);
    g_buffer[0] = 0;
    const int passed = tests[i].run(out);
    if (!passed) failures++;
    printf("::%s title=Host self-test: %s %s::%s\n", passed ? "notice" : "warning", tests[i].name,
           passed ? "PASS" : "FAIL", g_buffer);
  }
  printf("Host self-test: %d test(s) failed\n", failures);
  return failures == 0 ? 0 : 1;
}
