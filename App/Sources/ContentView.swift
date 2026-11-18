import SwiftUI

struct ContentView: View {
    @EnvironmentObject private var controllers: ControllerMonitor
    @EnvironmentObject private var stats: RenderStats
    @ObservedObject private var selfTest = RuntimeSelfTest.shared

    var body: some View {
        ZStack(alignment: .topLeading) {
            MetalView(stats: stats)
                .ignoresSafeArea()

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
                Button(selfTest.running ? "Runtime self-test running…" : "Run runtime self-test") {
                    selfTest.run()
                }
                .disabled(selfTest.running)
                .buttonStyle(.borderedProminent)
                Button("Run ReXGlue window test (15 s, fresh launch)") {
                    selfTest.runWindowTest()
                }
                .disabled(selfTest.running)
                .buttonStyle(.bordered)
                if !selfTest.summary.isEmpty {
                    Text(selfTest.summary)
                }
                ForEach(selfTest.lines.suffix(28)) { line in
                    Text(line.text)
                        .font(.system(.caption, design: .monospaced))
                        .lineLimit(2)
                }
                Text("Log: Files › On My iPad › NFS MW Shell › shell-log.txt")
                    .font(.caption2)
            }
            .font(.system(.body, design: .monospaced))
            .foregroundStyle(.white)
            .padding(14)
            .background(Color.black.opacity(0.55), in: RoundedRectangle(cornerRadius: 10))
            .padding(24)
        }
    }
}
