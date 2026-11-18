import SwiftUI
import UIKit
import os

/// iPadOS test shell for Milestone N1-a.
/// It proves the platform pieces only: an ARM64 app built without a Mac, Metal rendering,
/// game-controller detection, logging and a memory-limit reading.
/// It contains no game code and is not a port of any game.
@main
struct ShellApp: App {
    @StateObject private var controllers = ControllerMonitor()
    @StateObject private var stats = RenderStats()

    init() {
        if let documents = FileManager.default.urls(for: .documentDirectory, in: .userDomainMask).first {
            crashlog_install(documents.appendingPathComponent("shell-log.txt").path)
        }
        NSSetUncaughtExceptionHandler { exception in
            ShellLog.shared.writeSync("CRASH: Objective-C exception \(exception.name.rawValue): \(exception.reason ?? "")")
        }
        let info = ProcessInfo.processInfo
        ShellLog.shared.write("Launch: iPadOS \(UIDevice.current.systemVersion), model \(DeviceInfo.machine)")
        ShellLog.shared.write(String(format: "Device RAM %.2f GB, app memory available at launch %.2f GB",
                                     Double(info.physicalMemory) / 1_073_741_824,
                                     DeviceInfo.availableMemoryGB))
    }

    var body: some Scene {
        WindowGroup {
            ContentView()
                .environmentObject(controllers)
                .environmentObject(stats)
                .persistentSystemOverlays(.hidden)
                .statusBarHidden(true)
        }
    }
}

enum DeviceInfo {
    /// Hardware identifier, e.g. "iPad14,3" for an 11-inch iPad Pro (M2).
    static var machine: String {
        var system = utsname()
        uname(&system)
        return withUnsafeBytes(of: &system.machine) { raw in
            String(decoding: raw.prefix { $0 != 0 }, as: UTF8.self)
        }
    }

    /// Memory this process may still allocate before iOS ends it (os_proc_available_memory).
    /// Measures the per-app limit that applies under the signing used to install the app.
    static var availableMemoryGB: Double {
        Double(os_proc_available_memory()) / 1_073_741_824
    }
}
