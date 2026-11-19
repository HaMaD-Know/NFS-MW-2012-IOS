import SwiftUI

struct ContentView: View {
    @EnvironmentObject private var controllers: ControllerMonitor
    @EnvironmentObject private var stats: RenderStats
    @ObservedObject private var selfTest = RuntimeSelfTest.shared

    var body: some View {
        ZStack(alignment: .topLeading) {
            MetalView(stats: stats)
                .ignoresSafeArea()

            ScrollView {
            VStack(alignment: .leading, spacing: 6) {
                Text("NFS MW 2012 iPad — platform test shell")
                    .font(.headline)
                Text("Test only. This is not the game and contains no game code.")
                    .font(.caption)
                Divider().frame(width: 360)
                Text("Device: \(DeviceInfo.machine)")
                Text("Metal: \(stats.deviceName)")
                Text(String(format: "Frame rate: %.0f fps (%.2f ms)", stats.fps, stats.frameMs))
                Text("Drawable: \(stats.drawableSize)")
                Text(String(format: "Runtime shader compile: %.1f ms", stats.shaderCompileMs))
                Text(String(format: "App memory available: %.2f GB", stats.memoryAvailableGB))
                Divider().frame(width: 360)
                if controllers.controllers.isEmpty {
                    Text("Controllers: none connected")
                } else {
                    Text("Controllers:")
                    ForEach(controllers.controllers) { controller in
                        Text("  \(controller.name) — \(controller.profile)")
                    }
                }
                Text("Last input: \(controllers.lastInput)")
                if let error = stats.error {
                    Text("Error: \(error)")
                        .foregroundStyle(.red)
                }
                Divider().frame(width: 360)
                Button(selfTest.running ? "Testing…" : "Run all tests") {
                    selfTest.runAll()
                }
                .disabled(selfTest.running || selfTest.ranThisLaunch)
                .buttonStyle(.borderedProminent)
                Text(selfTest.ranThisLaunch && !selfTest.running
                     ? "To run again, close the app fully and open it."
                     : "Connect the controller first. Near the end the screen turns black for 15 s: press buttons then. You should hear a short beep.")
                    .font(.caption)
                    .fixedSize(horizontal: false, vertical: true)
                    .frame(width: 400, alignment: .leading)
                if !selfTest.summary.isEmpty {
                    Text(selfTest.summary).bold()
                }
                ForEach(selfTest.results) { result in
                    HStack(alignment: .top, spacing: 6) {
                        Text(icon(result.status))
                            .foregroundStyle(color(result.status))
                        VStack(alignment: .leading, spacing: 0) {
                            Text(result.name)
                                .font(.system(.caption, design: .monospaced))
                            if !result.detail.isEmpty {
                                Text(result.detail)
                                    .font(.system(.caption2, design: .monospaced))
                                    .foregroundStyle(.white.opacity(0.7))
                                    .lineLimit(2)
                            }
                        }
                    }
                    .frame(width: 400, alignment: .leading)
                }
                if selfTest.running, let last = selfTest.lines.last {
                    Text(last.text)
                        .font(.system(.caption2, design: .monospaced))
                        .lineLimit(1)
                        .frame(width: 400, alignment: .leading)
                }
                Text("Log: Files › On My iPad › NFS MW Shell › shell-log.txt")
                    .font(.caption2)
            }
            .font(.system(.body, design: .monospaced))
            .foregroundStyle(.white)
            .padding(14)
            .background(Color.black.opacity(0.55), in: RoundedRectangle(cornerRadius: 10))
            }
            .frame(width: 460)
            .padding(.vertical, 24)
            .padding(.leading, 24)

            GameImportPanel()
                .padding(24)
                .frame(maxWidth: .infinity, alignment: .topTrailing)
        }
    }

    private func icon(_ status: RuntimeSelfTest.Status) -> String {
        switch status {
        case .pass: return "✓"
        case .expected: return "✓"
        case .fail: return "✗"
        case .skip: return "–"
        }
    }

    private func color(_ status: RuntimeSelfTest.Status) -> Color {
        switch status {
        case .pass, .expected: return .green
        case .fail: return .red
        case .skip: return .yellow
        }
    }
}
