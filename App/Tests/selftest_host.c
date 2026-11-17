// Runs the shell's runtime self-tests on the macOS CI runner (Apple silicon).
// Purpose: catch mistakes in the test code itself before it reaches the iPad.
// It says nothing about iPadOS: macOS allows shm_open and ucontext, which the iPad refused.
#include "../Sources/SelfTest/SelfTest.h"

#include <stdio.h>

static void out(const char *line) {
  printf("%s\n", line);
  fflush(stdout);
}

int main(void) {
  struct {
    const char *name;
    int (*run)(selftest_log_fn);
    int must_pass_on_mac;
  } tests[] = {
      {"Address space", selftest_address_space, 1},
      {"Memory A: temporary file", selftest_backing_file, 1},
      {"Memory B: Mach memory entry", selftest_backing_mach_entry, 1},
      {"Memory C: vm_remap", selftest_backing_vm_remap, 1},
      {"Fibers: new arm64 switch", selftest_fibers_arm64, 1},
      {"Original shm_open method", selftest_guest_memory, 0},
      {"Original ucontext fibers", selftest_fibers, 0},
      {"Original fault handling (shm)", selftest_fault_handling, 0},
  };
  int failures = 0;
  for (size_t i = 0; i < sizeof tests / sizeof tests[0]; i++) {
    printf("--- %s\n", tests[i].name);
    const int passed = tests[i].run(out);
    if (!passed && tests[i].must_pass_on_mac) failures++;
    printf("::%s title=Host self-test::%s: %s\n", passed ? "notice" : "warning", tests[i].name,
           passed ? "PASS" : "FAIL");
  }
  printf("Host self-test: %d required test(s) failed\n", failures);
  return failures == 0 ? 0 : 1;
}
