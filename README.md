# 易来 Codex 切换器 for macOS

macOS 13+ 原生 SwiftUI/AppKit 版本，支持 Intel 与 Apple Silicon。

## 下载

从 [Releases](https://github.com/kingduoyu/yilai-codex-switcher-macos/releases/latest) 下载 `YilaiCodexSwitcher-macOS-universal.zip`，解压后打开 `易来 Codex 切换器.app`。

当前版本使用 ad-hoc 签名，没有 Apple Developer ID 公证。首次打开若被 Gatekeeper 拦截，请右键应用并选择“打开”。

## 构建

在安装了 Xcode Command Line Tools 的 Mac 上运行：

```bash
bash build-macos.sh
```

产物：

```text
publish/mac-universal/易来 Codex 切换器.app
publish/mac-universal/YilaiCodexSwitcher-macOS-universal.zip
```

构建脚本会创建通用二进制、应用图标和 ad-hoc 签名，并运行隔离配置自测。正式公开分发建议另行使用 Apple Developer ID 签名与公证。

本仓库不包含 API Key、用户 Codex 配置或认证文件。
