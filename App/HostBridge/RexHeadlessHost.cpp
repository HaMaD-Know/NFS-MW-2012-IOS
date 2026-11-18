// N1-b step B7: starts the real ReXGlue app framework on the iPad with no game, then stops it.
//
// The shell's self-test dlopens this library (libnfsmwhost.dylib, built by
// .github/workflows/ipad-runtime-app.yml with the same compiler flags as ReXGlue's own app
// library) and calls nfsmw_rexhost_headless. It runs ReXGlue's normal startup: settings, log file,
// crash reporter, loading and connecting the eight system modules (filesystem, input, audio, UI,
// graphics, XAM, XBDM, kernel) from the app's Frameworks folder and initializing them. It asks for
// no window, no audio, no input and no graphics driver, and loads no game code or data.
#include <rex/app/app.h>
#include <rex/cli.h>
#include <rex/result.h>

#include <algorithm>
#include <atomic>
#include <deque>
#include <exception>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace {

using LogFn = void (*)(const char *line);

LogFn g_log = nullptr;

void Say(std::string_view text) {
  if (g_log) {
    const std::string line(text);
    g_log(line.c_str());
  }
}

const char *PhaseName(rex::app::AppPhase phase) {
  switch (phase) {
    case rex::app::AppPhase::kStart: return "start";
    case rex::app::AppPhase::kProviders: return "modules";
    case rex::app::AppPhase::kRunning: return "running";
    case rex::app::AppPhase::kTitleLaunch: return "title launch";
    case rex::app::AppPhase::kTitleRunning: return "title running";
    case rex::app::AppPhase::kTitleStop: return "title stop";
    case rex::app::AppPhase::kShutdown: return "shutdown";
  }
  return "unknown";
}

// A plain rex::app::App (not a TitleApp): it requires no graphics, audio, input or UI.
class HeadlessHost final : public rex::app::App {
 public:
  HeadlessHost() : App("nfsmw-ipad-host") {}

 protected:
  void OnModules(std::vector<std::string> &modules) override {
    std::string list;
    for (const std::string &name : modules) {
      list += list.empty() ? name : ", " + name;
    }
    Say("  System modules to load: " + list);
  }

  rex::Result<void> OnInit() override {
    Say("  OnInit reached: every module loaded, connected and initialized");
    return rex::Ok();
  }

  void ReportError(std::string_view message) override {
    Say(std::string("  ReXGlue reported: ").append(message));
    App::ReportError(message);
  }
};

// Copies the end of ReXGlue's own log file into the shell log.
void ShowRexLog(const std::filesystem::path &folder) {
  std::error_code error;
  std::filesystem::path newest;
  std::filesystem::file_time_type newest_time{};
  for (const auto &entry : std::filesystem::directory_iterator(folder, error)) {
    if (!entry.is_regular_file(error)) continue;
    const auto time = entry.last_write_time(error);
    if (newest.empty() || time > newest_time) {
      newest = entry.path();
      newest_time = time;
    }
  }
  if (newest.empty()) {
    Say("  No ReXGlue log file in " + folder.string());
    return;
  }
  std::ifstream file(newest);
  std::deque<std::string> tail;
  size_t total = 0;
  for (std::string line; std::getline(file, line);) {
    total++;
    tail.push_back(std::move(line));
    if (tail.size() > 150) tail.pop_front();
  }
  Say("  ReXGlue log " + newest.filename().string() + ": " + std::to_string(total) +
      " lines, last " + std::to_string(tail.size()) + " follow");
  for (const std::string &line : tail) {
    Say("  log| " + line.substr(0, 400));
  }
}

int Run(const std::filesystem::path &root) {
  std::error_code error;
  std::filesystem::create_directories(root, error);
  Say("  App folder: " + root.string() + (error ? " (could not create: " + error.message() + ")" : ""));

  const std::vector<std::string> args = {
      "--app.root=" + root.string(),
      "--ui.driver=none",    // headless: no window yet
      "--apu.driver=none",   // no audio yet
      "--hid.drivers=none",  // no controller yet
  };
  std::string shown;
  for (const std::string &arg : args) shown += " " + arg;
  Say("  Settings:" + shown);

  bool init_ok = false;
  int exit_code = -1;
  {
    HeadlessHost app;
    Say("  Init: starting");
    const rex::Result<void> init = app.Init(rex::CLIArgList::Parse(args));
    if (!init) {
      Say("  Init FAILED: " + init.error().GetDescription());
    } else if (rex::app::IsShutdownRequested()) {
      Say("  Init ended early because a shutdown was requested");
    } else {
      init_ok = true;
      Say(std::string("  Init OK, phase: ") + PhaseName(app.GetPhase()) +
          (app.FindUI() ? "" : " (no UI system)"));
    }
    Say("  Shutdown: starting");
    exit_code = app.Shutdown();
    Say("  Shutdown returned " + std::to_string(exit_code) + ", phase: " + PhaseName(app.GetPhase()));
  }
  Say("  App object destroyed (modules unloaded)");
  ShowRexLog(root / "logs");
  return init_ok && exit_code == 0;
}

}  // namespace

extern "C" __attribute__((visibility("default"))) int nfsmw_rexhost_headless(const char *root,
                                                                              LogFn log) {
  g_log = log;
  static std::atomic<bool> ran{false};
  if (ran.exchange(true)) {
    Say("  Already ran once in this launch; close and reopen the app to run it again");
    return 0;
  }
  try {
    return Run(root ? std::filesystem::path(root) : std::filesystem::path("."));
  } catch (const std::exception &failure) {
    Say(std::string("  FAIL: C++ exception: ") + failure.what());
  } catch (...) {
    Say("  FAIL: unknown C++ exception");
  }
  return 0;
}
