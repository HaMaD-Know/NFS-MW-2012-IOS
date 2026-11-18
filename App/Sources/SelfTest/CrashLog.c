// Writes a short crash record (signal, fault address, call stack) into shell-log.txt before the
// app ends, so a crash on the iPad can be diagnosed from the log alone. Diagnostics only.
#include "SelfTest.h"

#include <execinfo.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int g_log_fd = -1;
static const int kSignals[] = {SIGSEGV, SIGBUS, SIGILL, SIGABRT, SIGTRAP, SIGFPE};

static const char *signal_name(int s) {
  switch (s) {
    case SIGSEGV: return "SIGSEGV (bad memory access)";
    case SIGBUS: return "SIGBUS (bad memory access)";
    case SIGILL: return "SIGILL (illegal instruction)";
    case SIGABRT: return "SIGABRT (abort)";
    case SIGTRAP: return "SIGTRAP (trap / assertion)";
    case SIGFPE: return "SIGFPE (arithmetic)";
    default: return "unknown";
  }
}

static void crash_handler(int signal_number, siginfo_t *info, void *context) {
  (void)context;
  if (g_log_fd >= 0) {
    char line[256];
    const int n = snprintf(line, sizeof line, "CRASH: signal %d %s at address %p. Call stack:\n",
                           signal_number, signal_name(signal_number), info ? info->si_addr : NULL);
    if (n > 0) write(g_log_fd, line, (size_t)n);
    void *frames[48];
    const int count = backtrace(frames, 48);
    backtrace_symbols_fd(frames, count, g_log_fd);
    fsync(g_log_fd);
  }
  // Let the system finish the crash as usual (and write its own crash report).
  signal(signal_number, SIG_DFL);
  raise(signal_number);
}

void crashlog_install(const char *log_path) {
  if (g_log_fd < 0) g_log_fd = open(log_path, O_WRONLY | O_APPEND | O_CREAT, 0644);
  static char alternate_stack[64 * 1024];
  stack_t ss;
  memset(&ss, 0, sizeof ss);
  ss.ss_sp = alternate_stack;
  ss.ss_size = sizeof alternate_stack;
  sigaltstack(&ss, NULL);
  struct sigaction action;
  memset(&action, 0, sizeof action);
  action.sa_sigaction = crash_handler;
  action.sa_flags = SA_SIGINFO | SA_ONSTACK;
  sigemptyset(&action.sa_mask);
  for (size_t i = 0; i < sizeof kSignals / sizeof kSignals[0]; i++) sigaction(kSignals[i], &action, NULL);
}

void crashlog_redirect_stderr(void) {
  // Libraries such as MoltenVK print their own errors to stderr; keep them in the log file.
  if (g_log_fd >= 0) dup2(g_log_fd, STDERR_FILENO);
}
