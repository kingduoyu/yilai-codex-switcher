# 易来 Codex 配置器 v3.3.6

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

本版不读取、同步、迁移或恢复历史，不改写历史数据库。CCS 的历史统一开关保持原样。配置来源探测使用临时数据库目录。

日志在 CODEX_HOME/yilai-switcher-logs，记录配置阶段、错误和回滚，不写密钥或对话正文。重置只改名停用 config.toml。

## CCS 使用顺序

先在 CCS 选择 API 供应商，再退出 CCS 和 Codex，运行本配置器。不要在 CCS 仍选中官方时用外部工具改写连接：CCS 后续可能把磁盘配置回存到当前官方条目。本版没有修复 CCS 回存状态冲突，也不修改其数据库。

模型选择和现有目录指向原样保留，包括旧版 yilai-model-catalog.json；不删除旧指向，不获取或生成新目录。新安装用户需要已有模型目录或使用运行时默认列表。

## 开发与验证

共享配置规则：Sources/ConfigRewrite；生效来源：Sources/ConfigSources；操作锁：Sources/OperationGuard；日志：Sources/Diagnostics。Windows 平台代码：Windows；macOS：Sources/App。

Windows：pwsh -File Windows/build.ps1，运行 dist/windows/YilaiCodexSwitcher.exe --self-test，以及 python Tests/windows-ui.py。

运行时回归：pwsh -File Tests/run-runtime.ps1 -Codex <codex.exe绝对路径>。来源回归：python Tests/source-integration.py dist/test-driver.exe --auto-runtime。测试采用隔离目录和本机模拟服务，不调用付费模型。运行时发现与诊断回归见 Tests/runtime-discovery.py。

macOS：PUBLISH_DIR="$PWD/dist" bash build-macos.sh；公开工作流构建 Intel + Apple Silicon 通用版本，执行自测、DMG 校验和截图。macOS 13+，ad-hoc 签名，未公证。

版本事实见 releases/v3.3.6/RELEASE-MANIFEST.md，校验值见 SHA256SUMS.txt。
