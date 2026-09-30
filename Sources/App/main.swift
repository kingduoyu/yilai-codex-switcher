import AppKit
import SwiftUI

final class Controller: ObservableObject {
    @Published var key = UserDefaults.standard.string(forKey: "yilai.apiKey") ?? ""
    @Published var reveal = false
    @Published var busy = false
    @Published var failed = false
    @Published var warning = false
    @Published var message = "准备就绪。"
    @Published var mode = ""
    @Published var models: [CatalogModel] = []
    @Published var inspectedModelID = ""
    @Published var checkingUpdate = false
    @Published var release: SoftwareRelease?
    @Published var updateMessage = "启动后检查更新"
    @Published var showReleaseNotes = false
    let service = PlatformService()
    private let updates = UpdateService()

    init() {
        mode = service.mode()
        refreshModels()
        if let result = UpdateInstaller.consumeResult() {
            message = result.message
            warning = result.warning
        }
    }

    var inspectedModel: CatalogModel? { models.first { $0.id == inspectedModelID } ?? models.first }
    var versionBadge: String {
        guard let release, release.available else { return "v\(UpdateProtocol.version)" }
        return "有新版本 \(release.version)"
    }

    private func refreshModels() {
        models = service.modelCatalog()
        if !models.contains(where: { $0.id == inspectedModelID }) { inspectedModelID = models.first?.id ?? "" }
    }

    func checkUpdates(silent: Bool = false) {
        guard !checkingUpdate, !busy else { return }
        checkingUpdate = true
        updateMessage = "正在检查软件更新…"
        Task { @MainActor [self] in
            defer { checkingUpdate = false }
            do {
                let result = try await updates.checkRelease()
                release = result
                updateMessage = result.available ? "有新版本 \(result.version)" : "已是最新版本"
                if !silent && !busy {
                    message = updateMessage
                    failed = false
                    warning = false
                }
            } catch {
                updateMessage = "软件更新检查失败：\(error.localizedDescription)"
                if !silent && !busy {
                    message = updateMessage
                    failed = true
                    warning = false
                }
            }
        }
    }

    func updateModels() {
        guard !busy else { return }
        busy = true
        failed = false
        warning = false
        message = "正在更新模型目录，请稍候…"
        Task { @MainActor [self] in
            defer { busy = false }
            do {
                let result = try await updates.updateCatalog(using: service)
                refreshModels()
                message = result.message
                warning = result.warning
            } catch {
                failed = true
                message = "模型更新失败：\(error.localizedDescription)"
            }
        }
    }

    func installUpdate() {
        guard !busy, let release, release.available else { return }
        busy = true
        failed = false
        warning = false
        showReleaseNotes = false
        message = "正在下载并校验软件更新；完成后将安装并重新启动…"
        Task { @MainActor [self] in
            do {
                let installation = try await updates.prepareInstallation(release)
                try await installation.launch()
                busy = false
                NSApplication.shared.terminate(nil)
            } catch {
                busy = false
                failed = true
                message = "软件更新失败：\(error.localizedDescription)"
            }
        }
    }

    func execute(_ operation: Operation) {
        guard !busy else { return }
        busy = true
        failed = false
        warning = false
        message = operation == .cleanup ? "正在重置配置…" : operation == .official ? "正在切换官方…" : "正在配置易来 API 并启用生图，请稍候…"
        let token = key
        if operation == .configure { UserDefaults.standard.set(token, forKey: "yilai.apiKey") }
        DispatchQueue.global(qos: .userInitiated).async { [self] in
            let outcome: Result<OperationOutcome, Error> = Result { try service.run(operation, key: token) }
            DispatchQueue.main.async { [self] in
                busy = false
                switch outcome {
                case .success(let result):
                    message = result.message
                    warning = result.warning
                case .failure(let error):
                    failed = true
                    message = error.localizedDescription
                }
                mode = service.mode()
                refreshModels()
            }
        }
    }
}

private struct SwitchButtonStyle: ButtonStyle {
    @Environment(\.isEnabled) private var enabled
    var primary = true
    private let blue = Color(red: 37 / 255, green: 99 / 255, blue: 235 / 255)

    func makeBody(configuration: Configuration) -> some View {
        configuration.label
            .font(.system(size: 16, weight: .semibold))
            .frame(maxWidth: .infinity)
            .frame(height: 48)
            .foregroundStyle(primary ? Color.white : Color.primary)
            .background(primary ? blue : Color.white)
            .clipShape(RoundedRectangle(cornerRadius: 8))
            .overlay(RoundedRectangle(cornerRadius: 8).stroke(primary ? blue : Color(red: 225 / 255, green: 231 / 255, blue: 240 / 255), lineWidth: 1))
            .opacity(enabled ? (configuration.isPressed ? 0.8 : 1) : 0.55)
    }
}

