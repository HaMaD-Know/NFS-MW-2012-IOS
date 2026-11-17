import Foundation
import os

/// Writes to the unified log and to Documents/shell-log.txt, which the Files app can open
/// (UIFileSharingEnabled), so results can be read on the iPad without a Mac.
final class ShellLog {
    static let shared = ShellLog()

    private let logger = Logger(subsystem: "dev.nfsmw2012ipad.shell", category: "shell")
    private let queue = DispatchQueue(label: "dev.nfsmw2012ipad.shell.log")
    private let fileURL: URL?
    private let formatter = ISO8601DateFormatter()

    private init() {
        fileURL = FileManager.default
            .urls(for: .documentDirectory, in: .userDomainMask)
            .first?
            .appendingPathComponent("shell-log.txt")
    }

    func write(_ message: String) {
        logger.info("\(message, privacy: .public)")
        guard let fileURL else { return }
        let line = "\(formatter.string(from: Date())) \(message)\n"
        queue.async {
            let data = Data(line.utf8)
            if let handle = try? FileHandle(forWritingTo: fileURL) {
                defer { try? handle.close() }
                _ = try? handle.seekToEnd()
                try? handle.write(contentsOf: data)
            } else {
                try? data.write(to: fileURL)
            }
        }
    }
}
