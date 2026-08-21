import AppKit
import Darwin
import SwiftUI

private let productName = "易来 Codex 切换器"

final class SwitcherModel: ObservableObject {
  @Published var key = ""
  @Published var showKey = false
  @Published var mode: CodexMode = .officialOrOther
  @Published var message = "切换前请完全退出 Codex，完成后重新打开。"
  @Published var messageIsError = false
  @Published var messageIsSuccess = false

  private let service = CodexConfigurationService()

  init() { refresh() }

  func switchToYilai() {
    do {
      try service.switchToYilai(key: key)
      key = ""
      message = "已切换到易来 API，请重新打开 Codex。"
      messageIsError = false
      messageIsSuccess = true
      refreshMode()
    } catch {
      show(error)
    }
  }

  func restore() {
    do {
      try service.switchToOfficial()
      message = "已恢复原有配置，请重新打开 Codex。"
      messageIsError = false
      messageIsSuccess = true
      refreshMode()
    } catch {
      show(error)
    }
  }

  private func refresh() {
    do { mode = try service.mode() } catch { show(error) }
  }

  private func refreshMode() {
    if let current = try? service.mode() { mode = current }
  }

  private func show(_ error: Error) {
    message = error.localizedDescription
    messageIsError = true
    messageIsSuccess = false
  }
}

struct ContentView: View {
  @StateObject private var model = SwitcherModel()
  @FocusState private var keyFocused: Bool

  var body: some View {
    ZStack {
      Color(red: 0.91, green: 0.95, blue: 0.98)
      if let background = resourceImage("liquid-glass-background") {
        Image(nsImage: background)
          .resizable()
          .scaledToFill()
          .opacity(0.62)
      }
      Color.white.opacity(0.48)

      VStack(spacing: 0) {
        titleBar
        mainContent
        footer
      }
    }
    .frame(width: 960, height: 650)
    .clipShape(RoundedRectangle(cornerRadius: 18, style: .continuous))
    .overlay(
      RoundedRectangle(cornerRadius: 18, style: .continuous).stroke(
        Color.white.opacity(0.92), lineWidth: 1))
  }

  private var titleBar: some View {
    HStack(spacing: 15) {
      macWindowButton(color: Color(red: 1.0, green: 0.38, blue: 0.34), symbol: "xmark") {
        NSApp.keyWindow?.close()
      }
      macWindowButton(color: Color(red: 1.0, green: 0.74, blue: 0.24), symbol: "minus") {
        NSApp.keyWindow?.miniaturize(nil)
      }

      if let logo = resourceImage("yilai-switcher-logo") {
        Image(nsImage: logo).resizable().scaledToFit().frame(width: 32, height: 32)
      }
      Text(productName)
        .font(.system(size: 18, weight: .semibold))
        .foregroundStyle(Color(hex: 0x17263A))
      Spacer()
    }
    .padding(.horizontal, 22)
    .frame(height: 66)
    .background(.white.opacity(0.72))
    .overlay(alignment: .bottom) { Rectangle().fill(.white.opacity(0.9)).frame(height: 1) }
  }

