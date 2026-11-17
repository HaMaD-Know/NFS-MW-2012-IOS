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
            RuntimeSelfTest.emit(String(format: "Self-test v2 start. Device %@, app memory available %.2f GB",
                                        DeviceInfo.machine, DeviceInfo.availableMemoryGB))
            // Self-test version 2 (after the first device run on 2026-11-17).
            // The two "ReXGlue original" tests are kept to show the before/after on the same device.
            let tests: [(String, (selftest_log_fn?) -> Int32)] = [
                ("Address space", selftest_address_space),
                ("Memory A: temporary file", selftest_backing_file),
                ("Memory B: Mach memory entry", selftest_backing_mach_entry),
                ("Memory C: vm_remap", selftest_backing_vm_remap),
                ("Fibers: new arm64 switch", selftest_fibers_arm64),
                ("Original shm_open method (failed before)", selftest_guest_memory),
                ("Original ucontext fibers (failed before)", selftest_fibers),
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
