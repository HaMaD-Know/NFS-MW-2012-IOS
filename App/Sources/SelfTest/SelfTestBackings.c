// Replacement guest-memory backings for iOS, where shm_open is not permitted
// (device result 2026-11-17: shm_open -> EPERM). See SelfTest.h.
//
// Each candidate builds ReXGlue's exact guest layout (ebac1cf, src/kernel/memory.cpp
// kViewRanges): one 4.5 GB address reservation holding nine views, several of which mirror the
// same memory. Each is then checked for mirror correctness, fault handling and write speed.
//   A. file: a file in the app's tmp folder, deleted right after opening, mapped MAP_SHARED
//   B. mach_entry: a Mach named memory entry (no file), views mapped with vm_map
//   C. vm_remap: plain memory for the primary ranges, mirrors created with vm_remap
#define _DARWIN_C_SOURCE 1

#include "SelfTest.h"

#include <TargetConditionals.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <mach/mach.h>
#include <mach/mach_error.h>
#include <mach/vm_statistics.h>
#include <signal.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
#if TARGET_OS_IPHONE
#include <os/proc.h>
#endif

#ifndef MAP_MEM_NAMED_CREATE
#define MAP_MEM_NAMED_CREATE 0x020000
#endif

#define GB (1ull << 30)
#define MB (1ull << 20)
#define GUEST_TOTAL 0x120000000ull

static void say(selftest_log_fn log, const char *fmt, ...) {
  char buffer[512];
  va_list args;
  va_start(args, fmt);
  vsnprintf(buffer, sizeof buffer, fmt, args);
  va_end(args);
  if (log) log(buffer);
}

static const struct {
  uint64_t start, end, target;
} kViews[] = {
    {0x00000000ull, 0x3FFFFFFFull, 0x000000000ull},  {0x40000000ull, 0x7EFFFFFFull, 0x040000000ull},
    {0x7F000000ull, 0x7FFFFFFFull, 0x100000000ull},  {0x80000000ull, 0x8FFFFFFFull, 0x080000000ull},
    {0x90000000ull, 0x9FFFFFFFull, 0x080000000ull},  {0xA0000000ull, 0xBFFFFFFFull, 0x100000000ull},
    {0xC0000000ull, 0xDFFFFFFFull, 0x100000000ull},  {0xE0000000ull, 0xFFFFFFFFull, 0x100001000ull},
    {0x100000000ull, 0x11FFFFFFFull, 0x100000000ull},
};
#define VIEW_COUNT (sizeof kViews / sizeof kViews[0])

static double available_gb(void) {
#if TARGET_OS_IPHONE
  return (double)os_proc_available_memory() / GB;
#else
  return -1.0;  // not available on macOS (host logic check)
#endif
}

static double now_ms(void) {
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return (double)t.tv_sec * 1000.0 + (double)t.tv_nsec / 1e6;
}

// ---------------------------------------------------------------------------------------------
// Common checks
// ---------------------------------------------------------------------------------------------
static int alias_check(uint8_t *b, long page, selftest_log_fn log) {
  const size_t e_adjust = page > 0x1000 ? 0x1000 : 0;
  *(volatile uint32_t *)(b + 0xA0000000ull + 0x10000) = 0xC0FFEE01u;
  const uint32_t via_c = *(volatile uint32_t *)(b + 0xC0000000ull + 0x10000);
  const uint32_t via_e = *(volatile uint32_t *)(b + 0xE0000000ull + 0x10000 + e_adjust);
  const uint32_t via_raw = *(volatile uint32_t *)(b + 0x100000000ull + 0x10000);
  const uint32_t via_7f = *(volatile uint32_t *)(b + 0x7F000000ull + 0x10000);
  *(volatile uint32_t *)(b + 0x80000100ull) = 0x5EEDF00Du;
  const uint32_t via_90 = *(volatile uint32_t *)(b + 0x90000100ull);
  *(volatile uint32_t *)(b + 0x00001000ull) = 0x11112222u;  // plain view, no alias
  const uint32_t plain = *(volatile uint32_t *)(b + 0x00001000ull);
  const int ok = via_c == 0xC0FFEE01u && via_e == 0xC0FFEE01u && via_raw == 0xC0FFEE01u &&
                 via_7f == 0xC0FFEE01u && via_90 == 0x5EEDF00Du && plain == 0x11112222u;
  say(log, "  Mirrors: C=%08x E=%08x raw=%08x 7F=%08x 90=%08x -> %s", via_c, via_e, via_raw, via_7f,
      via_90, ok ? "ok" : "MISMATCH");
  return ok;
}

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
    return;
  }
  sigaction(signal_number, signal_number == SIGBUS ? &g_previous_bus : &g_previous_segv, NULL);
}

