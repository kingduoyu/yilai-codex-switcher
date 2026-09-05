# 易来 Codex 切换器 Release Notes

## v3.2.0（仅升级 Windows）

- Windows 写入专用 `model_catalog_json`，可见模型固定为 `gpt-5.6-sol`、`gpt-5.6-terra`、`gpt-6-astra`。
- 老用户再次切换时覆盖旧模型目录路径与重复配置项，保留首次登录备份、插件、会话及旧缓存，不需要先切回官方。
- 写入失败时回滚本次操作前的配置与模型目录；切回官方后解除目录限制。
- Windows 在 Codex app-server 0.153.0 下通过真实 `model/list` 测试，含 Luna 的旧缓存不再影响可见列表。
- 目录保留新模型的完整元数据；不兼容这些字段的旧 Codex 需先升级。macOS 本次不升级，附件沿用 v3.1.1 原 DMG/ZIP，保持原下载入口可用。
- Windows EXE SHA-256：`55F6E3954185BBB391D11FF89C5810B15E24A1E7B0CAAFE1C819ABA34ECE692A`。
- 发布范围和既有验收记录见 [RELEASE-MANIFEST](releases/v3.2.0/RELEASE-MANIFEST.md)。

## v3.1.1

- macOS 改用系统原生关闭与最小化按钮，移除会吞掉输入点击的全窗口拖动区域。
- 补全标准“编辑”菜单并修复 API Key 输入框焦点，支持直接点击输入和 `Command+V` 粘贴。
- 易来模式新增 `cli_auth_credentials_store = "file"`，避免 CC-Switch 混合登录保存在 macOS Keychain 的 Free 账号继续关闭生图能力。
- 易来模式会明确写入 `[features] image_generation = true`，兼容曾手动关闭生图功能的旧配置。
- 切换完成前校验真实 `~/.codex/config.toml`、纯 API 凭据模式及 `auth.json` 停用状态，失败时回滚并显示原因。
- 切回官方时从备份恢复原来的 Keychain/File 凭据存储设置，同时继续移除第三方 provider。
- macOS DMG SHA-256：`FF6EBB289347A8BF99E62610F39360BA896B051E401FBC32ED373B79EEA93AF9`。
- macOS ZIP SHA-256：`314C1A05FF9BC68C11AED47390DDBCEA455BF4647D65ECA9003E2A472A57B9DB`。

## v3.1.0

- “切换回官方”不再原样恢复切换前的 `config.toml`，避免切回另一个第三方 provider。
- 官方模式会移除顶层 `model_provider` 和所有 `[model_providers.*]` 定义，写入 CC-Switch OpenAI Official 当前使用的 `gpt-5.6-terra`。
- 桌面设置、插件、通知、功能开关和项目信任等通用配置继续保留。
- 没有本工具备份清单时也能直接切换为官方配置；已有官方登录仍按原有安全规则恢复。
- 当前模式明确区分“OpenAI 官方”“易来 API”“其他第三方配置”和“尚未配置”。
- Windows 与 macOS 隔离自测新增“第三方配置 -> 易来 -> OpenAI 官方”和无备份直切官方覆盖。
- Windows EXE SHA-256：`B7B3B9D49950488933672580120AA535456D21538E9B57AD75B46A48968777ED`。
- macOS DMG SHA-256：`E58A60C3FDD32C199534A5D81F8D5D8C50FC7A23DEE9EA83BBC9AE2DB576A58C`。
- macOS ZIP SHA-256：`BD04917FC8570C598B1862CFE5DF295FFAB7CAAD29569792F9B2C7E16D81A8F4`。

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