struct Content: View {
    @ObservedObject var model: Controller

    var body: some View {
        VStack(alignment: .leading, spacing: 24) {
            HStack(alignment: .center) {
                VStack(alignment: .leading, spacing: 8) {
                    Text("易来 Codex").font(.system(size: 24, weight: .semibold))
                    Text(model.mode)
                        .font(.system(size: 13))
                        .foregroundStyle(.secondary)
                        .lineLimit(1)
                        .accessibilityLabel("当前连接：\(model.mode)")
                }
                Spacer()
                HStack(spacing: 6) {
                    Circle().fill(model.release?.available == true ? Color.yellow : Color.clear).frame(width: 6, height: 6)
                    Menu {
                        Text("当前版本 v\(UpdateProtocol.version)")
                        Divider()
                        Button { model.checkUpdates() } label: { Label("检查更新", systemImage: "arrow.clockwise") }
                            .disabled(model.checkingUpdate)
                        Button { model.showReleaseNotes = true } label: { Label("更新说明", systemImage: "text.alignleft") }
                            .disabled(model.release == nil)
                        Button { model.installUpdate() } label: { Label("一键更新", systemImage: "arrow.down.circle") }
                            .disabled(model.release?.available != true)
                        Divider()
                        Text(model.updateMessage)
                    } label: {
                        Text(model.versionBadge)
                            .font(.system(size: 12, weight: .medium))
                            .lineLimit(1)
                    }
                    .menuStyle(.borderlessButton)
                    .help(model.updateMessage)
                    .accessibilityLabel("软件版本与更新")
                }
                .fixedSize()
            }

            VStack(alignment: .leading, spacing: 24) {
                VStack(alignment: .leading, spacing: 8) {
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
                    .clipShape(RoundedRectangle(cornerRadius: 8))
                    .overlay(RoundedRectangle(cornerRadius: 8).stroke(Color(red: 0.85, green: 0.88, blue: 0.92), lineWidth: 1))
                }

                VStack(alignment: .leading, spacing: 8) {
                    Text("模型目录（\(model.models.count)）").font(.system(size: 12, weight: .medium)).foregroundStyle(.secondary)
                    HStack(spacing: 12) {
                        Menu {
                            ForEach(model.models) { item in
                                Button(item.displayName) { model.inspectedModelID = item.id }
                            }
                        } label: {
                            Text(model.inspectedModel?.displayName ?? "模型目录")
                                .lineLimit(1)
                                .frame(maxWidth: .infinity, alignment: .leading)
                        }
                        .frame(maxWidth: .infinity)
                        .help("查看已安装的模型目录，不修改 Codex 模型选择。")
                        .accessibilityLabel("查看模型目录")
                        Button { model.updateModels() } label: {
                            Label("更新模型", systemImage: "arrow.clockwise")
                        }
                        .buttonStyle(.bordered)
                        .fixedSize()
                    }
                    .controlSize(.regular)
                    .font(.system(size: 13))
                    .frame(height: 30)
                }

                HStack(spacing: 12) {
                    Button("配置易来 API · 启用生图") { model.execute(.configure) }
                        .buttonStyle(SwitchButtonStyle())
                    Button("切换到官方") { model.execute(.official) }
                        .buttonStyle(SwitchButtonStyle(primary: false))
                }
            }
            .padding(24)
            .background(Color.white)
            .clipShape(RoundedRectangle(cornerRadius: 8))
            .overlay(RoundedRectangle(cornerRadius: 8).stroke(Color(red: 0.92, green: 0.93, blue: 0.96), lineWidth: 1))

            HStack(alignment: .top, spacing: 8) {
                if model.busy {
                    ProgressView().controlSize(.small).padding(.top, 1)
                } else {
                    Image(systemName: model.failed ? "exclamationmark.circle" : model.warning ? "exclamationmark.triangle" : "checkmark.circle")
                        .foregroundStyle(model.failed ? Color.red : model.warning ? Color.orange : Color.secondary)
                        .padding(.top, 1)
                }
                ScrollView {
                    Text(model.message)
                        .font(.system(size: 13))
                        .foregroundStyle(model.failed ? Color.red : model.warning ? Color.orange : Color.secondary)
                        .textSelection(.enabled)
                        .frame(maxWidth: .infinity, alignment: .leading)
                }
                .frame(height: 92)
            }

            Spacer(minLength: 0)
            HStack {
                Text("操作前退出 Codex 和 CC-Switch")
                    .font(.system(size: 12))
                    .foregroundStyle(.secondary)
                Spacer()
                Button("重置配置") { model.execute(.cleanup) }
                    .buttonStyle(.plain)
                    .font(.system(size: 12))
                    .foregroundStyle(.secondary)
                    .help("仅在切换无效时使用。旧配置会改名保留，登录与历史不变。")
            }
        }
        .padding(28)
        .frame(minWidth: 720, maxWidth: .infinity, minHeight: 700, maxHeight: .infinity)
        .background(Color(red: 245 / 255, green: 247 / 255, blue: 251 / 255))
        .disabled(model.busy)
        .preferredColorScheme(.light)
        .sheet(isPresented: $model.showReleaseNotes) {
            if let release = model.release {
                VStack(alignment: .leading, spacing: 18) {
                    HStack {
                        Text("更新说明 \(release.version)").font(.system(size: 18, weight: .semibold))
                        Spacer()
                        Button { model.showReleaseNotes = false } label: { Image(systemName: "xmark") }
                            .buttonStyle(.plain)
                            .help("关闭更新说明")
                    }
                    ScrollView {
                        Text(release.notes.isEmpty ? "此版本暂无更新说明。" : release.notes)
                            .font(.system(size: 13))
                            .textSelection(.enabled)
                            .frame(maxWidth: .infinity, alignment: .leading)
                    }
                    HStack {
                        Spacer()
                        Button { model.installUpdate() } label: { Label("一键更新", systemImage: "arrow.down.circle") }
                            .buttonStyle(.borderedProminent)
                            .disabled(!release.available || model.busy)
                    }
                }
                .padding(24)
                .frame(width: 540, height: 420)
            }
        }
    }
}
final class Delegate: NSObject, NSApplicationDelegate, NSWindowDelegate {
    var window: NSWindow!
    let controller = Controller()
    func applicationDidFinishLaunching(_ notification: Notification) {
        window = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 760, height: 700), styleMask: [.titled, .closable, .miniaturizable, .resizable], backing: .buffered, defer: false)
        window.contentMinSize = NSSize(width: 720, height: 700)
        window.title = "易来 Codex 配置器"; window.delegate = self; window.contentView = NSHostingView(rootView: Content(model: controller)); window.center(); window.makeKeyAndOrderFront(nil)
        NSApplication.shared.activate(ignoringOtherApps: true)
        if let index = CommandLine.arguments.firstIndex(of: "--screenshot"), CommandLine.arguments.count > index + 1 {
            if CommandLine.arguments.contains("--update-state") {
                controller.release = SoftwareRelease(available: true, version: "v3.4.2", notes: "", url: nil, sha256: nil, size: nil)
                controller.updateMessage = "有新版本 v3.4.2"
            }
            DispatchQueue.main.asyncAfter(deadline: .now() + 1) { [self] in
                guard let view = window.contentView, let bitmap = view.bitmapImageRepForCachingDisplay(in: view.bounds) else { exit(1) }
                view.cacheDisplay(in: view.bounds, to: bitmap)
                if CommandLine.arguments.contains("--update-state") {
                    var yellowPixels = 0
                    for y in 0..<bitmap.pixelsHigh {
                        for x in 0..<bitmap.pixelsWide {
                            if let color = bitmap.colorAt(x: x, y: y)?.usingColorSpace(.deviceRGB),
                               color.alphaComponent > 0.8, color.redComponent > 0.75,
                               color.greenComponent > 0.55, color.blueComponent < 0.4 {
                                yellowPixels += 1
                            }
                        }
                    }
                    guard yellowPixels >= 4 else { fputs("Update badge yellow dot was not rendered\n", stderr); exit(1) }
                }
                do { guard let png = bitmap.representation(using: .png, properties: [:]) else { exit(1) }; try png.write(to: URL(fileURLWithPath: CommandLine.arguments[index+1])); exit(0) } catch { exit(1) }
            }
        } else {
            controller.checkUpdates(silent: true)
        }
    }
    func applicationShouldTerminate(_ sender: NSApplication) -> NSApplication.TerminateReply {
        controller.busy ? .terminateCancel : .terminateNow
    }
    func windowShouldClose(_ sender: NSWindow) -> Bool { !controller.busy }
    func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool { true }
}
if CommandLine.arguments.count > 1 && CommandLine.arguments[1] == "--update-helper" {
    do { try UpdateInstaller.runHelper(Array(CommandLine.arguments.dropFirst(2))); exit(0) }
    catch { fputs("\(error.localizedDescription)\n", stderr); exit(1) }
}
if CommandLine.arguments.contains("--self-test") {
    do { try selfTest(); print("PASS: API configuration, macOS reset/rollback, update protocol/catalog, and unrelated data preservation"); exit(0) }
    catch { fputs("\(error.localizedDescription)\n", stderr); exit(1) }
}
let application = NSApplication.shared
let delegate = Delegate()
application.delegate = delegate
application.setActivationPolicy(.regular)
application.run()
