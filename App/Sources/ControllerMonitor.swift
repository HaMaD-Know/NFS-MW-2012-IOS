import Foundation
import GameController

/// Tracks connected game controllers and the most recent input. Version 1 of the port is
/// controller-only, so this is the input path that has to work on the device.
final class ControllerMonitor: ObservableObject {
    struct Info: Identifiable {
        let id: ObjectIdentifier
        let name: String
        let profile: String
    }

    @Published private(set) var controllers: [Info] = []
    @Published private(set) var lastInput: String = "none"

    private var observers: [NSObjectProtocol] = []

    init() {
        let center = NotificationCenter.default
        observers.append(center.addObserver(forName: .GCControllerDidConnect, object: nil, queue: .main) { [weak self] note in
            guard let self else { return }
            if let controller = note.object as? GCController {
                ShellLog.shared.write("Controller connected: \(controller.vendorName ?? "unknown")")
                self.attach(controller)
            }
            self.refresh()
        })
        observers.append(center.addObserver(forName: .GCControllerDidDisconnect, object: nil, queue: .main) { [weak self] note in
            let name = (note.object as? GCController)?.vendorName ?? "unknown"
            ShellLog.shared.write("Controller disconnected: \(name)")
            self?.refresh()
        })
        for controller in GCController.controllers() {
            attach(controller)
        }
        refresh()
    }

    deinit {
        for observer in observers {
            NotificationCenter.default.removeObserver(observer)
        }
    }

    private func attach(_ controller: GCController) {
        controller.extendedGamepad?.valueChangedHandler = { [weak self] _, element in
            let text = ControllerMonitor.describe(element)
            DispatchQueue.main.async {
                self?.lastInput = text
            }
        }
    }

    private static func describe(_ element: GCControllerElement) -> String {
        if let button = element as? GCControllerButtonInput {
            return "\(button.localizedName ?? "button") \(button.isPressed ? "pressed" : "released") (\(String(format: "%.2f", button.value)))"
        }
        if let pad = element as? GCControllerDirectionPad {
            return String(format: "%@ x %.2f y %.2f", pad.localizedName ?? "stick", pad.xAxis.value, pad.yAxis.value)
        }
        return element.localizedName ?? "input"
    }

    private func refresh() {
        controllers = GCController.controllers().map { controller in
            let profile: String
            if controller.extendedGamepad != nil {
                profile = "extended gamepad"
            } else if controller.microGamepad != nil {
                profile = "micro gamepad"
            } else {
                profile = "other"
            }
            return Info(id: ObjectIdentifier(controller),
                        name: controller.vendorName ?? "Unknown controller",
                        profile: profile)
        }
    }
}
