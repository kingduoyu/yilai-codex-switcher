# v3.3.18 发布清单

## 变更

- 新增内置模型 `gpt-6-sol`。
- 保留 `gpt-5.6-sol`、`gpt-5.6-terra`、`gpt-6-astra`。
- 更新 Windows/macOS 共享模型目录、配置白名单和运行时模型枚举测试。
- 保留候选中的 API Key 本地记忆功能。

## 本机验证

- Windows build: `Windows/build.ps1 -OutputDirectory ../dist/windows-v3.3.18`
- Windows self-test: passed
- Runtime integration test: pending final Codex executable path
- Source tree: candidate built from local working tree on 2026-09-23
