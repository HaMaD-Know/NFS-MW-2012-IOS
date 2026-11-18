// Runs the shell's Vulkan probe (RuntimeProbe.c) on the macOS CI runner against the macOS
// MoltenVK from the Vulkan SDK. If it works here but crashes on the iPad, the probe code is fine
// and the iOS MoltenVK library (or its use on iOS) is at fault.
#include "../Sources/SelfTest/SelfTest.h"

#include <stdio.h>

static void out(const char *line) {
  printf("%s\n", line);
  fflush(stdout);
}

int main(int argc, char **argv) {
  if (argc < 2) {
    fprintf(stderr, "usage: moltenvk_host <folder containing libMoltenVK.dylib>\n");
    return 2;
  }
  selftest_set_frameworks_dir(argv[1]);
  const int passed = selftest_moltenvk(out);
  printf("::%s title=Vulkan probe on macOS MoltenVK::%s\n", passed ? "notice" : "warning",
         passed ? "PASS" : "FAIL");
  return passed ? 0 : 1;
}
