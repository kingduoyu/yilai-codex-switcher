# 易来 Codex 配置器 v3.3.8

Windows / macOS 原生小工具：填写 API Key，一键配置易来 API 并启用生图。支持 API 与官方双向切换，并提供独立的本地历史统一入口。

## 使用

1. 完全退出 Codex 和 CC-Switch，包括后台进程。
2. 填写 API Key，点击 **配置易来 API · 启用生图**，完成后重新打开 Codex。
3. 旧对话来自 yilai、ccswitch 或官方时，完全退出 Codex/CCS 后点一次 **统一本地历史**，再选择 API 或官方。
4. **切换到官方** 使用官方认证路由，保留现有 auth.json；缺少官方登录时需要重新登录。

重置配置位于界面底部，仅将当前 config.toml 改名加唯一 disabled 后缀，使其失效。不弹确认、不删除原内容、不恢复停用文件、不动登录和历史。重置后重新配置 API。

## 配置与兼容

- 使用 CCS 兼容的 custom provider，供应商节点写入独立 bearer token，requires_openai_auth=false，启用生图并删除当前 CODEX_HOME/auth.json。
- 写入内置的 sol、terra、astra 三个模型和模型目录指向；保留已有合法模型选择，否则默认 sol。保留推理档位、MCP、权限等无关配置。
- 主按钮只执行本地配置写入，不启动 Codex、不检查跨文件优先级、不做运行时回读。写入错误保留日志并恢复本次改动。
- 同一 CODEX_HOME 的配置操作互斥。请勿在写入过程中启动 Codex/CCS。默认使用用户 .codex；设置 CODEX_HOME 时跟随它。

## 本地历史与日志

“统一本地历史”独立运行，迁移 sessions/archived_sessions 中第一条 session_meta 的 provider 和 state SQLite 索引为 custom。保留正文、继承的父会话元数据、标题和归档状态；仅处理已知旧 provider、当前配置定义的 provider 与 openai，不改未知私有 provider。

首次统一会扫描本地历史并创建 yilai-history-backups；出错恢复，未完成事务下次统一先恢复。重复运行无变化不再创建备份。迁移不修改连接或登录文件、不下载云端历史、不修改 CCS 数据库。日常 API/官方切换不扫描历史；新增旧来源会话需再次点统一。首次耗时取决于历史大小，不能保证十秒内。

API 和官方都使用 custom 归属；yilai 兼容别名随路由变化。官方 custom/yilai 不保留第三方地址、Key 或 headers。日志位于 CODEX_HOME/yilai-switcher-logs；重置只改名停用 config.toml。

## CCS 使用顺序

先在 CCS 选择 API 供应商，再退出 CCS 和 Codex，运行本配置器。不要在 CCS 仍选中官方时用外部工具改写连接：CCS 后续可能把磁盘配置回存到当前官方条目。本版没有修复 CCS 回存状态冲突，也不修改其数据库。

每次配置都会写入内置 model-catalog.json 中固定的 gpt-5.6-sol、gpt-5.6-terra、gpt-6-astra，到 CODEX_HOME/yilai-model-catalog.json，并更新根配置和当前 profile 的指向；不联网获取模型。其他目录文件不删除。若有其他来源覆盖，需另行处理；重置按钮只改名当前 config.toml，随后重新配置。

## 开发与验证

共享配置规则：Sources/ConfigRewrite；生效来源：Sources/ConfigSources；操作锁：Sources/OperationGuard；日志：Sources/Diagnostics。Windows 平台代码：Windows；macOS：Sources/App。

Windows：pwsh -File Windows/build.ps1，运行 dist/windows/YilaiCodexSwitcher.exe --self-test，以及 python Tests/windows-ui.py。

运行时回归：pwsh -File Tests/run-runtime.ps1 -Codex <codex.exe绝对路径>。来源回归：python Tests/source-integration.py dist/test-driver.exe --auto-runtime。测试采用隔离目录和本机模拟服务，不调用付费模型。运行时发现与诊断回归见 Tests/runtime-discovery.py。

macOS：PUBLISH_DIR="$PWD/dist" bash build-macos.sh；公开工作流构建 Intel + Apple Silicon 通用版本，执行自测、DMG 校验和截图。macOS 13+，ad-hoc 签名，未公证。

版本事实见 releases/v3.3.8/RELEASE-MANIFEST.md，校验值见 SHA256SUMS.txt。
