# 易来 Codex 配置器 v3.3.5

Windows / macOS 原生小工具：填写 API Key，一键配置易来 API 并启用生图。官方连接切换交给 CCS，本工具不提供切回官方或恢复旧配置的入口。

## 使用

1. 完全退出 Codex 和 CC-Switch，包括后台进程。
2. 填写 API Key，点击 **配置易来 API · 启用生图**，完成后重新打开 Codex。
3. 需要官方连接时，使用 CCS 切换。

重置配置位于界面底部，仅将当前 config.toml 改名加唯一 disabled 后缀，使其失效。不弹确认、不删除原内容、不恢复停用文件、不动登录和历史。重置后重新配置 API。

## 配置与兼容

- 使用 CCS 兼容的 custom provider，供应商节点写入独立 bearer token，requires_openai_auth=false，启用生图并删除当前 CODEX_HOME/auth.json。
- 保留用户模型、推理档位、CCS 模型目录、MCP、权限等无关配置；不修改 CCS 的开关、账号数据库或软件。
- API 写入前通过已安装 Codex 的 config/read 识别实际加载的来源，备份并移除已加载的更高优先级连接覆盖，写入后回读验证。配置、认证或来源核验失败时撤销本次未完成的配置操作。
- 检测覆盖默认用户目录和桌面记录的当前活动本地项目。未受信任项目不修改；未加载的 profile 不扫描；任意独立 CLI --profile/-c、未来项目及系统/组织策略不在自动处理范围。
- Windows 优先采用桌面端版本目录中的运行时，旧 bin/codex.exe 仅作为后备。macOS 使用其应用包运行时。
- 同一 CODEX_HOME 的配置操作互斥。请勿在写入过程中启动 Codex/CCS。默认使用用户 .codex；设置 CODEX_HOME 时跟随它。

## 本地历史与日志

CCS 已统一为 custom 的本地历史会保持该归属，我们配置 API 不会关闭 CCS 的历史统一开关。CCS 的迁移完成标记保存在它自己的设置中；不能把开关理解为永久自动迁移所有以后产生的其他归属记录。

API 配置后尝试同步已有本地 sessions、archived_sessions 和 state_5.sqlite，保留正文、标题、归档状态及分叉历史。支持 sqlite_home / CODEX_SQLITE_HOME。历史同步失败或旧历史恢复未完成时，保留已成功的 API 配置并显示警告和日志入口，不把历史问题当作连接失败；未完成的历史事务与备份保留供后续处理。

日志在 CODEX_HOME/yilai-switcher-logs，记录阶段、成功、失败、警告和回滚，不写密钥、配置全文或对话正文。RPC 拒绝显示安全分类、错误码、运行时路径和上下文。日志不采集后续聊天请求，不验证额度和服务器模型权限。

历史备份在 yilai-history-backups，来源备份在 yilai-source-backups。这些用于操作恢复，与重置后停用的配置文件无关；停用配置不会被重新启用。只处理已有本地历史，不下载其他账号云端记录。

## 开发与验证

共享配置规则：Sources/ConfigRewrite；生效来源：Sources/ConfigSources；历史：Sources/HistorySync；操作锁：Sources/OperationGuard；日志：Sources/Diagnostics。Windows 平台代码：Windows；macOS：Sources/App。

Windows：pwsh -File Windows/build.ps1，运行 dist/windows/YilaiCodexSwitcher.exe --self-test，以及 python Tests/windows-ui.py。

运行时回归：pwsh -File Tests/run-runtime.ps1 -Codex <codex.exe绝对路径>。来源回归：python Tests/source-integration.py dist/test-driver.exe --auto-runtime。测试采用隔离目录和本机模拟服务，不调用付费模型。运行时发现与诊断回归见 Tests/runtime-discovery.py。

macOS：PUBLISH_DIR="$PWD/dist" bash build-macos.sh；公开工作流构建 Intel + Apple Silicon 通用版本，执行自测、DMG 校验和截图。macOS 13+，ad-hoc 签名，未公证。

版本事实见 releases/v3.3.5/RELEASE-MANIFEST.md，校验值见 SHA256SUMS.txt。
