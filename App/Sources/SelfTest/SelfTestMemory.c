// Address-space, guest-memory and fault-handling self-tests. See SelfTest.h.
#define _DARWIN_C_SOURCE 1

#include "SelfTest.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

static void say(selftest_log_fn log, const char *fmt, ...) {
  char buffer[512];
  va_list args;
  va_start(args, fmt);
  vsnprintf(buffer, sizeof buffer, fmt, args);
  va_end(args);
  if (log) log(buffer);
}

#define GB (1ull << 30)
#define MB (1ull << 20)

// ---------------------------------------------------------------------------------------------
// 1. Address space
// ---------------------------------------------------------------------------------------------
int selftest_address_space(selftest_log_fn log) {
  static const uint64_t sizes[] = {64 * GB, 32 * GB, 16 * GB, 12 * GB, 8 * GB, 6 * GB,
                                   5 * GB, 0x120000000ull, 4 * GB, 3 * GB, 2 * GB, 1 * GB};
  uint64_t largest = 0;
  for (size_t i = 0; i < sizeof sizes / sizeof sizes[0]; i++) {
    void *p = mmap(NULL, (size_t)sizes[i], PROT_NONE, MAP_PRIVATE | MAP_ANON, -1, 0);
    if (p != MAP_FAILED) {
      largest = sizes[i];
      munmap(p, (size_t)sizes[i]);
      break;
    }
  }
  say(log, "Largest single reservation: %.2f GB", (double)largest / GB);

  // Total reservable, in 1 GB chunks, capped at 128 GB.
  enum { kMaxChunks = 128 };
  void *chunks[kMaxChunks];
  int count = 0;
  while (count < kMaxChunks) {
    void *p = mmap(NULL, (size_t)GB, PROT_NONE, MAP_PRIVATE | MAP_ANON, -1, 0);
    if (p == MAP_FAILED) break;
    chunks[count++] = p;
  }
  for (int i = 0; i < count; i++) munmap(chunks[i], (size_t)GB);
  say(log, "Total reservable address space: %s%d GB", count == kMaxChunks ? "at least " : "", count);

  const int ok = largest >= 0x120000000ull;
  say(log, "%s: a 4.5 GB guest reservation %s", ok ? "PASS" : "FAIL", ok ? "fits" : "does not fit");
  return ok;
}

// ---------------------------------------------------------------------------------------------
// 2. Guest memory layout (ReXGlue ebac1cf, src/kernel/memory.cpp)
// ---------------------------------------------------------------------------------------------
#define GUEST_TOTAL 0x120000000ull

static const struct {
  uint64_t start, end, target;
} kViews[] = {
    {0x00000000ull, 0x3FFFFFFFull, 0x000000000ull},  {0x40000000ull, 0x7EFFFFFFull, 0x040000000ull},
    {0x7F000000ull, 0x7FFFFFFFull, 0x100000000ull},  {0x80000000ull, 0x8FFFFFFFull, 0x080000000ull},
    {0x90000000ull, 0x9FFFFFFFull, 0x080000000ull},  {0xA0000000ull, 0xBFFFFFFFull, 0x100000000ull},
    {0xC0000000ull, 0xDFFFFFFFull, 0x100000000ull},  {0xE0000000ull, 0xFFFFFFFFull, 0x100001000ull},
    {0x100000000ull, 0x11FFFFFFFull, 0x100000000ull},
};

