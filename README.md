# 易来 Codex 切换器

面向 Codex CLI 与 Codex 桌面版的一键 API 配置工具。填写易来 API Key 后，可以在易来 API 与 OpenAI 官方配置之间安全切换。

## 下载

- Windows 10/11 x64：从 [Releases](https://github.com/kingduoyu/yilai-codex-switcher/releases/latest) 下载 v3.2.0 `YilaiCodexSwitcher.exe`，无需安装运行库。
- macOS 13+：下载 v3.2.1 `YilaiCodexSwitcher-macOS-universal.dmg`，同时支持 Intel 与 Apple Silicon。

当前 Windows 版本未进行代码签名，macOS 版本使用 ad-hoc 签名且未公证。首次打开若被系统拦截，请查看 Release 中的平台说明。

## 使用

1. 完全退出 Codex。
2. 打开易来 Codex 切换器。
3. 粘贴完整 API Key，不要添加 `Bearer` 或引号。
4. 点击“切换到易来 API”。
5. 重新打开 Codex。

需要切回官方时，完全退出 Codex，点击“切换回官方”，再重新打开。切换器会删除第三方 provider 选择和定义、保留插件等通用设置，并恢复受保护的 OpenAI 官方登录；不会恢复切换前可能存在的第三方配置。Codex 的 `model_provider` 默认值是内置 `openai`，见 [OpenAI Codex 配置参考](https://developers.openai.com/codex/config-reference/)。

## Windows v3.2.0 / macOS v3.2.1

易来模式固定显示 Sol (`gpt-5.6-sol`)、Terra (`gpt-5.6-terra`) 和 6 (`gpt-6-astra`)。旧用户完全退出 Codex 与 CC-Switch，重新用新版点击“切换到易来 API”即可覆盖旧模型目录配置，无需删除 `.codex` 或先切回官方；原登录备份、插件、会话和旧缓存保留。重开 Codex 后生效，切回官方会解除目录限制。模型清单固定，不会自动跟随服务器后续增删。

已用 Codex app-server 0.153.0 验证。过旧 Codex（例如不认识 `max` 推理等级的 0.130.0-alpha.5）需先升级，旧配置器配置迁移与旧 Codex 运行时兼容性是两回事。

## 构建

Windows 需要 Windows 10/11 和 LLVM-MinGW UCRT：

```powershell
winget install --id MartinStorsjo.LLVM-MinGW.UCRT --exact
pwsh -File Windows/build.ps1
```

macOS 需要 macOS 13+、Xcode Command Line Tools 和 Swift 5.9+：

```bash
bash build-macos.sh
```

两个实现都会运行隔离配置自测，不会操作构建机器的真实 Codex 配置。本仓库不包含 API Key、用户配置或认证文件。