  private var mainContent: some View {
    VStack(alignment: .leading, spacing: 0) {
      HStack(spacing: 20) {
        if let logo = resourceImage("yilai-switcher-logo") {
          Image(nsImage: logo).resizable().scaledToFit().frame(width: 74, height: 74)
        }
        VStack(alignment: .leading, spacing: 4) {
          Text(productName)
            .font(.system(size: 32, weight: .bold))
            .foregroundStyle(Color(hex: 0x101A29))
          Text("仅适用于 Codex CLI 与 Codex 桌面版")
            .font(.system(size: 17))
            .foregroundStyle(Color(hex: 0x465C76))
        }
        Spacer()
      }
      .padding(.top, 48)
      .padding(.horizontal, 56)

      statusPanel
        .padding(.top, 28)
        .padding(.horizontal, 56)

      Text("易来 API Key")
        .font(.system(size: 16, weight: .semibold))
        .foregroundStyle(Color(hex: 0x142338))
        .padding(.top, 18)
        .padding(.horizontal, 56)

      keyField
        .padding(.top, 8)
        .padding(.horizontal, 56)

      HStack(spacing: 18) {
        Button(action: model.switchToYilai) {
          Text("切换到易来 API")
            .font(.system(size: 18, weight: .semibold))
            .frame(width: 305, height: 62)
            .foregroundStyle(.white)
            .background(
              model.key.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty
                ? Color(hex: 0xA9BED7) : Color(hex: 0x0868D5)
            )
            .clipShape(RoundedRectangle(cornerRadius: 10, style: .continuous))
            .shadow(color: Color(hex: 0x0759B0).opacity(0.22), radius: 8, y: 4)
        }
        .buttonStyle(.plain)
        .disabled(model.key.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty)

        Button(action: model.restore) {
          Label("切换回官方", systemImage: "arrow.triangle.2.circlepath")
            .font(.system(size: 18, weight: .semibold))
            .frame(width: 235, height: 62)
            .foregroundStyle(Color(hex: 0x1E3047))
            .background(.white.opacity(0.82))
            .clipShape(RoundedRectangle(cornerRadius: 10, style: .continuous))
            .overlay(RoundedRectangle(cornerRadius: 10).stroke(Color(hex: 0xA6BAD0), lineWidth: 1))
            .shadow(color: Color(hex: 0x607890).opacity(0.12), radius: 7, y: 3)
        }
        .buttonStyle(.plain)
        Spacer()
      }
      .padding(.top, 22)
      .padding(.horizontal, 56)

      Text("Key 仅写入你的 Codex 配置文件，不会上传到其他服务。")
        .font(.system(size: 12))
        .foregroundStyle(Color(hex: 0x5E738B))
        .padding(.top, 8)
        .padding(.horizontal, 56)
      Spacer()
    }
    .frame(height: 489)
  }

  private var statusPanel: some View {
    HStack(spacing: 0) {
      Circle()
        .fill(model.mode == .yilai ? Color(hex: 0x0877D9) : Color(hex: 0x119869))
        .frame(width: 14, height: 14)
        .padding(.leading, 28)
      Text("连接设置")
        .font(.system(size: 17))
        .foregroundStyle(Color(hex: 0x3E526B))
        .padding(.leading, 14)
      Rectangle().fill(Color(hex: 0xB8C8D9).opacity(0.85)).frame(width: 1, height: 38).padding(
        .leading, 34)
      Text("当前模式")
        .font(.system(size: 17))
        .foregroundStyle(Color(hex: 0x596E86))
        .padding(.leading, 30)
      Text(model.mode.label)
        .font(.system(size: 17, weight: .semibold))
        .foregroundStyle(Color(hex: 0x101A29))
        .padding(.leading, 28)
      Spacer()
    }
    .frame(height: 86)
    .background(.ultraThinMaterial)
    .clipShape(RoundedRectangle(cornerRadius: 13, style: .continuous))
    .overlay(RoundedRectangle(cornerRadius: 13).stroke(.white.opacity(0.9), lineWidth: 1))
    .shadow(color: Color(hex: 0x607890).opacity(0.16), radius: 8, y: 4)
  }