int selftest_guest_memory(selftest_log_fn log) {
  const long page = getpagesize();
  const size_t shm_size = (size_t)GUEST_TOTAL + (size_t)page;
  say(log, "Host page size: %ld bytes", page);

  char name[32];
  snprintf(name, sizeof name, "/nfsst_%d", (int)getpid());
  shm_unlink(name);
  int fd = shm_open(name, O_RDWR | O_CREAT | O_EXCL, 0600);
  if (fd < 0) {
    say(log, "FAIL: shm_open: %s (errno %d)", strerror(errno), errno);
    return 0;
  }
  say(log, "shm_open: ok");
  if (ftruncate(fd, (off_t)shm_size) != 0) {
    say(log, "FAIL: ftruncate to %.2f GB: %s (errno %d)", (double)shm_size / GB, strerror(errno), errno);
    close(fd);
    shm_unlink(name);
    return 0;
  }
  say(log, "ftruncate to %.2f GB: ok", (double)shm_size / GB);
  shm_unlink(name);  // the descriptor keeps the object alive

  void *base = mmap(NULL, (size_t)GUEST_TOTAL, PROT_NONE, MAP_PRIVATE | MAP_ANON, -1, 0);
  if (base == MAP_FAILED) {
    say(log, "FAIL: 4.5 GB reservation: %s (errno %d)", strerror(errno), errno);
    close(fd);
    return 0;
  }
  say(log, "4.5 GB reservation at %p: ok", base);

  const uint64_t mask = ~(uint64_t)(page - 1);
  int ok = 1;
  for (size_t n = 0; n < sizeof kViews / sizeof kViews[0]; n++) {
    const size_t size = (size_t)(kViews[n].end - kViews[n].start + 1);
    const off_t offset = (off_t)(kViews[n].target & mask);
    void *target = (uint8_t *)base + kViews[n].start;
    void *r = mmap(target, size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED, fd, offset);
    if (r == MAP_FAILED || r != target) {
      say(log, "FAIL: view %zu (guest 0x%09llx, %.2f GB, offset 0x%llx): %s (errno %d)", n,
          (unsigned long long)kViews[n].start, (double)size / GB, (unsigned long long)offset,
          strerror(errno), errno);
      ok = 0;
      break;
    }
  }
  if (ok) say(log, "Nine MAP_SHARED|MAP_FIXED views: ok");

  if (ok) {
    uint8_t *b = (uint8_t *)base;
    // Physical memory appears at 0xA0000000, 0xC0000000, 0xE0000000 (+0x1000 in code on 16 KB pages)
    // and raw at 0x100000000. Write through one view, read through the others.
    // Guest 0xE0000000 + X is physical X + 0x1000. With 16 KB host pages the view starts at physical 0
    // and code adds 0x1000 to the host address; with 4 KB pages the view itself is offset by 0x1000.
    // Either way, physical P is at host 0xE0000000 + P - (page > 0x1000 ? 0 : 0x1000).
    const size_t e_adjust = page > 0x1000 ? 0 : 0x1000;
    volatile uint32_t *via_a = (volatile uint32_t *)(b + 0xA0000000ull + 0x10000);
    *via_a = 0xC0FFEE01u;
    const uint32_t via_c = *(volatile uint32_t *)(b + 0xC0000000ull + 0x10000);
    const uint32_t via_e = *(volatile uint32_t *)(b + 0xE0000000ull + 0x10000 - e_adjust);
    const uint32_t via_raw = *(volatile uint32_t *)(b + 0x100000000ull + 0x10000);
    // 0x80000000 and 0x90000000 map the same backing (XEX image mirrors).
    *(volatile uint32_t *)(b + 0x80000100ull) = 0x5EEDF00Du;
    const uint32_t via_90 = *(volatile uint32_t *)(b + 0x90000100ull);
    const int aliases = via_c == 0xC0FFEE01u && via_e == 0xC0FFEE01u && via_raw == 0xC0FFEE01u &&
                        via_90 == 0x5EEDF00Du;
    say(log, "Alias check: C=%08x E=%08x raw=%08x 90=%08x -> %s", via_c, via_e, via_raw, via_90,
        aliases ? "ok" : "MISMATCH");
    ok = ok && aliases;
  }

  munmap(base, (size_t)GUEST_TOTAL);
  close(fd);
  say(log, "%s: ReXGlue guest memory layout", ok ? "PASS" : "FAIL");
  return ok;
}

