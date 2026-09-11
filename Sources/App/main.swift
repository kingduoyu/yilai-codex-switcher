import AppKit
import SwiftUI

final class Controller: ObservableObject {
    @Published var key = ""
    @Published var reveal = false
    @Published var busy = false
    @Published var message = "准备就绪。默认启用生图不会改写模型目录或清理登录。"
    @Published var mode = ""
    let service = PlatformService()
    init() { mode = service.mode() }
    func execute(_ operation: Operation) {
        guard !busy else { return }
        if operation == .sync || operation == .undo || operation == .cleanup {
            let dialog = NSAlert(); dialog.messageText = operation == .sync ? "同步全部本地历史" : operation == .undo ? "撤销上次同步" : "清理旧登录"
            dialog.informativeText = operation == .sync ? "先自动备份，再统一本机已有会话的分类；不改变登录和消息内容。\n\n若使用 CCS 切换官方，请同时开启 CCS 的“统一 Codex 会话历史”。" : operation == .undo ? "按上次备份还原会话归属。保留同步后新增的消息和会话；已被其他工具改过的连接配置不覆盖。" : "仅用于易来连接的旧登录干扰。旧登录和本工具旧备份会移入废纸篓，旧固定模型目录会解除引用。"
            dialog.addButton(withTitle: "取消"); dialog.addButton(withTitle: "继续")
            guard dialog.runModal() == .alertSecondButtonReturn else { return }
        }
        busy = true; message = "正在处理，请勿启动 Codex 或 CCS。历史较多时请等待完成。"
        let token = key
        DispatchQueue.global(qos: .userInitiated).async { [self] in
            let outcome: Result<String, Error> = Result { try service.run(operation, key: token) }
            DispatchQueue.main.async { [self] in
                busy = false
                switch outcome { case .success(let result): message = result; if operation == .configure { key = "" }; case .failure(let error): message = error.localizedDescription }
                mode = service.mode()
            }
        }
    }
}
struct Content: View {
    @ObservedObject var model: Controller
    var body: some View {
        VStack(alignment: .leading, spacing: 18) {
            Text("易来 Codex 配置器").font(.system(size: 29, weight: .semibold))
            Text("CCS 配置增强 · 本地历史统一     v3.3.0").foregroundStyle(.secondary)
            Text("当前连接：\(model.mode)").font(.headline).padding(.vertical, 4)
            Button { model.execute(.images) } label: {
                Text("启用生图 · 保留现有模型和登录").font(.system(size: 18, weight: .semibold)).frame(maxWidth: .infinity).frame(height: 48)
            }.buttonStyle(.borderedProminent)
            Text("易来 API Key（仅配置连接时填写；启用生图无需填写）").padding(.top, 8)
            HStack(spacing: 12) {
                Group { if model.reveal { TextField("", text: $model.key) } else { SecureField("", text: $model.key) } }.textFieldStyle(.roundedBorder)
                Toggle("显示", isOn: $model.reveal).toggleStyle(.checkbox)
                Button("配置易来连接") { model.execute(.configure) }.disabled(model.key.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty)
            }
            HStack(spacing: 16) {
                Button("同步全部本地历史") { model.execute(.sync) }.frame(maxWidth: .infinity)
                Button("撤销上次同步") { model.execute(.undo) }.frame(maxWidth: .infinity)
                Button("清理旧登录") { model.execute(.cleanup) }.frame(maxWidth: .infinity)
            }.controlSize(.large).padding(.vertical, 10)
            Text("操作前完全退出 Codex 和 CC-Switch。切回官方请使用 CCS。")
            Text("历史同步只处理本机已有记录，自动备份；不会下载其他账号的云端历史。").font(.system(size: 13)).foregroundStyle(.secondary)
            ScrollView { Text(model.message).textSelection(.enabled).frame(maxWidth: .infinity, alignment: .leading).padding(12) }
                .frame(height: 108).background(Color.white.opacity(0.75)).clipShape(RoundedRectangle(cornerRadius: 9))
            Spacer(minLength: 0)
        }
        .padding(32).frame(width: 864, height: 624).background(Color(red: 0.973, green: 0.98, blue: 0.992))
        .disabled(model.busy)
        .preferredColorScheme(.light)
    }
}
final class Delegate: NSObject, NSApplicationDelegate, NSWindowDelegate {
    var window: NSWindow!
    let controller = Controller()
    func applicationDidFinishLaunching(_ notification: Notification) {
        window = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 864, height: 624), styleMask: [.titled, .closable, .miniaturizable], backing: .buffered, defer: false)
        window.title = "易来 Codex 配置器 v3.3.0"; window.delegate = self; window.contentView = NSHostingView(rootView: Content(model: controller)); window.center(); window.makeKeyAndOrderFront(nil)
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
    do { try selfTest(); print("PASS: configuration, history migration/undo, and macOS cleanup/rollback"); exit(0) }
    catch { fputs("\(error.localizedDescription)\n", stderr); exit(1) }
}
let application = NSApplication.shared
let delegate = Delegate()
application.delegate = delegate
application.setActivationPolicy(.regular)
application.run()
