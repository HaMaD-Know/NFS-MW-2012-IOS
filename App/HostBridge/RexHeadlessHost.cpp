// N1-b step B7: starts the real ReXGlue app framework on the iPad with no game, then stops it.
//
// The shell dlopens this library (libnfsmwhost.dylib, built by
// .github/workflows/ipad-runtime-app.yml with the same compiler flags as ReXGlue's own app
// library) and calls one of two entry points. Neither loads game code or data.
//
//  nfsmw_rexhost_headless  Part 1. ReXGlue's normal startup with no window, audio, input or
//                          graphics driver: settings, log file, crash reporter, and the eight
//                          system modules loaded, connected and initialized. Any thread.
//  nfsmw_rexhost_window    Part 2. The same, plus the Vulkan graphics driver (MoltenVK on
//                          Metal), an SDL3 window and SDL3 controller input, run for a few
//                          seconds. Must run on the main thread, which UIKit requires for windows.
//
// One ReXGlue start per app launch: a second start in the same process is refused.
#include <rex/app/app.h>
#include <rex/app_info.h>
#include <rex/cli.h>
#include <rex/gpu/graphics_system.h>
#include <rex/hid/input_system.h>
#include <rex/kernel/guest_memory.h>
#include <rex/kernel/title_paths.h>
#include <rex/kernel/title_runtime.h>
#include <rex/ppc/image.h>
#include <rex/setting.h>
#include <rex/result.h>
#include <rex/ui/ui_system.h>

#include <dlfcn.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
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
void *g_sdl = nullptr;  // libSDL3, the same copy ReXGlue's SDL3 drivers use

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

// A plain rex::app::App (not a TitleApp). Headless it requires nothing; with a window it
// requires a Xenos graphics driver and a UI window, as a game would, but not audio or input.
class Host final : public rex::app::App {
 public:
  explicit Host(bool window) : App("nfsmw-ipad-host"), window_(window) {}

  bool HasUISystem() { return FindUI() != nullptr; }

 protected:
  rex::AppSysRequirements GetRequirements() const override {
    rex::AppSysRequirements needs;
    if (window_) {
      needs.graphics_family = rex::GraphicsFamily::kXenos;
      needs.ui = true;
    }
    return needs;
  }