  private var keyField: some View {
    HStack(spacing: 12) {
      Group {
        if model.showKey {
          TextField("粘贴完整 Key，例如 sk-...", text: $model.key)
        } else {
          SecureField("粘贴完整 Key，例如 sk-...", text: $model.key)
        }
      }
      .textFieldStyle(.plain)
      .font(.system(size: 18))
      .focused($keyFocused)

      Button {
        model.showKey.toggle()
        keyFocused = true
      } label: {
        Image(systemName: model.showKey ? "eye.slash" : "eye")
          .font(.system(size: 19, weight: .medium))
          .foregroundStyle(Color(hex: 0x334A65))
          .frame(width: 44, height: 44)
      }
      .buttonStyle(.plain)
      .help(model.showKey ? "隐藏 Key" : "显示 Key")
    }
    .padding(.horizontal, 20)
    .frame(height: 70)
    .background(.white.opacity(0.88))
    .clipShape(RoundedRectangle(cornerRadius: 11, style: .continuous))
    .overlay(
      RoundedRectangle(cornerRadius: 11).stroke(
        keyFocused ? Color(hex: 0x0A67D8) : Color(hex: 0xA6BAD0), lineWidth: keyFocused ? 2 : 1)
    )
    .shadow(
      color: (keyFocused ? Color(hex: 0x0A67D8) : Color(hex: 0x607890)).opacity(0.18), radius: 8,
      y: 4)
  }

  private var footer: some View {
    HStack(spacing: 12) {
      Image(systemName: model.messageIsError ? "exclamationmark.shield" : "checkmark.shield")
        .font(.system(size: 22, weight: .medium))
      Text(model.message)
        .font(.system(size: 15))
      Spacer()
      Text("macOS 13+  ·  v3.0.0")
        .font(.system(size: 13))
        .foregroundStyle(Color(hex: 0x62778F))
    }
    .foregroundStyle(
      model.messageIsError
        ? Color(hex: 0xB3263E)
        : (model.messageIsSuccess ? Color(hex: 0x0D7F57) : Color(hex: 0x40566F))
    )
    .padding(.horizontal, 56)
    .frame(height: 95)
    .background(.white.opacity(0.76))
    .overlay(alignment: .top) { Rectangle().fill(.white.opacity(0.95)).frame(height: 1) }
  }

  private func macWindowButton(color: Color, symbol: String, action: @escaping () -> Void)
    -> some View
  {
    Button(action: action) {
      ZStack {
        Circle().fill(color).frame(width: 13, height: 13)
        Image(systemName: symbol).font(.system(size: 7, weight: .bold)).foregroundStyle(
          .black.opacity(0.55))
      }
      .frame(width: 18, height: 32)
    }
    .buttonStyle(.plain)
  }
}

private func resourceImage(_ name: String) -> NSImage? {
  guard let url = Bundle.main.url(forResource: name, withExtension: "png") else { return nil }
  return NSImage(contentsOf: url)
}

extension Color {
  fileprivate init(hex: UInt32) {
    self.init(
      red: Double((hex >> 16) & 0xFF) / 255,
      green: Double((hex >> 8) & 0xFF) / 255,
      blue: Double(hex & 0xFF) / 255
    )
  }
}

final class AppDelegate: NSObject, NSApplicationDelegate {
  private var window: NSWindow?

  func applicationDidFinishLaunching(_ notification: Notification) {
    NSApp.setActivationPolicy(.regular)
    let content = NSHostingView(rootView: ContentView())
    let window = NSWindow(
      contentRect: NSRect(x: 0, y: 0, width: 960, height: 650),
      styleMask: [.borderless, .closable, .miniaturizable],
      backing: .buffered,
      defer: false
    )
    window.contentView = content
    window.title = productName
    window.isOpaque = false
    window.backgroundColor = .clear
    window.hasShadow = true
    window.isMovableByWindowBackground = true
    window.minSize = NSSize(width: 960, height: 650)
    window.maxSize = NSSize(width: 960, height: 650)
    window.center()
    window.makeKeyAndOrderFront(nil)
    NSApp.activate(ignoringOtherApps: true)
    self.window = window
  }

  func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool { true }
}

if CommandLine.arguments.contains("--self-test") {
  do {
    try runSelfTest()
    exit(0)
  } catch {
    fputs("\(error.localizedDescription)\n", stderr)
    exit(1)
  }
}

let app = NSApplication.shared
let delegate = AppDelegate()
app.delegate = delegate
app.run()
