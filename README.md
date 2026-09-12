# 易来 Codex 配置器 v3.3.11

Windows / macOS 原生小工具：填写 API Key，一键配置易来 API 并启用生图。支持 API 与官方双向切换，切换时自动检查旧易来对话归属，无需额外按钮。

稳定下载地址：[Windows EXE](https://github.com/kingduoyu/yilai-codex-switcher/releases/latest/download/YilaiCodexSwitcher.exe) · [macOS DMG](https://github.com/kingduoyu/yilai-codex-switcher/releases/latest/download/YilaiCodexSwitcher-macOS-universal.dmg) · [macOS ZIP](https://github.com/kingduoyu/yilai-codex-switcher/releases/latest/download/YilaiCodexSwitcher-macOS-universal.zip)。也可以在 [GitHub Releases](https://github.com/kingduoyu/yilai-codex-switcher/releases/latest) 页面下载带版本号的附件。

## 使用

1. 完全退出 Codex 和 CC-Switch，包括后台进程。
2. 填写 API Key，点击 **配置易来 API · 启用生图**，完成后重新打开 Codex。
3. 旧易来对话检查自动随切换完成；只有 `yilai` 记录才修复，已是 `custom` 或其他来源的不改写。
4. **切换到官方** 使用官方认证路由，保留现有 auth.json；缺少官方登录时需要重新登录。

重置配置位于界面底部，仅将当前 config.toml 改名加唯一 disabled 后缀，使其失效。不弹确认、不删除原内容、不恢复停用文件、不动登录和历史。重置后重新配置 API。

## 配置与兼容

- 使用 CCS 兼容的 custom provider，供应商节点写入独立 bearer token，requires_openai_auth=false，启用生图并删除当前 CODEX_HOME/auth.json。
- 写入内置的 sol、terra、astra 三个模型和模型目录指向；保留已有合法模型选择，否则默认 sol。保留推理档位、MCP、权限等无关配置。配置未显式写权限时，沿用 Codex 桌面端已保存的完全访问模式，不降级为审批模式。
- 主按钮只执行本地配置写入，不启动 Codex、不检查跨文件优先级、不做运行时回读。写入错误保留日志并恢复本次改动。
- 同一 CODEX_HOME 的配置操作互斥。请勿在写入过程中启动 Codex/CCS。默认使用用户 .codex；设置 CODEX_HOME 时跟随它。
- 官方切换拒绝含 NUL 的异常配置；写入与回滚前检查文件状态，检测到外部修改时不覆盖。其他 profile 引用的原连接保留，当前 custom/yilai 官方别名不带第三方凭据。

## 旧易来对话与日志

API/官方切换时自动检查旧易来 yilai 对话：只将 JSONL 第一条 session_meta 和 SQLite 索引中的 yilai 改成 custom。openai、ccswitch、custom 等其他来源只读元数据前缀，不读取完整正文、不迁移；正文、标题、归档和父对话元数据不修改。不增加历史按钮。

迁移前备份到 yilai-history-backups，失败恢复，已是 custom 的不改写。待迁移文件仍做完整 JSONL 校验；重复检查走前缀和待迁移数据库行查询，不再反复读取大体积正文或完整数据库。切换不启动 Codex 做配置来源或回读核验。官方当前路由与 API 均采用 custom，认证方式随当前选择变化；没有官方 auth.json 时需重新登录。

日志在 CODEX_HOME/yilai-switcher-logs；重置只改名 config.toml。未测试生产官方发送，不声明完整复制 CCS 账号管理和自动化功能。

## CCS 使用顺序

先在 CCS 选择 API 供应商，再退出 CCS 和 Codex，运行本配置器。不要在 CCS 仍选中官方时用外部工具改写连接：CCS 后续可能把磁盘配置回存到当前官方条目。本版没有修复 CCS 回存状态冲突，也不修改其数据库。

每次配置都会写入内置 model-catalog.json 中固定的 gpt-5.6-sol、gpt-5.6-terra、gpt-6-astra，到 CODEX_HOME/yilai-model-catalog.json，并更新根配置和当前 profile 的指向；不联网获取模型。其他目录文件不删除。若有其他来源覆盖，需另行处理；重置按钮只改名当前 config.toml，随后重新配置。

## 开发与验证

共享配置规则：Sources/ConfigRewrite；生效来源：Sources/ConfigSources；操作锁：Sources/OperationGuard；日志：Sources/Diagnostics。Windows 平台代码：Windows；macOS：Sources/App。

Windows：pwsh -File Windows/build.ps1，运行 dist/windows/YilaiCodexSwitcher.exe --self-test，以及 python Tests/windows-ui.py。

运行时回归：pwsh -File Tests/run-runtime.ps1 -Codex <codex.exe绝对路径>。来源回归：python Tests/source-integration.py dist/test-driver.exe --auto-runtime。测试采用隔离目录和本机模拟服务，不调用付费模型。运行时发现与诊断回归见 Tests/runtime-discovery.py。

macOS：PUBLISH_DIR="$PWD/dist" bash build-macos.sh；公开工作流构建 Intel + Apple Silicon 通用版本，执行自测、DMG 校验和截图。macOS 13+，ad-hoc 签名，未公证。

v3.3.11 发布候选验证见 releases/v3.3.11/RELEASE-MANIFEST.md；正式附件发布后再更新 SHA256SUMS.txt。
