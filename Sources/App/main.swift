import AppKit
import SwiftUI

final class Controller: ObservableObject {
    @Published var key = ""
    @Published var reveal = false
    @Published var busy = false
    @Published var failed = false
    @Published var message = "准备就绪。填写 API Key，一键启用易来 API 和生图。"
    @Published var mode = ""
    let service = PlatformService()

    init() { mode = service.mode() }

    func showLogs() {
        if !NSWorkspace.shared.open(service.logsDirectory) {
            message = "日志目录暂时无法打开。可能尚未生成日志，或目录没有访问权限。"
        }
    }

    func execute(_ operation: Operation) {
        guard !busy else { return }
        busy = true
        failed = false
        message = operation == .cleanup ? "正在重置配置…" : "正在配置易来 API 并启用生图，请稍候…"
        let token = key
        DispatchQueue.global(qos: .userInitiated).async { [self] in
            let outcome: Result<String, Error> = Result { try service.run(operation, key: token) }
            DispatchQueue.main.async { [self] in
                busy = false
                switch outcome {
                case .success(let result):
                    message = result
                    if operation == .configure { key = "" }
                case .failure(let error):
                    failed = true
                    message = error.localizedDescription
                }
                mode = service.mode()
            }
        }
    }
}

private struct SwitchButtonStyle: ButtonStyle {
    @Environment(\.isEnabled) private var enabled
    private let blue = Color(red: 37 / 255, green: 99 / 255, blue: 235 / 255)

    func makeBody(configuration: Configuration) -> some View {
        configuration.label
            .font(.system(size: 16, weight: .semibold))
            .frame(maxWidth: .infinity)
            .frame(height: 48)
            .foregroundStyle(Color.white)
            .background(blue)
            .clipShape(RoundedRectangle(cornerRadius: 10))
            .opacity(enabled ? (configuration.isPressed ? 0.8 : 1) : 0.55)
    }
}

struct Content: View {
    @ObservedObject var model: Controller