// Protects one page of a mirrored view, faults on it, resumes, and checks the other mirror.
static int fault_check(uint8_t *page, const uint8_t *mirror, long page_size, selftest_log_fn log) {
  struct sigaction action;
  memset(&action, 0, sizeof action);
  action.sa_sigaction = fault_handler;
  action.sa_flags = SA_SIGINFO;
  sigemptyset(&action.sa_mask);
  sigaction(SIGSEGV, &action, &g_previous_segv);
  sigaction(SIGBUS, &action, &g_previous_bus);

  g_fault_length = (size_t)page_size;
  g_fault_page = page;
  atomic_store(&g_fault_count, 0);
  g_fault_signal = 0;

  if (mprotect(page, (size_t)page_size, PROT_READ) != 0) {
    say(log, "  Fault check: mprotect failed: %s (errno %d)", strerror(errno), errno);
    g_fault_page = NULL;
    sigaction(SIGSEGV, &g_previous_segv, NULL);
    sigaction(SIGBUS, &g_previous_bus, NULL);
    return 0;
  }
  ((volatile uint8_t *)page)[8] = 0x5A;  // write to read-only: fault, unprotect, resume
  const int write_faults = atomic_load(&g_fault_count);
  const int write_signal = g_fault_signal;

  mprotect(page, (size_t)page_size, PROT_NONE);
  const uint8_t read_back = ((volatile uint8_t *)page)[8];  // read from inaccessible
  const int read_faults = atomic_load(&g_fault_count) - write_faults;
  const uint8_t via_mirror = mirror[8];

  g_fault_page = NULL;
  sigaction(SIGSEGV, &g_previous_segv, NULL);
  sigaction(SIGBUS, &g_previous_bus, NULL);

  const int ok = write_faults == 1 && read_faults == 1 && read_back == 0x5A && via_mirror == 0x5A;
  say(log, "  Fault handling: write %d fault (%s), read %d fault, value %s, mirror %s -> %s",
      write_faults, write_signal == SIGBUS ? "SIGBUS" : write_signal == SIGSEGV ? "SIGSEGV" : "none",
      read_faults, read_back == 0x5A ? "kept" : "lost", via_mirror == 0x5A ? "sees it" : "does NOT",
      ok ? "ok" : "FAIL");
  return ok;
}

// Writes 256 MB through a mirrored view and reports speed and the change in available memory.
static void write_measure(uint8_t *b, selftest_log_fn log) {
  const size_t length = 256 * MB;
  uint8_t *p = b + 0xA0000000ull + 16 * MB;
  const double before = available_gb();
  const double t0 = now_ms();
  memset(p, 0xAB, length);
  const double elapsed = now_ms() - t0;
  const double after = available_gb();
  const uint32_t probe = *(volatile uint32_t *)(b + 0x100000000ull + 16 * MB + 100 * MB);
  say(log, "  Wrote 256 MB: %.0f ms (%.0f MB/s); app memory available %.2f -> %.2f GB; mirror %s",
      elapsed, 256.0 / (elapsed / 1000.0), before, after, probe == 0xABABABABu ? "ok" : "WRONG");
  // Release the pages again so the next candidate starts from the same state.
  madvise(p, length, MADV_FREE);
}

static int finish(uint8_t *base, long page, selftest_log_fn log) {
  int ok = alias_check(base, page, log);
  ok = fault_check(base + 0xA0000000ull + 0x40000, base + 0xC0000000ull + 0x40000, page, log) && ok;
  write_measure(base, log);
  return ok;
}

static void *reserve_guest(selftest_log_fn log) {
  void *base = mmap(NULL, (size_t)GUEST_TOTAL, PROT_NONE, MAP_PRIVATE | MAP_ANON, -1, 0);
  if (base == MAP_FAILED) {
    say(log, "  FAIL: 4.5 GB reservation: %s (errno %d)", strerror(errno), errno);
    return NULL;
  }
  return base;
}

