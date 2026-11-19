import Foundation

/// "Import Your Game" (Milestone G1, BYOG_DESIGN.md). The user picks their own game folder with
/// the iPadOS Files picker; the app keeps a security-scoped bookmark to it ("use in place") and
/// checks it with GameCheck.c. The app only reads the folder: it never changes, copies or
/// uploads the user's files.
///
/// No edition has a real manifest yet (that needs the owner's own copy, Milestone G2), so a real
/// copy can be identified but not yet checked for completeness. For testing, "Make test game
/// folder" writes a synthetic folder (no game content) and a test manifest into Documents.
final class GameLibrary: ObservableObject {
    static let shared = GameLibrary()

    enum Phase: Equatable {
        case noGame
        case verifying
        case result(state: Int32)
        case needsAttention(String)
    }

    @Published private(set) var phase: Phase = .noGame
    @Published private(set) var title = "No game chosen"
    @Published private(set) var message = "Choose the folder that holds your own copy's default.xex."
    @Published private(set) var details: [String] = []
    @Published private(set) var folderName = ""
    @Published private(set) var testManifestActive = false

    private let bookmarkKey = "gameFolderBookmark"
    private let queue = DispatchQueue(label: "game-import", qos: .userInitiated)

    private var documents: URL {
        FileManager.default.urls(for: .documentDirectory, in: .userDomainMask)[0]
    }
    private var testRoot: URL { documents.appendingPathComponent("ImportTest", isDirectory: true) }
    private var testManifest: URL { testRoot.appendingPathComponent("test-manifest.txt") }

    private init() {}

    var busy: Bool { phase == .verifying }

    // MARK: Choosing and restoring

    /// Called with the folder the Files picker returned.
    func choseFolder(_ url: URL) {
        let access = url.startAccessingSecurityScopedResource()
        defer { if access { url.stopAccessingSecurityScopedResource() } }
        do {
            let bookmark = try url.bookmarkData(options: [], includingResourceValuesForKeys: nil, relativeTo: nil)
            UserDefaults.standard.set(bookmark, forKey: bookmarkKey)
            log("Chose folder \"\(url.lastPathComponent)\" (bookmark saved, used in place)")
        } catch {
            log("Could not remember the folder: \(error.localizedDescription)")
        }
        verify(url, full: true)
    }

    /// Called with the file the Files picker returned for "Choose Disc Image".
    func choseImage(_ url: URL) {
        let access = url.startAccessingSecurityScopedResource()
        publish(.verifying, title: "Checking disc image…", message: "", details: [])
        queue.async {
            defer { if access { url.stopAccessingSecurityScopedResource() } }
            var lines: [String] = []
            var report = gamecheck_report()
            let state = GameLibrary.collect(into: &lines) { sink in
                gamecheck_probe_image(url.path, sink, &report)
            }
            self.finish(state: state, report: report, lines: lines, name: url.lastPathComponent)
        }
    }

    /// At launch: find the remembered folder again and run the quick check.
    func restore() {
        refreshTestManifest()
        guard let url = resolveBookmark() else { return }
        verify(url, full: false)
    }

    func verifyAgain() {
        refreshTestManifest()
        guard let url = resolveBookmark() else { return }
        verify(url, full: true)
    }

    /// Forgets the remembered folder. The user's files are not touched.
    func forget() {
        UserDefaults.standard.removeObject(forKey: bookmarkKey)
        log("Forgot the game folder (the folder itself was not changed)")
        publish(.noGame, title: "No game chosen",
                message: "Choose the folder that holds your own copy's default.xex.", details: [])
        DispatchQueue.main.async { self.folderName = "" }
    }

    private func resolveBookmark() -> URL? {
        guard let data = UserDefaults.standard.data(forKey: bookmarkKey) else {
            publish(.noGame, title: "No game chosen",
                    message: "Choose the folder that holds your own copy's default.xex.", details: [])
            return nil
        }
        var stale = false
        guard let url = try? URL(resolvingBookmarkData: data, options: [], relativeTo: nil,
                                 bookmarkDataIsStale: &stale),
              FileManager.default.fileExists(atPath: url.path) || startAndCheck(url) else {
            log("The remembered game folder cannot be found")
            publish(.needsAttention("missing"), title: "Needs attention",
                    message: "The game folder you chose before cannot be found. It may have been moved, renamed or deleted, or its drive is not connected. Choose it again.",
                    details: [])
            return nil
        }
        if stale, let fresh = try? url.bookmarkData(options: [], includingResourceValuesForKeys: nil, relativeTo: nil) {
            UserDefaults.standard.set(fresh, forKey: bookmarkKey)
            log("Bookmark was stale and has been renewed")
        }
        return url
    }

    private func startAndCheck(_ url: URL) -> Bool {
        let access = url.startAccessingSecurityScopedResource()
        defer { if access { url.stopAccessingSecurityScopedResource() } }
        return FileManager.default.fileExists(atPath: url.path)
    }

    // MARK: Verification

