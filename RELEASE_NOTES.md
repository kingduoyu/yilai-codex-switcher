# 易来 Codex 切换器 v3.0.1

## macOS 修复

- 恢复标准 macOS 编辑菜单及 `Command-V` 粘贴，并在 Key 输入框提供独立粘贴按钮。
- 配置完成前强制校验 `gpt-5.6-sol`、易来 provider、API Key 和生图请求头，校验失败会恢复原配置并弹窗说明。
- 自动保留重复切换时新生成或遗留的 `auth.json`，避免登录文件冲突导致配置被静默回滚。
- 隔离自测新增 Sol 模型写入和重复切换登录保护检查。

## Windows

- Windows 10/11 x64 原生单文件版本，无需安装 .NET、Go 或 VC++ 运行库。
- SHA-256：`7474DCFC552884CAB39BE9F172F762DB273AF5545D05CB4857ECB0F5A13D2D49`
- 当前未进行代码签名；若 SmartScreen 拦截，请确认下载来源后选择“更多信息”并继续运行。

## macOS

- 原生 SwiftUI/AppKit 通用版本，同时支持 Intel `x86_64` 与 Apple Silicon `arm64`。
- 当前使用 ad-hoc 签名；首次打开若被 Gatekeeper 拦截，请右键应用并选择“打开”。

推荐 Windows 用户下载 `.exe`，macOS 用户下载 `.dmg`；`.zip` 保留为 macOS 备用包。
