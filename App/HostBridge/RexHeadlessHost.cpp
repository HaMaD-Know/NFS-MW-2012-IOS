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
#include <rex/apu/audio_driver.h>
#include <rex/apu/audio_system.h>
#include <rex/cli.h>
#include <rex/gpu/graphics_system.h>
#include <rex/hid/input_system.h>
#include <rex/kernel/guest_memory.h>
#include <rex/platform/thread.h>
#include <rex/kernel/title_paths.h>
#include <rex/kernel/title_exit.h>
#include <rex/kernel/title_runtime.h>
#include <rex/ppc/image.h>
#include <rex/setting.h>
#include <rex/result.h>
#include <rex/ui/ui_system.h>

#include <dlfcn.h>

// The project's own Xbox 360 test program (Tools/make_test_xex.py), recompiled by ReXGlue in CI.
// Its generated code is compiled into this library only by ipad-runtime-app.yml.
#if __has_include("testgame_init.h")
#include "testgame_init.h"
#define NFSMW_HAVE_TEST_GAME 1
#endif

#include <algorithm>
#include <array>
#include <cmath>
#include <thread>
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

// One line per checked item for the app's results list: RESULT|PASS or FAIL or SKIP|name|detail.
void ReportResult(const char *status, const std::string &name, const std::string &detail) {
  Say(std::string("RESULT|") + status + "|" + name + "|" + detail);
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
  // libSDL3d.0.dylib in a Debug build of ReXGlue, libSDL3.0.dylib in Release.
  std::filesystem::path sdl;
  std::error_code list_error;
  for (const auto &entry : std::filesystem::directory_iterator(OwnFolder(), list_error)) {
    const std::string name = entry.path().filename().string();
    if (name.rfind("libSDL3", 0) == 0 && entry.path().extension() == ".dylib") sdl = entry.path();
  }
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
// Plays a quiet 440 Hz tone for about two seconds through ReXGlue's audio system (front left and
// right), the way a title's audio reaches the device: one planar 6-channel frame of 256 samples
// at 48 kHz per Submit. Returns a report line.
std::string PlayTestTone(rex::apu::IAudioSystem &audio, bool *ok) {
  *ok = false;
  using rex::apu::kAudioChannelSamples;
  using rex::apu::kAudioFrameSamples;
  using rex::apu::kAudioSampleRate;
  auto stream = audio.OpenStream([] {});
  if (!stream) return "Audio: could not open a stream: " + stream.error().GetDescription();
  constexpr int kFrames = 2 * static_cast<int>(kAudioSampleRate / kAudioChannelSamples);  // ~2 s
  std::array<float, kAudioFrameSamples> frame{};
  double phase = 0.0;
  const double step = 2.0 * 3.14159265358979 * 440.0 / kAudioSampleRate;
  int submitted = 0, waits = 0;
  for (int f = 0; f < kFrames; f++) {
    for (std::uint32_t i = 0; i < kAudioChannelSamples; i++) {
      const float sample = static_cast<float>(0.15 * std::sin(phase));
      phase += step;
      frame[i] = sample;                         // front left
      frame[kAudioChannelSamples + i] = sample;  // front right
    }
    for (int tries = 0; tries < 2000; tries++) {
      if (audio.Submit(*stream, rex::apu::AudioFrame(frame))) {
        submitted++;
        break;
      }
      waits++;
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(150));  // let the queue drain
  const rex::apu::AudioStats stats = audio.GetStats();
  audio.CloseStream(*stream);
  *ok = submitted == kFrames;
  return "Audio: tone played, " + std::to_string(submitted) + " of " + std::to_string(kFrames) +
         " frames accepted (" + std::to_string(waits) + " waits for a full queue); device " +
         std::to_string(audio.GetOutputChannels()) + " channels; latency " +
         std::to_string(stats.latency_us / 1000) + " ms; underruns " + std::to_string(stats.underruns);
}

void RunWindow(Host &app, int seconds) {
  rex::gpu::IGraphicsSystem &graphics = app.GetGraphics();
  rex::ui::IUISystem &ui = app.GetUI();
  rex::hid::IInputSystem &input = app.GetInput();
  Say(std::string("  Graphics driver: ") + (graphics.HasDriver() ? graphics.GetDriverName() : "none") +
      "; window: " + (ui.HasWindow() ? "open" : "none") + (ui.IsHeadless() ? " (headless)" : ""));
  rex::apu::IAudioSystem &audio = app.GetAudio();
  Say("  Audio driver: " + audio.GetSettings().driver.Get() + ", " +
      std::to_string(audio.GetOutputChannels()) + " output channels. Playing a 2-second test tone.");
  std::string tone_report;
  bool tone_ok = false;
  std::thread tone([&audio, &tone_report, &tone_ok] { tone_report = PlayTestTone(audio, &tone_ok); });
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
  bool pad_seen = false;
  int input_changes = 0;
  std::string pad_name;
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
        pad_seen = true;
        pad_name = std::string(PadTypeName(info->type)) + ids;
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
        if (pad->packet != last_packet) {
          line += " (changed)";
          if (last_packet != 0 || pad->state.buttons || pad->state.left_trigger ||
              pad->state.right_trigger || pad->state.thumb_lx || pad->state.thumb_ly ||
              pad->state.thumb_rx || pad->state.thumb_ry)
            input_changes++;
        }
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
  tone.join();
  Say("  " + tone_report);

  const bool drawing = graphics.HasDriver() && ui.HasWindow() && loops > 0;
  ReportResult(drawing ? "PASS" : "FAIL", "Graphics: Vulkan on Metal in a ReXGlue window",
         std::string(graphics.HasDriver() ? graphics.GetDriverName() : "no driver") + ", window " +
             (ui.HasWindow() ? "open" : "missing") + ", " + std::to_string(loops) + " frame-loop passes");
  if (!pad_seen) {
    ReportResult("SKIP", "Controller input", "no controller connected");
  } else if (input_changes == 0) {
    ReportResult("SKIP", "Controller input", pad_name + " connected, but no buttons were pressed");
  } else {
    ReportResult("PASS", "Controller input", pad_name + ", " + std::to_string(input_changes) + " input changes read");
  }
  ReportResult(tone_ok ? "PASS" : "FAIL", "Audio: test tone through ReXGlue", tone_report);
}

// Step after B7: ReXGlue's own guest memory on the iPad. TitleRuntime::Load with an image that
// has no recompiled code ("tool mode") and an empty title.entry maps the full guest address space
// (patch 0004's Mach memory entry and its nine views) and creates the kernel, loading no
// executable. The test then allocates guest memory, writes and reads it, checks that physical
// memory is visible through its mirror views, and unloads.
// ReXGlue's own fibers (rex::platform::Fiber, the arm64 switch of patch 0005), the mechanism
// guest threads use for guest fibers. Runs on a fresh thread: convert it, ping-pong through two
// fibers, check a floating-point value kept across switches, run a deep call stack in a fiber,
// time the switches, and destroy everything.
struct FiberRun {
  rex::platform::Fiber *main = nullptr, *a = nullptr, *b = nullptr, *deep = nullptr;
  std::uint64_t count = 0;
  double kept = 0.0;
  int deep_result = -1;
};
FiberRun g_fibers;

int DeepSum(int n) {
  volatile char pad[1024];
  pad[0] = static_cast<char>(n);
  return n == 0 ? 0 : n + DeepSum(n - 1) + (pad[0] - static_cast<char>(n));
}

void FiberA(void *) {
  double value = 1.5;
  for (;;) {
    g_fibers.count++;
    rex::platform::Fiber::SwitchTo(g_fibers.b);
    value += 1.0;
    g_fibers.kept = value;
    value = 1.5;
  }
}

void FiberB(void *) {
  for (;;) {
    g_fibers.count++;
    rex::platform::Fiber::SwitchTo(g_fibers.main);
  }
}

void FiberDeep(void *) {
  g_fibers.deep_result = DeepSum(200);
  for (;;) rex::platform::Fiber::SwitchTo(g_fibers.main);
}

bool RunFibers() {
  bool ok = false;
  std::string report;
  std::thread worker([&ok, &report] {
    using rex::platform::Fiber;
    g_fibers = FiberRun{};
    g_fibers.main = Fiber::ConvertCurrentThread();
    g_fibers.a = Fiber::Create(256 << 10, FiberA, nullptr);
    g_fibers.b = Fiber::Create(256 << 10, FiberB, nullptr);
    g_fibers.deep = Fiber::Create(1 << 20, FiberDeep, nullptr);
    if (!g_fibers.main || !g_fibers.a || !g_fibers.b || !g_fibers.deep) {
      report = "Fibers: could not create the fibers";
      return;
    }
    const bool current_ok = Fiber::GetCurrent() == g_fibers.main;
    constexpr int kRounds = 100000;
    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < kRounds; i++) Fiber::SwitchTo(g_fibers.a);  // main -> a -> b -> main
    const auto elapsed = std::chrono::steady_clock::now() - start;
    Fiber::SwitchTo(g_fibers.deep);
    const double ns = std::chrono::duration<double, std::nano>(elapsed).count() / (3.0 * kRounds);
    const bool count_ok = g_fibers.count == 2ull * kRounds;
    const bool kept_ok = g_fibers.kept == 2.5;
    const bool deep_ok = g_fibers.deep_result == 20100;
    g_fibers.a->Destroy();
    g_fibers.b->Destroy();
    g_fibers.deep->Destroy();
    g_fibers.main->Destroy();
    char text[300];
    std::snprintf(text, sizeof text,
                  "Fibers (ReXGlue rex::platform::Fiber): %d rounds of 3 switches, count %llu "
                  "(expected %d) %s, float kept %.1f %s, deep stack sum %d %s, current fiber %s, "
                  "%.1f ns per switch",
                  kRounds, static_cast<unsigned long long>(g_fibers.count), 2 * kRounds,
                  count_ok ? "ok" : "WRONG", g_fibers.kept, kept_ok ? "ok" : "WRONG",
                  g_fibers.deep_result, deep_ok ? "ok" : "WRONG", current_ok ? "ok" : "WRONG", ns);
    report = text;
    ok = count_ok && kept_ok && deep_ok && current_ok;
  });
  worker.join();
  Say("  " + report);
  return ok;
}

// Runs the recompiled test program through ReXGlue's real title path: TitleRuntime::Load mounts
// the app's TestGame folder as game: and loads default.xex (our own, unencrypted), Launch starts
// its main thread on the recompiled ARM64 code, and the title ends when the code returns. The
// program sums 1..50,000,000 and stores the result and a marker in guest memory.
bool RunTestGame(Host &app) {
#ifndef NFSMW_HAVE_TEST_GAME
  (void)app;
  ReportResult("SKIP", "Test game (our own Xbox 360 program)", "not built into this app");
  return true;
#else
  const std::filesystem::path game = OwnFolder().parent_path() / "TestGame";
  rex::kernel::TitleRuntime title(kPPCImageConfig);
  // The title paths are process-wide: the guest-memory test before this one cleared title.entry
  // (shell 0.1.4 then loaded no executable: "PrepareModuleLaunch: No executable module loaded").
  if (auto set = title.GetTitlePaths().entry.SetText(rex::SettingLayer::kTitle, "default.xex"); !set) {
    ReportResult("FAIL", "Test game (our own Xbox 360 program)", "entry: " + set.error().GetDescription());
    return false;
  }
  if (auto set = title.GetTitlePaths().game_root.SetText(rex::SettingLayer::kTitle, game.string()); !set) {
    ReportResult("FAIL", "Test game (our own Xbox 360 program)", "game_root: " + set.error().GetDescription());
    return false;
  }
  Say("  Test game: TitleRuntime::Load of TestGame/default.xex");
  if (auto loaded = title.Load(); !loaded) {
    ReportResult("FAIL", "Test game (our own Xbox 360 program)", "Load: " + loaded.error().GetDescription());
    return false;
  }
  std::atomic<int> exit_reason{-1};
  auto guard = title.OnTitleExit([&exit_reason](rex::kernel::TitleExitReason reason) {
    exit_reason.store(static_cast<int>(reason));
  });
  Say("  Test game: Launch (recompiled code runs on the title's main thread)");
  const auto start = std::chrono::steady_clock::now();
  if (auto launched = title.Launch(); !launched) {
    ReportResult("FAIL", "Test game (our own Xbox 360 program)", "Launch: " + launched.error().GetDescription());
    title.Unload();
    return false;
  }
  while (exit_reason.load() < 0 && std::chrono::steady_clock::now() - start < std::chrono::seconds(20)) {
    if (!app.RunFrame()) break;
  }
  const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
  std::uint8_t *base = title.GetMembase().GetBase();
  // The guest stores big-endian words; read them raw and swap.
  const auto word = [base](std::uint32_t address) -> std::uint32_t {
    std::uint32_t raw = 0;
    if (base) std::memcpy(&raw, base + address, 4);
    return __builtin_bswap32(raw);
  };
  const std::uint32_t sum = word(0x82004000u), marker = word(0x82004004u);
  const std::uint32_t frequency = word(0x82004010u);
  const std::uint64_t system_time = (std::uint64_t(word(0x82004018u)) << 32) | word(0x8200401Cu);
  const std::uint32_t create_status = word(0x82004024u), wait_status = word(0x82004028u);
  const std::uint32_t thread_value = word(0x82004030u), thread_marker = word(0x82004034u);
  constexpr std::uint64_t kN = 50000000ull;
  const std::uint32_t expected = static_cast<std::uint32_t>(kN * (kN + 1) / 2);
  const int reason = exit_reason.load();
  title.Unload();

  // The same loop written natively, for comparison (volatile bound so it is not folded away).
  volatile std::uint32_t native_n = static_cast<std::uint32_t>(kN);
  const auto native_start = std::chrono::steady_clock::now();
  std::uint32_t native_sum = 0;
  for (std::uint32_t i = 1; i <= native_n; i++) native_sum += i;
  const double native_seconds =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - native_start).count();

  const bool code_ok = reason == static_cast<int>(rex::kernel::TitleExitReason::kNormalExit) &&
                       sum == expected && marker == 0x600DF00Du;
  char text[400];
  std::snprintf(text, sizeof text,
                "exit %s, sum %08X (expected %08X), marker %08X, %.3f s for the whole program "
                "(the loop is 200 million guest instructions, recompiled code built with -O2); the "
                "same loop native: %.3f s, sum %s",
                reason < 0 ? "never came" : reason == 0 ? "normal" : "abnormal", sum, expected, marker,
                seconds, native_seconds, native_sum == expected ? "same" : "DIFFERENT");
  Say(std::string("  Test game: ") + text);
  ReportResult(code_ok ? "PASS" : "FAIL", "Test game: recompiled code runs", text);

  // Unix seconds from the FILETIME (100 ns units since 1601).
  const long long unix_seconds = static_cast<long long>(system_time / 10000000ull) - 11644473600ll;
  const bool kernel_ok = code_ok && frequency != 0 && unix_seconds > 1700000000ll &&
                         create_status == 0 && wait_status == 0 && thread_value == 0x2468u &&
                         thread_marker == 0x7EADu;
  std::snprintf(text, sizeof text,
                "performance frequency %u Hz; system time %lld (Unix seconds); ExCreateThread status "
                "%08X; wait status %08X; second thread result %04X (expected 2468), marker %04X",
                frequency, unix_seconds, create_status, wait_status, thread_value, thread_marker);
  Say(std::string("  Test game: ") + text);
  ReportResult(kernel_ok ? "PASS" : "FAIL", "Test game: Xbox kernel calls (time, thread, wait)", text);
  const bool ok = code_ok && kernel_ok;
  return ok;
#endif
}

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

  std::vector<std::string> args = {"--app.root=" + root.string()};
  if (window) {
    args.push_back("--gpu.driver=vulkan");  // MoltenVK on Metal; audio stays on its sdl3 default
    TellSDLMainIsReady();
  } else {
    args.push_back("--apu.driver=none");   // no audio
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
      ReportResult("FAIL", "ReXGlue starts", init.error().GetDescription());
    } else if (rex::app::IsShutdownRequested()) {
      Say("  Init ended early because a shutdown was requested");
    } else {
      init_ok = true;
      Say(std::string("  Init OK, phase: ") + PhaseName(app.GetPhase()) +
          (app.HasUISystem() ? "" : " (no UI system)"));
      ReportResult("PASS", "ReXGlue starts", "all 8 system modules loaded and initialized");
      if (window) RunWindow(app, seconds);
      if (guest_memory) {
        const bool memory_ok = RunGuestMemory();
        Say(std::string("  Guest memory test: ") + (memory_ok ? "ok" : "FAILED"));
        ReportResult(memory_ok ? "PASS" : "FAIL", "Guest memory inside ReXGlue",
               "title and physical memory, mirror view");
        const bool game_ok = RunTestGame(app);
        Say(std::string("  Test game: ") + (game_ok ? "ok" : "FAILED"));
        const bool fibers_ok = RunFibers();
        Say(std::string("  Fiber test: ") + (fibers_ok ? "ok" : "FAILED"));
        ReportResult(fibers_ok ? "PASS" : "FAIL", "Fibers inside ReXGlue", "switches, saved values, deep stack");
        init_ok = init_ok && memory_ok && fibers_ok && game_ok;
      }
    }
    Say("  Shutdown: starting");
    exit_code = app.Shutdown();
    Say("  Shutdown returned " + std::to_string(exit_code) + ", phase: " + PhaseName(app.GetPhase()));
    ReportResult(exit_code == 0 ? "PASS" : "FAIL", "ReXGlue shuts down cleanly",
           "exit code " + std::to_string(exit_code));
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

// Everything in one ReXGlue start, on the main thread: window, Vulkan graphics, controller, audio
// tone, then guest memory and fibers, then shutdown.
extern "C" __attribute__((visibility("default"))) int nfsmw_rexhost_all(const char *root, LogFn log,
                                                                         int seconds) {
  return Guarded(root, log, true, seconds, true);
}
