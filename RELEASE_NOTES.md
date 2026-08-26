# 易来 Codex 切换器 Release Notes

## v3.1.0（待发布）

- “切换回官方”不再原样恢复切换前的 `config.toml`，避免切回另一个第三方 provider。
- 官方模式会移除顶层 `model_provider` 和所有 `[model_providers.*]` 定义，写入 CC-Switch OpenAI Official 当前使用的 `gpt-5.6-terra`。
- 桌面设置、插件、通知、功能开关和项目信任等通用配置继续保留。
- 没有本工具备份清单时也能直接切换为官方配置；已有官方登录仍按原有安全规则恢复。
- 当前模式明确区分“OpenAI 官方”“易来 API”“其他第三方配置”和“尚未配置”。
- Windows 与 macOS 隔离自测新增“第三方配置 -> 易来 -> OpenAI 官方”和无备份直切官方覆盖。
- Windows 候选 SHA-256：`CA79AD3482C0F7FCD3AA9C02A158758E62E6C46992034AA214D8457238ECD1B8`；macOS 3.1.0 制品尚未构建。

## v3.0.1

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
- DMG SHA-256：`83EE814DC054EBC696AFEE23D1BA33DCCEE8822EE1907675D56FCEDD6DFC48F6`
- ZIP SHA-256：`A28CA1A12CA07D4BD24C5A0976ACF7508F3A37D5CEB2DBB5FDBDCDCCD5528951`
- 当前使用 ad-hoc 签名；首次打开若被 Gatekeeper 拦截，请右键应用并选择“打开”。

推荐 Windows 用户下载 `.exe`，macOS 用户下载 `.dmg`；`.zip` 保留为 macOS 备用包。
