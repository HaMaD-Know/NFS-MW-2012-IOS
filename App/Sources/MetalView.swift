import MetalKit
import QuartzCore
import SwiftUI

/// Published once per second, never per frame, so SwiftUI does not redraw every frame.
final class RenderStats: ObservableObject {
    @Published var deviceName = "not started"
    @Published var fps = 0.0
    @Published var frameMs = 0.0
    @Published var drawableSize = "-"
    @Published var memoryAvailableGB = 0.0
    @Published var shaderCompileMs = 0.0
    @Published var error: String?
}

struct MetalView: UIViewRepresentable {
    let stats: RenderStats

    func makeCoordinator() -> Renderer {
        Renderer(stats: stats)
    }

    func makeUIView(context: Context) -> MTKView {
        let view = MTKView()
        view.colorPixelFormat = .bgra8Unorm
        view.preferredFramesPerSecond = 120   // ProMotion iPads; the measured rate is reported
        context.coordinator.attach(to: view)
        return view
    }

    func updateUIView(_ uiView: MTKView, context: Context) {}
}

/// Matches `struct Uniforms` in the shader source below.
private struct Uniforms {
    var angle: Float
    var aspect: Float
}

/// Compiled on the device at run time (MTLDevice.makeLibrary(source:)). This shows that Metal
/// shaders need no Mac: an offline .metallib would need Apple's Metal compiler.
private let shaderSource = """
#include <metal_stdlib>
using namespace metal;

struct Uniforms { float angle; float aspect; };
struct VertexOut { float4 position [[position]]; float4 color; };

vertex VertexOut shell_vertex(uint vid [[vertex_id]], constant Uniforms &u [[buffer(0)]]) {
    const float2 corners[3] = { float2(0.0, 0.6), float2(-0.52, -0.3), float2(0.52, -0.3) };
    const float4 colors[3]  = { float4(1.0, 0.35, 0.2, 1.0), float4(0.2, 0.9, 0.45, 1.0), float4(0.25, 0.45, 1.0, 1.0) };
    float s = sin(u.angle);
    float c = cos(u.angle);
    float2 p = corners[vid];
    float2 r = float2(p.x * c - p.y * s, p.x * s + p.y * c);
    VertexOut out;
    out.position = float4(r.x * u.aspect, r.y, 0.0, 1.0);
    out.color = colors[vid];
    return out;
}

fragment float4 shell_fragment(VertexOut in [[stage_in]]) {
    return in.color;
}
"""

final class Renderer: NSObject, MTKViewDelegate {
    private let stats: RenderStats
    private var queue: MTLCommandQueue?
    private var pipeline: MTLRenderPipelineState?
    private let startTime = CACurrentMediaTime()
    private var windowStart = CACurrentMediaTime()
    private var framesInWindow = 0

    init(stats: RenderStats) {
        self.stats = stats
        super.init()
    }

    func attach(to view: MTKView) {
        guard let device = MTLCreateSystemDefaultDevice() else {
            report("Metal is not available on this device")
            return
        }
        view.device = device
        queue = device.makeCommandQueue()
        stats.deviceName = device.name
        ShellLog.shared.write("Metal device: \(device.name)")

        let compileStart = CACurrentMediaTime()
        do {
            let library = try device.makeLibrary(source: shaderSource, options: nil)
            let descriptor = MTLRenderPipelineDescriptor()
            descriptor.vertexFunction = library.makeFunction(name: "shell_vertex")
            descriptor.fragmentFunction = library.makeFunction(name: "shell_fragment")
            descriptor.colorAttachments[0].pixelFormat = view.colorPixelFormat
            pipeline = try device.makeRenderPipelineState(descriptor: descriptor)
            stats.shaderCompileMs = (CACurrentMediaTime() - compileStart) * 1000
            ShellLog.shared.write(String(format: "Runtime shader compile and pipeline: %.1f ms", stats.shaderCompileMs))
        } catch {
            report("Shader setup failed: \(error.localizedDescription)")
        }
        view.delegate = self
    }

    func mtkView(_ view: MTKView, drawableSizeWillChange size: CGSize) {
        stats.drawableSize = "\(Int(size.width)) x \(Int(size.height))"
        ShellLog.shared.write("Drawable size: \(stats.drawableSize)")
    }

    func draw(in view: MTKView) {
        guard let queue,
              let pipeline,
              let pass = view.currentRenderPassDescriptor,
              let drawable = view.currentDrawable,
              let commands = queue.makeCommandBuffer() else { return }

        let t = CACurrentMediaTime() - startTime
        pass.colorAttachments[0].loadAction = .clear
        pass.colorAttachments[0].clearColor = MTLClearColor(red: 0.04, green: 0.05 + 0.03 * sin(t), blue: 0.09, alpha: 1)

        guard let encoder = commands.makeRenderCommandEncoder(descriptor: pass) else { return }
        let size = view.drawableSize
        var uniforms = Uniforms(angle: Float(t), aspect: Float(size.height / max(size.width, 1)))
        encoder.setRenderPipelineState(pipeline)
        encoder.setVertexBytes(&uniforms, length: MemoryLayout<Uniforms>.stride, index: 0)
        encoder.drawPrimitives(type: .triangle, vertexStart: 0, vertexCount: 3)
        encoder.endEncoding()
        commands.present(drawable)
        commands.commit()

        countFrame()
    }

    private func countFrame() {
        framesInWindow += 1
        let now = CACurrentMediaTime()
        let elapsed = now - windowStart
        guard elapsed >= 1 else { return }
        stats.fps = Double(framesInWindow) / elapsed
        stats.frameMs = elapsed * 1000 / Double(framesInWindow)
        stats.memoryAvailableGB = DeviceInfo.availableMemoryGB
        framesInWindow = 0
        windowStart = now
    }

    private func report(_ message: String) {
        stats.error = message
        ShellLog.shared.write("ERROR: \(message)")
    }
}