// ---------------------------------------------------------------------------------------------
// A. Temporary file
// ---------------------------------------------------------------------------------------------
int selftest_backing_file(selftest_log_fn log) {
  const long page = getpagesize();
  const char *tmp = getenv("TMPDIR");
  if (!tmp || !*tmp) tmp = "/tmp";
  char path[1024];
  snprintf(path, sizeof path, "%s/nfsst_%d.mem", tmp, (int)getpid());
  unlink(path);
  int fd = open(path, O_RDWR | O_CREAT | O_EXCL, 0600);
  if (fd < 0) {
    say(log, "  FAIL: open in TMPDIR: %s (errno %d)", strerror(errno), errno);
    say(log, "FAIL: A. temporary-file backing");
    return 0;
  }
  unlink(path);  // nothing visible in the file system; space returns when the app closes it
  if (ftruncate(fd, (off_t)(GUEST_TOTAL + (uint64_t)page)) != 0) {
    say(log, "  FAIL: ftruncate to 4.5 GB: %s (errno %d)", strerror(errno), errno);
    close(fd);
    say(log, "FAIL: A. temporary-file backing");
    return 0;
  }
  say(log, "  Temporary file created, deleted from the folder, sized 4.5 GB: ok");

  uint8_t *base = reserve_guest(log);
  int ok = base != NULL;
  const uint64_t mask = ~(uint64_t)(page - 1);
  for (size_t n = 0; ok && n < VIEW_COUNT; n++) {
    const size_t size = (size_t)(kViews[n].end - kViews[n].start + 1);
    void *target = base + kViews[n].start;
    void *r = mmap(target, size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED, fd,
                   (off_t)(kViews[n].target & mask));
    if (r == MAP_FAILED || r != target) {
      say(log, "  FAIL: view %zu: %s (errno %d)", n, strerror(errno), errno);
      ok = 0;
    }
  }
  if (ok) {
    say(log, "  Nine views mapped: ok");
    ok = finish(base, page, log);
  }
  if (base) munmap(base, (size_t)GUEST_TOTAL);
  close(fd);
  say(log, "%s: A. temporary-file backing", ok ? "PASS" : "FAIL");
  return ok;
}

// ---------------------------------------------------------------------------------------------
// B. Mach named memory entry
// ---------------------------------------------------------------------------------------------
typedef kern_return_t (*make_entry_fn)(vm_map_t, memory_object_size_t *, memory_object_offset_t,
                                       vm_prot_t, mach_port_t *, mach_port_t);
typedef kern_return_t (*vm_map_fn)(vm_map_t, vm_address_t *, vm_size_t, vm_address_t, int,
                                   mach_port_t, vm_offset_t, boolean_t, vm_prot_t, vm_prot_t,
                                   vm_inherit_t);

int selftest_backing_mach_entry(selftest_log_fn log) {
  const long page = getpagesize();
  make_entry_fn make_entry = (make_entry_fn)dlsym(RTLD_DEFAULT, "mach_make_memory_entry_64");
  vm_map_fn map = (vm_map_fn)dlsym(RTLD_DEFAULT, "vm_map");
  if (!make_entry || !map) {
    say(log, "  FAIL: mach_make_memory_entry_64 %s, vm_map %s", make_entry ? "found" : "missing",
        map ? "found" : "missing");
    say(log, "FAIL: B. Mach memory-entry backing");
    return 0;
  }
  memory_object_size_t size = GUEST_TOTAL + (uint64_t)page;
  mach_port_t entry = MACH_PORT_NULL;
  kern_return_t kr = make_entry(mach_task_self(), &size, 0,
                                MAP_MEM_NAMED_CREATE | VM_PROT_READ | VM_PROT_WRITE, &entry,
                                MACH_PORT_NULL);
  if (kr != KERN_SUCCESS) {
    say(log, "  FAIL: mach_make_memory_entry_64: %s (%d)", mach_error_string(kr), kr);
    say(log, "FAIL: B. Mach memory-entry backing");
    return 0;
  }
  say(log, "  Memory entry of %.2f GB created: ok", (double)size / GB);

  uint8_t *base = reserve_guest(log);
  int ok = base != NULL;
  const uint64_t mask = ~(uint64_t)(page - 1);
  for (size_t n = 0; ok && n < VIEW_COUNT; n++) {
    const vm_size_t view_size = (vm_size_t)(kViews[n].end - kViews[n].start + 1);
    vm_address_t address = (vm_address_t)(base + kViews[n].start);
    kr = map(mach_task_self(), &address, view_size, 0, VM_FLAGS_FIXED | VM_FLAGS_OVERWRITE, entry,
             (vm_offset_t)(kViews[n].target & mask), FALSE, VM_PROT_READ | VM_PROT_WRITE,
             VM_PROT_READ | VM_PROT_WRITE, VM_INHERIT_NONE);
    if (kr != KERN_SUCCESS || address != (vm_address_t)(base + kViews[n].start)) {
      say(log, "  FAIL: vm_map view %zu: %s (%d)", n, mach_error_string(kr), kr);
      ok = 0;
    }
  }
  if (ok) {
    say(log, "  Nine views mapped: ok");
    ok = finish(base, page, log);
  }
  if (base) munmap(base, (size_t)GUEST_TOTAL);
  mach_port_deallocate(mach_task_self(), entry);
  say(log, "%s: B. Mach memory-entry backing", ok ? "PASS" : "FAIL");
  return ok;
}

