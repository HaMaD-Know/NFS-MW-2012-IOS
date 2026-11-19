// Runs the importer's checker self-test (GameImport/GameCheck.c) on the macOS CI runner.
// Builds synthetic game folders (no game content) and checks that every verification state and
// message comes out right. The iPad runs the same test from the shell's "Importer self-test".
#include "../Sources/GameImport/GameCheck.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static void out(const char *line) {
  printf("%s\n", line);
  fflush(stdout);
}

int main(void) {
  char work[] = "/tmp/gamecheck-XXXXXX";
  if (!mkdtemp(work)) {
    perror("mkdtemp");
    return 2;
  }
  const int passed = gamecheck_selftest(work, out);
  printf("::%s title=Importer checks on macOS::%s\n", passed ? "notice" : "error",
         passed ? "PASS" : "FAIL");
  return passed ? 0 : 1;
}