    var body: some View {
        VStack(alignment: .leading, spacing: 22) {
            HStack(alignment: .center) {
                VStack(alignment: .leading, spacing: 7) {
                    Text("易来 Codex").font(.system(size: 28, weight: .semibold))
                    Text("一键连接，继续创作。")
                        .font(.system(size: 14))
                        .foregroundStyle(.secondary)
                }
                Spacer()
                HStack(spacing: 7) {
                    Circle().fill(Color(red: 37 / 255, green: 99 / 255, blue: 235 / 255)).frame(width: 6, height: 6)
                    Text(model.mode).font(.system(size: 12, weight: .medium)).lineLimit(1)
                }
                .padding(.horizontal, 12)
                .padding(.vertical, 9)
                .background(Color.white)
                .clipShape(Capsule())
                .accessibilityLabel("当前连接：\(model.mode)")
            }

            VStack(alignment: .leading, spacing: 20) {
                VStack(alignment: .leading, spacing: 10) {
                    Text("易来 API Key").font(.system(size: 14, weight: .medium))
                    HStack(spacing: 12) {
                        Group {
                            if model.reveal {
                                TextField("粘贴你的 API Key", text: $model.key)
                            } else {
                                SecureField("粘贴你的 API Key", text: $model.key)
                            }
                        }
                        .textFieldStyle(.plain)
                        .font(.system(size: 14))
                        .accessibilityLabel("易来 API Key")
                        Button { model.reveal.toggle() } label: {
                            Image(systemName: model.reveal ? "eye.slash" : "eye")
                                .font(.system(size: 14))
                                .foregroundStyle(.secondary)
                                .frame(width: 24, height: 24)
                        }
                        .buttonStyle(.plain)
                        .accessibilityLabel(model.reveal ? "隐藏 API Key" : "显示 API Key")
                    }
                    .padding(.horizontal, 13)
                    .frame(height: 46)
                    .background(Color(red: 0.98, green: 0.985, blue: 0.993))
                    .clipShape(RoundedRectangle(cornerRadius: 9))
                    .overlay(RoundedRectangle(cornerRadius: 9).stroke(Color(red: 0.85, green: 0.88, blue: 0.92), lineWidth: 1))
                }

                Button("配置易来 API · 启用生图") { model.execute(.configure) }
                    .buttonStyle(SwitchButtonStyle())

                Label("自动启用生图 · 切回官方请使用 CCS", systemImage: "sparkles")
                    .font(.system(size: 12))
                    .foregroundStyle(.secondary)
            }
            .padding(24)
            .background(Color.white)
            .clipShape(RoundedRectangle(cornerRadius: 16))
            .overlay(RoundedRectangle(cornerRadius: 16).stroke(Color(red: 0.92, green: 0.93, blue: 0.96), lineWidth: 1))

            HStack(alignment: .top, spacing: 9) {
                if model.busy {
                    ProgressView().controlSize(.small).padding(.top, 1)
                } else {
                    Image(systemName: model.failed ? "exclamationmark.circle" : "checkmark.circle")
                        .foregroundStyle(model.failed ? Color.red : Color.secondary)
                        .padding(.top, 1)
                }
                ScrollView {
                    Text(model.message)
                        .font(.system(size: 13))
                        .foregroundStyle(model.failed ? Color.red : Color.secondary)
                        .textSelection(.enabled)
                        .frame(maxWidth: .infinity, alignment: .leading)
                }
                .frame(height: 58)
            }

            Spacer(minLength: 0)
            HStack {
                Text("操作前退出 Codex 和 CC-Switch")
                    .font(.system(size: 12))
                    .foregroundStyle(.secondary)
                Spacer()
                if model.failed {
                    Button("查看日志") { model.showLogs() }
                        .buttonStyle(.plain)
                        .font(.system(size: 12))
                        .foregroundStyle(.secondary)
                        .padding(.trailing, 12)
                }
                Button("重置配置") { model.execute(.cleanup) }
                    .buttonStyle(.plain)
                    .font(.system(size: 12))
                    .foregroundStyle(.secondary)
                    .help("仅在切换无效时使用。旧配置会改名保留，登录与历史不变。")
            }
        }
        .padding(28)
        .frame(width: 760, height: 520)
        .background(Color(red: 245 / 255, green: 247 / 255, blue: 251 / 255))
        .disabled(model.busy)
        .preferredColorScheme(.light)
    }
}
final class Delegate: NSObject, NSApplicationDelegate, NSWindowDelegate {
    var window: NSWindow!
    let controller = Controller()
    func applicationDidFinishLaunching(_ notification: Notification) {
        window = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 760, height: 520), styleMask: [.titled, .closable, .miniaturizable], backing: .buffered, defer: false)
        window.title = "易来 Codex 配置器 v3.3.6"; window.delegate = self; window.contentView = NSHostingView(rootView: Content(model: controller)); window.center(); window.makeKeyAndOrderFront(nil)
        NSApplication.shared.activate(ignoringOtherApps: true)
        if let index = CommandLine.arguments.firstIndex(of: "--screenshot"), CommandLine.arguments.count > index + 1 {
            DispatchQueue.main.asyncAfter(deadline: .now() + 1) { [self] in
                guard let view = window.contentView, let bitmap = view.bitmapImageRepForCachingDisplay(in: view.bounds) else { exit(1) }
                view.cacheDisplay(in: view.bounds, to: bitmap)
                do { guard let png = bitmap.representation(using: .png, properties: [:]) else { exit(1) }; try png.write(to: URL(fileURLWithPath: CommandLine.arguments[index+1])); exit(0) } catch { exit(1) }
            }
        }
    }
    func applicationShouldTerminate(_ sender: NSApplication) -> NSApplication.TerminateReply {
        controller.busy ? .terminateCancel : .terminateNow
    }
    func windowShouldClose(_ sender: NSWindow) -> Bool { !controller.busy }
    func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool { true }
}
if CommandLine.arguments.contains("--self-test") {
    do { try selfTest(); print("PASS: API configuration, macOS reset/rollback, and unrelated data preservation"); exit(0) }
    catch { fputs("\(error.localizedDescription)\n", stderr); exit(1) }
}
let application = NSApplication.shared
let delegate = Delegate()
application.delegate = delegate
application.setActivationPolicy(.regular)
application.run()
