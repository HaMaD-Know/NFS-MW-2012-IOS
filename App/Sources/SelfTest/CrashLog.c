// Writes a short crash record (signal, fault address, call stack) into shell-log.txt before the
// app ends, so a crash on the iPad can be diagnosed from the log alone. Diagnostics only.
#include "SelfTest.h"

#include <dirent.h>
#include <execinfo.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static int g_log_fd = -1;
static char g_rexglue_logs[1024];
static const int kSignals[] = {SIGSEGV, SIGBUS, SIGILL, SIGABRT, SIGTRAP, SIGFPE};

void crashlog_set_rexglue_logs(const char *folder) {
  strlcpy(g_rexglue_logs, folder ? folder : "", sizeof g_rexglue_logs);
}

// Copies the end of the newest ReXGlue log (its fatal message and the warnings before it) into
// shell-log.txt, so one file explains a crash. Plain open/read/write only.
static void copy_rexglue_log_tail(void) {
  if (g_log_fd < 0 || !g_rexglue_logs[0]) return;
  DIR *folder = opendir(g_rexglue_logs);
  if (!folder) return;
  char newest[1280] = "";
  struct timespec newest_time = {0, 0};
  char path[1280];
  struct stat info;
  struct dirent *entry;
  while ((entry = readdir(folder)) != NULL) {
    if (entry->d_name[0] == '.' || !strstr(entry->d_name, ".log")) continue;
    snprintf(path, sizeof path, "%s/%s", g_rexglue_logs, entry->d_name);
    if (stat(path, &info) != 0 || !S_ISREG(info.st_mode)) continue;
    const struct timespec t = info.st_mtimespec;
    if (t.tv_sec > newest_time.tv_sec ||
        (t.tv_sec == newest_time.tv_sec && t.tv_nsec >= newest_time.tv_nsec)) {
      newest_time = t;
      strlcpy(newest, path, sizeof newest);
    }
  }
  closedir(folder);
  if (!newest[0]) return;
  const int fd = open(newest, O_RDONLY);
  if (fd < 0) return;
  const off_t size = lseek(fd, 0, SEEK_END);
  const off_t start = size > 12000 ? size - 12000 : 0;
  lseek(fd, start, SEEK_SET);
  char line[1400];
  const int n = snprintf(line, sizeof line, "--- last %lld bytes of the ReXGlue log %s:\n",
                         (long long)(size - start), newest);
  if (n > 0) write(g_log_fd, line, (size_t)n);
  char buffer[4096];
  ssize_t got;
  while ((got = read(fd, buffer, sizeof buffer)) > 0) write(g_log_fd, buffer, (size_t)got);
  close(fd);
  write(g_log_fd, "\n--- end of the ReXGlue log\n", 28);
}

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
    copy_rexglue_log_tail();
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