// ---------------------------------------------------------------------------------------------
// 3. Fault handling
// ---------------------------------------------------------------------------------------------
static uint8_t *volatile g_fault_page;
static volatile size_t g_fault_length;
static atomic_int g_fault_count;
static volatile int g_fault_signal;
static struct sigaction g_previous_segv, g_previous_bus;

static void fault_handler(int signal_number, siginfo_t *info, void *context) {
  (void)context;
  uint8_t *address = (uint8_t *)info->si_addr;
  if (g_fault_page && address >= g_fault_page && address < g_fault_page + g_fault_length) {
    atomic_fetch_add(&g_fault_count, 1);
    g_fault_signal = signal_number;
    mprotect(g_fault_page, g_fault_length, PROT_READ | PROT_WRITE);
    return;  // the faulting instruction runs again and now succeeds
  }
  // Not ours: restore the previous handler; returning re-executes the access under it.
  sigaction(signal_number, signal_number == SIGBUS ? &g_previous_bus : &g_previous_segv, NULL);
}

int selftest_fault_handling(selftest_log_fn log) {
  const long page = getpagesize();
  char name[32];
  snprintf(name, sizeof name, "/nfsflt_%d", (int)getpid());
  shm_unlink(name);
  int fd = shm_open(name, O_RDWR | O_CREAT | O_EXCL, 0600);
  if (fd < 0 || ftruncate(fd, 4 * page) != 0) {
    say(log, "FAIL: shared page setup: %s (errno %d)", strerror(errno), errno);
    if (fd >= 0) close(fd);
    shm_unlink(name);
    return 0;
  }
  shm_unlink(name);
  uint8_t *p = mmap(NULL, (size_t)(4 * page), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if (p == MAP_FAILED) {
    say(log, "FAIL: mmap: %s (errno %d)", strerror(errno), errno);
    close(fd);
    return 0;
  }

  struct sigaction action;
  memset(&action, 0, sizeof action);
  action.sa_sigaction = fault_handler;
  action.sa_flags = SA_SIGINFO;
  sigemptyset(&action.sa_mask);
  sigaction(SIGSEGV, &action, &g_previous_segv);
  sigaction(SIGBUS, &action, &g_previous_bus);

  uint8_t *watched = p + page;
  g_fault_length = (size_t)page;
  g_fault_page = watched;
  atomic_store(&g_fault_count, 0);

  // Write to a read-only page (write watch).
  mprotect(watched, (size_t)page, PROT_READ);
  ((volatile uint8_t *)watched)[8] = 0x5A;
  const int write_faults = atomic_load(&g_fault_count);
  const int write_signal = g_fault_signal;
  const int write_ok = write_faults == 1 && watched[8] == 0x5A;
  say(log, "Write to read-only shared page: %d fault(s), %s, value %s", write_faults,
      write_signal == SIGBUS ? "SIGBUS" : write_signal == SIGSEGV ? "SIGSEGV" : "no signal",
      watched[8] == 0x5A ? "kept" : "lost");

  // Read from an inaccessible page (MMIO-style).
  mprotect(watched, (size_t)page, PROT_NONE);
  const uint8_t read_back = ((volatile uint8_t *)watched)[8];
  const int read_faults = atomic_load(&g_fault_count) - write_faults;
  const int read_ok = read_faults == 1 && read_back == 0x5A;
  say(log, "Read from inaccessible shared page: %d fault(s), %s, value %s", read_faults,
      g_fault_signal == SIGBUS ? "SIGBUS" : g_fault_signal == SIGSEGV ? "SIGSEGV" : "no signal",
      read_back == 0x5A ? "correct" : "wrong");

  g_fault_page = NULL;
  sigaction(SIGSEGV, &g_previous_segv, NULL);
  sigaction(SIGBUS, &g_previous_bus, NULL);
  munmap(p, (size_t)(4 * page));
  close(fd);

  const int ok = write_ok && read_ok;
  say(log, "%s: memory-fault handling and resume", ok ? "PASS" : "FAIL");
  return ok;
}
