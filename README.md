# 易来 Codex 配置器 v3.3.3

Windows / macOS 原生小工具。填写 API Key，选择连接，重新打开 Codex 即可。

## 使用

1. 完全退出 Codex 和 CC-Switch，包括后台进程。
2. 使用易来时，填写 API Key，点击 **切换到易来 API**。生图和本地历史同步会自动完成。
3. 使用官方时，点击 **切换回官方设置**，重新打开 Codex 并登录。

下载本仓库 Releases 中的 Windows EXE 或 macOS 通用 DMG / ZIP。支持 Windows 10/11 x64、macOS 13+ Intel / Apple Silicon。macOS 为 ad-hoc 签名，首次打开可能需要右键“打开”。

界面底部的 **重置配置** 仅供切换无效时使用。点击即把 config.toml 改名为 config.toml.disabled-唯一后缀，不弹确认、不删除原文件、不自动恢复它，也不碰登录和历史。重置后重新填写 Key 并切换。

## 错误反馈

每次操作会自动写入 CODEX_HOME/yilai-switcher-logs。失败时界面显示具体阶段和原因，并出现“查看日志”入口；可将对应日志发给支持人员。日志包含版本、平台、操作步骤、错误与回滚结果，不记录配置全文、登录内容或对话正文，Key/令牌会脱敏。日志写入失败不会改变切换操作的成败；操作失败且日志无法完整保存时，会在错误信息中明确说明。这些日志仅记录配置器操作，不采集 Codex 后续聊天或网络请求；切换不会验证 Key 额度或服务端模型权限。

## 切换行为

- 易来使用 CCS 兼容的 custom provider，在供应商节点写入 API Key，设置 requires_openai_auth=false，并启用生图。
- 两个主切换在写配置前调用已安装 Codex 的 config/read，读取实际加载层及字段来源；默认检查当前用户目录，以及桌面记录的 active-workspace-roots 中可访问的本机活动项目。
- 对用户配置之上、已加载的项目/profile文件，先备份，再移除连接选择、连接覆盖和生图开关；保留模型、目录、MCP、权限及未使用的其他配置。存在但未受信任的项目层不修改，不扫描未加载的 profile 文件。
- 写入新连接后再次读取实际结果，核验 provider、地址、认证及 API 生图配置；核验失败不进入历史同步，恢复本次配置/auth/来源改动。源码层备份位于 CODEX_HOME/yilai-source-backups，已知中断记录下次切换自动处理。
- 核验针对本机默认启动及当前活动项目；独立 CLI 的 --profile/-c、未来新项目或之后更改的启动参数不在已验证范围。当前运行时 app-server 不能选择 CLI 独立 profile；不会以扫描文件代替实际加载结果。不可识别来源、不可访问的活动项目或不可修改的覆盖会明确报错，不宣称永远最高优先级。
- 两个切换按钮都会删除当前 CODEX_HOME/auth.json，再自动同步已有本地历史。默认使用当前用户 .codex；设置 CODEX_HOME 时跟随它。
- 采用 CCS v3.20.2 关闭保留官方登录的认证方式：独立 bearer token + 关闭官方认证 + 删除登录文件。不会写空 auth 对象、不会锁文件、不扫描旧登录档案，也不清理系统钥匙串或 Windows 凭据管理器。
- 切换保留模型和 CCS 模型目录，移除当前连接的强制登录/地址覆盖。官方使用原生认证，不保留第三方 token/地址/生图占位请求头。TOML 会规范化，原注释不保留。
- 同一 CODEX_HOME 的完整操作互斥，多个配置器同时操作时会拒绝后来的操作；锁随进程退出释放。
- 切换前自动检查上次中断的历史同步，完成收尾或恢复后继续；未知状态或无法安全恢复时保留现场并报错。重置仍只停用配置，不触发历史恢复。
- 相同连接重复切换不增加备用 provider；只有其他 profile 确实需要旧连接时才保留。已有旧备用节点不自动批量删除。
- 只在所有步骤完成后显示切换成功。普通写入、登录删除或历史同步失败时，还原本次连接和登录改动；历史核心负责自身回滚。不要在操作中启动 Codex/CCS 或强制结束进程。

## 本地历史

切换时备份并同步 sessions、archived_sessions 及 state_5.sqlite 的会话归属，保留消息、标题与归档状态。只共享同一 CODEX_HOME 已有的本地记录，不下载其他账号的云端会话。

历史备份位于 CODEX_HOME/yilai-history-backups，包含迁移清单、会话文件与数据库备份。支持 sqlite_home / CODEX_SQLITE_HOME。损坏历史、未知数据库版本或并发改动会使同步失败并明确报错，保留现场与备份。

以后若用 CCS 切换，开启 CCS 自己的“统一 Codex 会话历史”，才能持续使用同一历史分类。本工具不修改 CCS 软件、设置或账号库。

## 验证与开发

配置规则：Sources/ConfigRewrite。生效来源识别、事务和运行时探测：Sources/ConfigSources。历史核心：Sources/HistorySync。完整操作互斥：Sources/OperationGuard。Windows 界面与文件操作：Windows。macOS：Sources/App。

Windows：pwsh -File Windows/build.ps1，然后运行 dist/windows/YilaiCodexSwitcher.exe --self-test，以及 python Tests/windows-ui.py 验证真实失败后的日志按钮状态。

macOS：PUBLISH_DIR="$PWD/dist" bash build-macos.sh；正式通用版由 Build macOS app 工作流构建、执行自测、校验 DMG 并截图。

真实运行时验证：pwsh -File Tests/run-runtime.ps1 -Codex <codex.exe绝对路径>。需要 Node.js、Python 和 LLVM-MinGW。测试使用独立数据目录与本机模拟 Responses 服务，验证 auth 删除后无需官方登录、API Key 实际请求认证、内置生图工具、自动同步与继续对话；不调用付费模型。

版本事实见 releases/v3.3.3/RELEASE-MANIFEST.md，制品校验值见 SHA256SUMS.txt。

新增来源集成验证：python Tests/source-integration.py dist/test-driver.exe <codex.exe绝对路径>。使用合成受信/未受信项目、嵌套覆盖与故障历史，不调用付费模型。
