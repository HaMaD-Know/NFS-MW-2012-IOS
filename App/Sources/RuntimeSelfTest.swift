import Foundation

/// "Run all tests": every N1-b and G1 check in one go, with a pass/fail list on screen.
///
/// Order: the platform self-tests (SelfTest/*.c) and the importer self-test on a background
/// thread, then one ReXGlue start on the main thread (UIKit allows windows only there) that
/// checks graphics, controller, audio, guest memory, fibers and shutdown. ReXGlue can start once
/// per launch, so a second run needs the app closed and opened again.
/// Each line also goes to shell-log.txt synchronously, so a crash still shows how far it got.
final class RuntimeSelfTest: ObservableObject {
    static let shared = RuntimeSelfTest()

    struct Line: Identifiable {
        let id: Int
        let text: String
    }

    enum Status: String {
        case pass = "PASS", fail = "FAIL", skip = "SKIP", expected = "EXPECTED"
    }

    struct TestResult: Identifiable {
        let id: Int
        let name: String
        let status: Status
        let detail: String
    }

    @Published private(set) var lines: [Line] = []
    @Published private(set) var results: [TestResult] = []
    @Published private(set) var running = false
    @Published private(set) var summary = ""
    @Published private(set) var ranThisLaunch = false

    private var nextID = 0

    private init() {}

    func runAll() {
        guard !running, !ranThisLaunch else { return }
        running = true
        ranThisLaunch = true
        lines = []
        results = []
        summary = "Running… (the screen goes black for about 15 seconds near the end)"

        DispatchQueue.global(qos: .userInitiated).async {
            RuntimeSelfTest.emit(String(format: "Run all tests (shell %@). Device %@, app memory available %.2f GB",
                                        Bundle.main.object(forInfoDictionaryKey: "CFBundleShortVersionString") as? String ?? "?",
                                        DeviceInfo.machine, DeviceInfo.availableMemoryGB))
            selftest_set_frameworks_dir(Bundle.main.privateFrameworksPath ?? "")
            selftest_set_documents_dir(
                FileManager.default.urls(for: .documentDirectory, in: .userDomainMask).first?.path ?? "")
            setenv("MVK_CONFIG_LOG_LEVEL", "2", 1)  // MoltenVK: errors and warnings
            crashlog_redirect_stderr()

            // The two "original" methods are iPadOS limits the project works around; failing is
            // the expected result and is shown as such.
            let tests: [(String, Bool, (selftest_log_fn?) -> Int32)] = [
                ("ReXGlue libraries load in the app", false, selftest_runtime_libraries),
                ("Address space for the 4.5 GB guest", false, selftest_address_space),
                ("Guest memory, temporary-file method", false, selftest_backing_file),
                ("Guest memory, Mach memory entry (used)", false, selftest_backing_mach_entry),
                ("Guest memory, vm_remap method", false, selftest_backing_vm_remap),
                ("arm64 fiber switch (standalone)", false, selftest_fibers_arm64),
                ("Original shm_open method (iPadOS blocks it)", true, selftest_guest_memory),
                ("Original ucontext fibers (iPadOS blocks it)", true, selftest_fibers),
                ("Vulkan on Metal through MoltenVK", false, selftest_moltenvk),
            ]
            for (name, expectFail, test) in tests {
                RuntimeSelfTest.emit("— \(name): starting")
                let passed = test(selfTestLogSink) != 0
                let status: Status = expectFail ? (passed ? .fail : .expected) : (passed ? .pass : .fail)
                RuntimeSelfTest.record(name, status, expectFail && !passed ? "fails as expected" : "")
            }

            RuntimeSelfTest.emit("— Importer self-test (synthetic folders and disc images): starting")
            let work = FileManager.default.temporaryDirectory.appendingPathComponent("gamecheck-all")
            let importerPassed = gamecheck_selftest(work.path, gameCheckLogSink) != 0
            try? FileManager.default.removeItem(at: work)
            RuntimeSelfTest.record("Importer checks (fake data)", importerPassed ? .pass : .fail,
                                   "folders and disc images, 13 cases")

            // ReXGlue on the main thread: window, graphics, controller, audio, memory, fibers.
            DispatchQueue.main.async {
                RuntimeSelfTest.emit("— ReXGlue full run (window 15 s; press controller buttons): starting")
                _ = selftest_rexglue_all(selfTestLogSink, 15)
                RuntimeSelfTest.finish()
            }
        }
    }

    private static func finish() {
        DispatchQueue.main.async {
            let shared = RuntimeSelfTest.shared
            let passed = shared.results.filter { $0.status == .pass || $0.status == .expected }.count
            let failed = shared.results.filter { $0.status == .fail }.count
            let skipped = shared.results.filter { $0.status == .skip }.count
            shared.summary = "\(passed) passed, \(failed) failed, \(skipped) skipped"
            shared.running = false
            ShellLog.shared.writeSync(String(format: "[selftest] Run all tests end: %@. App memory available %.2f GB",
                                             shared.summary, DeviceInfo.availableMemoryGB))
        }
    }

    fileprivate static func record(_ name: String, _ status: Status, _ detail: String) {
        ShellLog.shared.writeSync("[result] \(status.rawValue) \(name)\(detail.isEmpty ? "" : " — \(detail)")")
        DispatchQueue.main.async {
            let shared = RuntimeSelfTest.shared
            shared.results.append(TestResult(id: shared.nextID, name: name, status: status, detail: detail))
            shared.nextID += 1
        }
    }

    /// Writes a line to the log file (synchronously) and to the on-screen list. Lines of the form
    /// RESULT|status|name|detail (from the ReXGlue bridge) become entries of the results list.
    static func emit(_ text: String) {
        if text.hasPrefix("RESULT|") {
            let parts = text.split(separator: "|", maxSplits: 3, omittingEmptySubsequences: false).map(String.init)
            if parts.count == 4 {
                record(parts[2], Status(rawValue: parts[1]) ?? .fail, parts[3])
                return
            }
        }
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

/// The importer checker's callback, same idea.
private let gameCheckLogSink: gamecheck_log_fn = { cLine in
    guard let cLine else { return }
    RuntimeSelfTest.emit(String(cString: cLine))
}