  void OnModules(std::vector<std::string> &modules) override {
    std::string list;
    for (const std::string &module : modules) {
      list += list.empty() ? module : ", " + module;
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

 private:
  bool window_;
};

// Copies the end of ReXGlue's own log file into the shell log.
void ShowRexLog(const std::filesystem::path &folder, size_t keep) {
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
    if (tail.size() > keep) tail.pop_front();
  }
  Say("  ReXGlue log " + newest.filename().string() + ": " + std::to_string(total) +
      " lines, last " + std::to_string(tail.size()) + " follow");
  for (const std::string &line : tail) {
    Say("  log| " + line.substr(0, 400));
  }
}

// Folder this library was loaded from: the app's Frameworks folder.
std::filesystem::path OwnFolder() {
  Dl_info info{};
  if (dladdr(reinterpret_cast<const void *>(&OwnFolder), &info) && info.dli_fname) {
    return std::filesystem::path(info.dli_fname).parent_path();
  }
  return {};
}

// SDL on iOS refuses to start unless the program says its main function is ready (normally
// SDL's own main does this). The shell has its own UIKit main, so tell SDL directly.
void TellSDLMainIsReady() {
  const std::filesystem::path sdl = OwnFolder() / "libSDL3d.0.dylib";
  void *handle = dlopen(sdl.c_str(), RTLD_NOW | RTLD_GLOBAL);
  g_sdl = handle;
  if (!handle) {
    const char *error = dlerror();
    Say(std::string("  Could not load SDL: ") + (error ? error : "unknown error"));
    return;
  }
  using ReadyFn = void (*)();
  if (auto ready = reinterpret_cast<ReadyFn>(dlsym(handle, "SDL_SetMainReady"))) {
    ready();
    Say("  Told SDL that main is ready");
  } else {
    Say("  SDL_SetMainReady not found");
  }
}

std::string PadText(const rex::hid::PadState &pad) {
  char text[160];
  std::snprintf(text, sizeof text, "buttons %04x, triggers %u/%u, left stick %d,%d, right stick %d,%d",
                static_cast<unsigned>(pad.buttons), static_cast<unsigned>(pad.left_trigger),
                static_cast<unsigned>(pad.right_trigger), static_cast<int>(pad.thumb_lx),
                static_cast<int>(pad.thumb_ly), static_cast<int>(pad.thumb_rx),
                static_cast<int>(pad.thumb_ry));
  return text;
}

// What SDL itself sees, to tell "no controller found" from "ReXGlue hides its input".
std::string SDLGamepads() {
  if (!g_sdl) return "SDL not loaded";
  using GetGamepadsFn = std::uint32_t *(*)(int *count);
  using NameFn = const char *(*)(std::uint32_t id);
  using FreeFn = void (*)(void *memory);
  auto get = reinterpret_cast<GetGamepadsFn>(dlsym(g_sdl, "SDL_GetGamepads"));
  auto name = reinterpret_cast<NameFn>(dlsym(g_sdl, "SDL_GetGamepadNameForID"));
  auto release = reinterpret_cast<FreeFn>(dlsym(g_sdl, "SDL_free"));
  if (!get || !name || !release) return "SDL gamepad functions not found";
  int count = 0;
  std::uint32_t *ids = get(&count);
  std::string text = std::to_string(count) + " gamepad" + (count == 1 ? "" : "s");
  for (int i = 0; ids && i < count; i++) {
    const char *pad_name = name(ids[i]);
    text += std::string(i ? ", " : ": ") + (pad_name ? pad_name : "unnamed");
  }
  if (ids) release(ids);
  return text;
}

const char *PadTypeName(rex::hid::PadType type) {
  switch (type) {
    case rex::hid::PadType::kUnknown: return "unknown";
    case rex::hid::PadType::kStandard: return "standard";
    case rex::hid::PadType::kX360: return "Xbox 360";
    case rex::hid::PadType::kXOne: return "Xbox One/Series";
    case rex::hid::PadType::kPS3: return "PS3";
    case rex::hid::PadType::kPS4: return "PS4";
    case rex::hid::PadType::kPS5: return "PS5";
    case rex::hid::PadType::kSwitchPro: return "Switch Pro";
    case rex::hid::PadType::kSwitchJoyCon: return "Joy-Con";
  }
  return "other";
}

// Runs the window for `seconds`, reporting frames and the first controller once a second.
void RunWindow(Host &app, int seconds) {
  rex::gpu::IGraphicsSystem &graphics = app.GetGraphics();
  rex::ui::IUISystem &ui = app.GetUI();
  rex::hid::IInputSystem &input = app.GetInput();
  Say(std::string("  Graphics driver: ") + (graphics.HasDriver() ? graphics.GetDriverName() : "none") +
      "; window: " + (ui.HasWindow() ? "open" : "none") + (ui.IsHeadless() ? " (headless)" : ""));
  Say("  Running the window for " + std::to_string(seconds) +
      " seconds. Press buttons on the controller now.");

  using Clock = std::chrono::steady_clock;
  const auto start = Clock::now();
  auto next_report = start + std::chrono::seconds(1);
  std::uint64_t loops = 0;
  std::uint32_t last_packet = 0;
  std::string last_pad_error;
  std::string last_devices;
  bool ended = false;
  while (Clock::now() - start < std::chrono::seconds(seconds)) {
    if (!app.RunFrame()) {
      ended = true;
      break;
    }
    loops++;
    if (Clock::now() >= next_report) {
      next_report += std::chrono::seconds(1);
      const auto stats = graphics.GetFrameStats();
      std::string line = "  t=" +
                         std::to_string(std::chrono::duration_cast<std::chrono::seconds>(
                                            Clock::now() - start)
                                            .count()) +
                         "s UI loops " + std::to_string(loops) + ", frames " +
                         std::to_string(stats.frame_count);
      std::string devices = "SDL sees " + SDLGamepads();
      if (const auto info = input.GetPadInfo(0)) {
        char ids[48];
        std::snprintf(ids, sizeof ids, " %04x:%04x", static_cast<unsigned>(info->vendor),
                      static_cast<unsigned>(info->product));
        devices += std::string("; ReXGlue pad 1: ") + PadTypeName(info->type) + ids;
      } else {
        devices += "; ReXGlue pad 1: " + info.error().GetDescription();
      }
      devices += ui.IsFocused() ? "; window focused" : "; window NOT focused";
      if (devices != last_devices) {
        Say("  " + devices);
        last_devices = devices;
      }
      const auto pad = input.GetState(0);
      if (pad) {
        line += "; controller 1: " + PadText(pad->state);
        if (pad->packet != last_packet) line += " (changed)";
        last_packet = pad->packet;
      } else if (pad.error().GetDescription() != last_pad_error) {
        last_pad_error = pad.error().GetDescription();
        line += "; controller 1: " + last_pad_error;
      }
      Say(line);
    }
  }
  Say(ended ? "  The UI loop ended early (window closed or shutdown requested)"
            : "  Window time is up");
}

// Step after B7: ReXGlue's own guest memory on the iPad. TitleRuntime::Load with an image that
// has no recompiled code ("tool mode") and an empty title.entry maps the full guest address space
// (patch 0004's Mach memory entry and its nine views) and creates the kernel, loading no
// executable. The test then allocates guest memory, writes and reads it, checks that physical
// memory is visible through its mirror views, and unloads.
bool RunGuestMemory() {
  static const ::PPCFuncMapping kNoFunctions[] = {{0, nullptr}};
  rex::ppc::PPCImage image{};
  image.func_mappings = kNoFunctions;
  rex::kernel::TitleRuntime title(image);
  if (auto set = title.GetTitlePaths().entry.SetText(rex::SettingLayer::kTitle, ""); !set) {
    Say("  Could not clear title.entry: " + set.error().GetDescription());
    return false;
  }
  Say("  TitleRuntime::Load (tool mode, no executable): starting");
  if (auto loaded = title.Load(); !loaded) {
    Say("  Load FAILED: " + loaded.error().GetDescription());
    return false;
  }
  Say("  Load OK: guest memory mapped, kernel services created");
  rex::kernel::GuestMemory &memory = title.GetGuestMemory();
  std::uint8_t *base = title.GetMembase().GetBase();
  bool ok = base != nullptr;
  char text[200];
  std::snprintf(text, sizeof text, "  Guest memory base %p", static_cast<void *>(base));
  Say(text);

  // Title (virtual) memory: allocate 1 MiB, store and load through ReXGlue, then read the same
  // bytes directly through the host mapping.
  constexpr std::uint32_t kPattern = 0xC0FFEE01u;
  auto virt = memory.Allocate(1u << 20, 4096, rex::kernel::GuestProtect::kReadWrite);
  if (!virt) {
    Say("  Title memory allocate FAILED: " + virt.error().GetDescription());
    ok = false;
  } else {
    const bool stored = memory.Store<std::uint32_t>(*virt + 0x100, kPattern).has_value();
    const auto loaded = memory.Load<std::uint32_t>(*virt + 0x100);
    std::uint32_t raw = 0;
    if (base) std::memcpy(&raw, base + *virt + 0x100, sizeof raw);
    const bool host_sees = raw == kPattern;  // Store writes host byte order
    const bool good = stored && loaded && *loaded == kPattern && host_sees;
    std::snprintf(text, sizeof text,
                  "  Title memory: 1 MiB at guest 0x%08X, store/load %08X, host mapping %s -> %s",
                  *virt, loaded ? *loaded : 0u, host_sees ? "sees it" : "EMPTY",
                  good ? "ok" : "FAIL");
    Say(text);
    ok = ok && good;
    (void)memory.Free(*virt);
  }

  // Physical memory: allocate 64 KiB, write through the 0xA0000000 view, read through the
  // 0xC0000000 view, which maps the same physical pages.
  auto phys = memory.Allocate(64u << 10, 4096, rex::kernel::GuestProtect::kReadWrite,
                              rex::kernel::GuestHeap::kPhysical);
  if (!phys) {
    Say("  Physical memory allocate FAILED: " + phys.error().GetDescription());
    ok = false;
  } else {
    const std::uint32_t address = *phys + 0x200;
    const bool stored = memory.Store<std::uint32_t>(address, 0x5EEDF00Du).has_value();
    // GuestMemory::Store copies the value in host byte order, so compare the raw bytes of the
    // 0xA0000000 view with those of the 0xC0000000 view (shell 0.1.1 wrongly assumed big-endian).
    std::uint32_t mirror_value = 0;
    bool mirror_ok = false;
    if (base && address >= 0xA0000000u && address < 0xC0000000u) {
      const std::uint8_t *primary = base + address;
      const std::uint8_t *mirror = base + (static_cast<std::uint64_t>(address) - 0xA0000000u + 0xC0000000u);
      std::memcpy(&mirror_value, mirror, sizeof mirror_value);
      mirror_ok = std::memcmp(primary, mirror, 4) == 0 && mirror_value == 0x5EEDF00Du;
    }
    std::snprintf(text, sizeof text,
                  "  Physical memory: 64 KiB at guest 0x%08X, mirror 0xC0000000 view reads %08X -> %s",
                  *phys, mirror_value, stored && mirror_ok ? "ok" : "FAIL");
    Say(text);
    ok = ok && stored && mirror_ok;
    (void)memory.Free(*phys);
  }

  Say("  TitleRuntime::Unload: starting");
  title.Unload();
  Say("  Unload done");
  return ok;
}

int Run(const std::filesystem::path &root, bool window, int seconds, bool guest_memory) {
  std::error_code error;
  std::filesystem::create_directories(root, error);
  Say("  App folder: " + root.string() + (error ? " (could not create: " + error.message() + ")" : ""));

  std::vector<std::string> args = {"--app.root=" + root.string(), "--apu.driver=none"};
  if (window) {
    args.push_back("--gpu.driver=vulkan");  // MoltenVK on Metal
    TellSDLMainIsReady();
  } else {
    args.push_back("--ui.driver=none");    // headless: no window
    args.push_back("--hid.drivers=none");  // no controller
  }
  std::string shown;
  for (const std::string &arg : args) shown += " " + arg;
  Say("  Settings:" + shown);

  bool init_ok = false;
  int exit_code = -1;
  {
    Host app(window);
    Say("  Init: starting");
    const rex::Result<void> init = app.Init(rex::CLIArgList::Parse(args));
    if (!init) {
      Say("  Init FAILED: " + init.error().GetDescription());
    } else if (rex::app::IsShutdownRequested()) {
      Say("  Init ended early because a shutdown was requested");
    } else {
      init_ok = true;
      Say(std::string("  Init OK, phase: ") + PhaseName(app.GetPhase()) +
          (app.HasUISystem() ? "" : " (no UI system)"));
      if (window) RunWindow(app, seconds);
      if (guest_memory) {
        const bool memory_ok = RunGuestMemory();
        Say(std::string("  Guest memory test: ") + (memory_ok ? "ok" : "FAILED"));
        init_ok = init_ok && memory_ok;
      }
    }
    Say("  Shutdown: starting");
    exit_code = app.Shutdown();
    Say("  Shutdown returned " + std::to_string(exit_code) + ", phase: " + PhaseName(app.GetPhase()));
  }
  Say("  App object destroyed (modules unloaded)");
  ShowRexLog(root / "logs", window ? 200 : 150);
  return init_ok && exit_code == 0;
}

int Guarded(const char *root, LogFn log, bool window, int seconds, bool guest_memory = false) {
  g_log = log;
  static std::atomic<bool> ran{false};
  if (ran.exchange(true)) {
    Say("  ReXGlue already ran once in this launch; close and reopen the app to run it again");
    return 0;
  }
  try {
    return Run(root ? std::filesystem::path(root) : std::filesystem::path("."), window, seconds,
               guest_memory);
  } catch (const std::exception &failure) {
    Say(std::string("  FAIL: C++ exception: ") + failure.what());
  } catch (...) {
    Say("  FAIL: unknown C++ exception");
  }
  return 0;
}

}  // namespace

extern "C" __attribute__((visibility("default"))) int nfsmw_rexhost_headless(const char *root,
                                                                              LogFn log) {
  return Guarded(root, log, false, 0);
}

extern "C" __attribute__((visibility("default"))) int nfsmw_rexhost_window(const char *root,
                                                                            LogFn log,
                                                                            int seconds) {
  return Guarded(root, log, true, seconds);
}

extern "C" __attribute__((visibility("default"))) int nfsmw_rexhost_memory(const char *root,
                                                                            LogFn log) {
  return Guarded(root, log, false, 0, true);
}