// ---------------------------------------------------------------------------------------------
// C. Plain memory + vm_remap mirrors
// ---------------------------------------------------------------------------------------------
typedef kern_return_t (*vm_allocate_fn)(vm_map_t, vm_address_t *, vm_size_t, int);
typedef kern_return_t (*vm_remap_fn)(vm_map_t, vm_address_t *, vm_size_t, vm_address_t, int,
                                     vm_map_t, vm_address_t, boolean_t, vm_prot_t *, vm_prot_t *,
                                     vm_inherit_t);

int selftest_backing_vm_remap(selftest_log_fn log) {
  const long page = getpagesize();
  vm_allocate_fn allocate = (vm_allocate_fn)dlsym(RTLD_DEFAULT, "vm_allocate");
  vm_remap_fn remap = (vm_remap_fn)dlsym(RTLD_DEFAULT, "vm_remap");
  if (!allocate || !remap) {
    say(log, "  FAIL: vm_allocate %s, vm_remap %s", allocate ? "found" : "missing",
        remap ? "found" : "missing");
    say(log, "FAIL: C. vm_remap backing");
    return 0;
  }
  uint8_t *base = reserve_guest(log);
  if (!base) {
    say(log, "FAIL: C. vm_remap backing");
    return 0;
  }
  // The layout's backing offsets equal guest addresses for the three ranges that own memory,
  // so the primaries live at base + offset and every other view mirrors one of them.
  static const struct {
    uint64_t start, length;
  } primaries[] = {{0x000000000ull, 0x7F000000ull},
                   {0x080000000ull, 0x10000000ull},
                   {0x100000000ull, 0x20000000ull}};
  int ok = 1;
  for (size_t i = 0; ok && i < 3; i++) {
    vm_address_t address = (vm_address_t)(base + primaries[i].start);
    kern_return_t kr = allocate(mach_task_self(), &address, (vm_size_t)primaries[i].length,
                                VM_FLAGS_FIXED | VM_FLAGS_OVERWRITE);
    if (kr != KERN_SUCCESS) {
      say(log, "  FAIL: vm_allocate primary %zu: %s (%d)", i, mach_error_string(kr), kr);
      ok = 0;
    }
  }
  const uint64_t mask = ~(uint64_t)(page - 1);
  static const size_t mirror_views[] = {2, 4, 5, 6, 7};
  for (size_t i = 0; ok && i < 5; i++) {
    const size_t n = mirror_views[i];
    const vm_size_t view_size = (vm_size_t)(kViews[n].end - kViews[n].start + 1);
    vm_address_t target = (vm_address_t)(base + kViews[n].start);
    const vm_address_t source = (vm_address_t)(base + (kViews[n].target & mask));
    vm_prot_t current = 0, maximum = 0;
    kern_return_t kr = remap(mach_task_self(), &target, view_size, 0,
                             VM_FLAGS_FIXED | VM_FLAGS_OVERWRITE, mach_task_self(), source,
                             FALSE, &current, &maximum, VM_INHERIT_NONE);
    if (kr != KERN_SUCCESS || target != (vm_address_t)(base + kViews[n].start)) {
      say(log, "  FAIL: vm_remap view %zu: %s (%d)", n, mach_error_string(kr), kr);
      ok = 0;
    } else if ((current & (VM_PROT_READ | VM_PROT_WRITE)) != (VM_PROT_READ | VM_PROT_WRITE)) {
      say(log, "  FAIL: vm_remap view %zu is not read-write (protection %d)", n, current);
      ok = 0;
    }
  }
  if (ok) {
    say(log, "  Three primaries and five mirrors mapped: ok");
    ok = finish(base, page, log);
  }
  munmap(base, (size_t)GUEST_TOTAL);
  say(log, "%s: C. vm_remap backing", ok ? "PASS" : "FAIL");
  return ok;
}
