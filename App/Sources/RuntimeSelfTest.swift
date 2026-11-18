import Foundation

/// Runs the N1-b runtime self-tests (SelfTest/*.c) on a background thread.
/// Each line is written to the log file synchronously before the next step starts, so if a test
/// crashes the app, shell-log.txt still shows how far it got.
final class RuntimeSelfTest: ObservableObject {
    static let shared = RuntimeSelfTest()

    struct Line: Identifiable {
        let id: Int
        let text: String
    }

    @Published private(set) var lines: [Line] = []
    @Published private(set) var running = false
    @Published private(set) var summary = ""

    private var nextID = 0

    private init() {}

    func run() {
        guard !running else { return }
        running = true
        lines = []
        summary = ""

        DispatchQueue.global(qos: .userInitiated).async {
            RuntimeSelfTest.emit(String(format: "Self-test v8 start. Device %@, app memory available %.2f GB",
                                        DeviceInfo.machine, DeviceInfo.availableMemoryGB))
            selftest_set_frameworks_dir(Bundle.main.privateFrameworksPath ?? "")
            selftest_set_documents_dir(
                FileManager.default.urls(for: .documentDirectory, in: .userDomainMask).first?.path ?? "")
            setenv("MVK_CONFIG_LOG_LEVEL", "3", 1)  // MoltenVK: errors, warnings and info
            crashlog_redirect_stderr()
            // Version 3: packaging probe first (ReXGlue libraries and MoltenVK inside the app),
            // then the version 2 mechanism tests. The two "original" tests show the before/after.
            let tests: [(String, (selftest_log_fn?) -> Int32)] = [
                ("ReXGlue libraries in the app", selftest_runtime_libraries),
                ("Address space", selftest_address_space),
                ("Memory A: temporary file", selftest_backing_file),
                ("Memory B: Mach memory entry", selftest_backing_mach_entry),
                ("Memory C: vm_remap", selftest_backing_vm_remap),
                ("Fibers: new arm64 switch", selftest_fibers_arm64),
                ("Original shm_open method (failed before)", selftest_guest_memory),
                ("Original ucontext fibers (failed before)", selftest_fibers),
                // Last, because it crashed in vkCreateInstance on 2026-11-18 (shell 0.0.3).
                ("Vulkan via MoltenVK", selftest_moltenvk),
                // Version 7 (step B7): the real ReXGlue runtime, started and stopped with no game.
                ("ReXGlue runtime start (no game)", selftest_rexglue_headless),
            ]
            var results: [String] = []
            for (name, test) in tests {
                RuntimeSelfTest.emit("— \(name): starting")
                let passed = test(selfTestLogSink) != 0
                results.append("\(name) \(passed ? "PASS" : "FAIL")")
            }
            let line = String(format: "Self-test end: %@. App memory available %.2f GB",
                              results.joined(separator: ", "), DeviceInfo.availableMemoryGB)
            RuntimeSelfTest.emit(line)
            DispatchQueue.main.async {
                RuntimeSelfTest.shared.summary = results.joined(separator: " · ")
                RuntimeSelfTest.shared.running = false
            }
        }
    }

    /// Step B7 part 2: ReXGlue with Vulkan graphics, its own SDL3 window and the controller,
    /// for 15 seconds, then shut down. UIKit allows windows only on the main thread, so this
    /// runs there; the shell's screen is covered and frozen until it ends. ReXGlue can start
    /// once per launch, so this needs a fresh launch without the self-test before it.
    func runWindowTest() {
        guard !running else { return }
        running = true
        lines = []
        summary = ""
        RuntimeSelfTest.emit(String(format: "ReXGlue window test start. Device %@, app memory available %.2f GB",
                                    DeviceInfo.machine, DeviceInfo.availableMemoryGB))
        selftest_set_frameworks_dir(Bundle.main.privateFrameworksPath ?? "")
        selftest_set_documents_dir(
            FileManager.default.urls(for: .documentDirectory, in: .userDomainMask).first?.path ?? "")
        setenv("MVK_CONFIG_LOG_LEVEL", "2", 1)  // MoltenVK: errors and warnings
        crashlog_redirect_stderr()
        // Let the button's tap finish first, then take over the main thread.
        DispatchQueue.main.async {
            let passed = selftest_rexglue_window(selfTestLogSink, 15) != 0
            let line = String(format: "ReXGlue window test end: %@. App memory available %.2f GB",
                              passed ? "PASS" : "FAIL", DeviceInfo.availableMemoryGB)
            RuntimeSelfTest.emit(line)
            RuntimeSelfTest.shared.summary = "ReXGlue window test \(passed ? "PASS" : "FAIL")"
            RuntimeSelfTest.shared.running = false
        }
    }

    /// Writes a line to the log file (synchronously) and to the on-screen list.
    static func emit(_ text: String) {
        ShellLog.shared.writeSync("[selftest] \(text)")
        DispatchQueue.main.async {
            let shared = RuntimeSelfTest.shared
            shared.lines.append(Line(id: shared.nextID, text: text))
            shared.nextID += 1
        }
    }
}

/// C callback handed to the self-tests. Captures nothing, as a C function pointer must.
private let selfTestLogSink: selftest_log_fn = { cLine in
    guard let cLine else { return }
    RuntimeSelfTest.emit(String(cString: cLine))
}