    private func verify(_ url: URL, full: Bool) {
        let name = url.lastPathComponent
        let manifest = testManifestActive ? testManifest.path : nil
        publish(.verifying, title: full ? "Verifying…" : "Checking…",
                message: full ? "Reading every listed file. A full game takes several minutes." : "", details: [])
        log("Verify \"\(name)\" (\(full ? "full" : "quick") check, \(manifest == nil ? "no manifest" : "test manifest"))")
        queue.async {
            let access = url.startAccessingSecurityScopedResource()
            defer { if access { url.stopAccessingSecurityScopedResource() } }
            var lines: [String] = []
            var report = gamecheck_report()
            let state = GameLibrary.collect(into: &lines) { sink in
                gamecheck_verify_folder(url.path, manifest, full ? 1 : 0, sink, &report)
            }
            self.finish(state: state, report: report, lines: lines, name: name)
        }
    }

    private func finish(state: Int32, report: gamecheck_report, lines: [String], name: String) {
        var report = report
        let message = GameLibrary.text(&report.message)
        let title: String
        switch UInt32(bitPattern: state) {
        case GC_VERIFIED.rawValue: title = "✓ Game data verified"
        case GC_IDENTIFIED_NO_MANIFEST.rawValue: title = "✓ Game identified (edition not yet supported)"
        case GC_INCOMPLETE.rawValue: title = "⚠ Incomplete or damaged"
        case GC_DISC_IMAGE_NOT_YET.rawValue: title = "Disc image found"
        default: title = "✗ Cannot use this"
        }
        log("Result: \(String(cString: gamecheck_state_name(state))). \(message)")
        publish(.result(state: state), title: title, message: message, details: lines)
        DispatchQueue.main.async { self.folderName = name }
    }

    // MARK: Test data

    /// Writes the synthetic test folder Documents/ImportTest/Game (no game content) and its test
    /// manifest. variant: 0 complete, 1 missing file, 2 damaged file, 3 different game.
    func makeTestFolder(variant: Int32) {
        let root = testRoot.appendingPathComponent("Game", isDirectory: true)
        try? FileManager.default.createDirectory(at: testRoot, withIntermediateDirectories: true)
        queue.async {
            let ok = gamecheck_make_test_folder(root.path, self.testManifest.path, variant, nil) != 0
            self.log("Test folder variant \(variant): \(ok ? "written" : "FAILED") at Documents/ImportTest/Game")
            self.refreshTestManifest()
            self.publishDetails(["Test folder written (variant \(variant)). Choose it with Choose Game Folder: Files › On My iPad › NFS MW Shell (inside LiveContainer: LiveContainer › Data › …) › ImportTest › Game"])
        }
    }

    /// Removes the synthetic test folder and the test manifest.
    func removeTestData() {
        try? FileManager.default.removeItem(at: testRoot)
        refreshTestManifest()
        log("Test folder and test manifest removed")
    }

    /// Runs every checker case on synthetic folders in the temporary folder.
    func runSelfTest() {
        publish(.verifying, title: "Importer self-test…", message: "", details: [])
        queue.async {
            let work = FileManager.default.temporaryDirectory.appendingPathComponent("gamecheck-selftest")
            var lines: [String] = []
            let passed = GameLibrary.collect(into: &lines) { sink in
                gamecheck_selftest(work.path, sink)
            } != 0
            try? FileManager.default.removeItem(at: work)
            for line in lines { self.log(line) }
            self.publish(.noGame, title: "Importer self-test \(passed ? "PASS" : "FAIL")",
                         message: "Synthetic folders only, no game data.", details: lines)
        }
    }

    private func refreshTestManifest() {
        let active = FileManager.default.fileExists(atPath: testManifest.path)
        DispatchQueue.main.async { self.testManifestActive = active }
    }

    // MARK: Helpers

    private func publish(_ phase: Phase, title: String, message: String, details: [String]) {
        DispatchQueue.main.async {
            self.phase = phase
            self.title = title
            self.message = message
            self.details = details
        }
    }

    private func publishDetails(_ details: [String]) {
        DispatchQueue.main.async { self.details = details }
    }

    private func log(_ text: String) {
        ShellLog.shared.writeSync("[import] \(text)")
    }

    /// Runs a C call that reports lines through a C callback, collecting the lines.
    private static func collect<T>(into lines: inout [String], _ body: (gamecheck_log_fn) -> T) -> T {
        GameLibrary.collected = []
        let result = body(gameCheckSink)
        lines = GameLibrary.collected
        return result
    }

    fileprivate static var collected: [String] = []

    private static func text<T>(_ tuple: inout T) -> String {
        withUnsafeBytes(of: &tuple) { raw in
            String(decoding: raw.prefix { $0 != 0 }, as: UTF8.self)
        }
    }
}

/// C callback for the checker. All checker calls run on GameLibrary's serial queue.
private let gameCheckSink: gamecheck_log_fn = { line in
    guard let line else { return }
    GameLibrary.collected.append(String(cString: line))
}
