# 易来 Codex 配置器 v3.3.1

Windows / macOS 原生小工具。填写 API Key，选择连接，重新打开 Codex 即可。

## 使用

1. 完全退出 Codex 和 CC-Switch，包括后台进程。
2. 使用易来时，填写 API Key，点击 **切换到易来 API**。生图和本地历史同步会自动完成。
3. 使用官方时，点击 **切换回官方设置**，重新打开 Codex 并登录。

下载本仓库 Releases 中的 Windows EXE 或 macOS 通用 DMG / ZIP。支持 Windows 10/11 x64、macOS 13+ Intel / Apple Silicon。macOS 为 ad-hoc 签名，首次打开可能需要右键“打开”。

界面底部的 **重置配置** 仅供切换无效时使用。点击即把 config.toml 改名为 config.toml.disabled-唯一后缀，不弹确认、不删除原文件、不自动恢复它，也不碰登录和历史。重置后重新填写 Key 并切换。

## 切换行为

- 易来使用 CCS 兼容的 custom provider，在供应商节点写入 API Key，设置 requires_openai_auth=false，并启用生图。
- 两个切换按钮都会删除当前 CODEX_HOME/auth.json，再自动同步已有本地历史。默认使用当前用户 .codex；设置 CODEX_HOME 时跟随它。
- 采用 CCS v3.20.2 关闭保留官方登录的认证方式：独立 bearer token + 关闭官方认证 + 删除登录文件。不会写空 auth 对象、不会锁文件、不扫描旧登录档案，也不清理系统钥匙串或 Windows 凭据管理器。
- 切换保留模型和 CCS 模型目录，移除当前连接的强制登录/地址覆盖。官方使用原生认证，不保留第三方 token/地址/生图占位请求头。TOML 会规范化，原注释不保留。
- 只在所有步骤完成后显示切换成功。普通写入、登录删除或历史同步失败时，还原本次连接和登录改动；历史核心负责自身回滚。不要在操作中启动 Codex/CCS 或强制结束进程。

## 本地历史

切换时备份并同步 sessions、archived_sessions 及 state_5.sqlite 的会话归属，保留消息、标题与归档状态。只共享同一 CODEX_HOME 已有的本地记录，不下载其他账号的云端会话。

历史备份位于 CODEX_HOME/yilai-history-backups，包含迁移清单、会话文件与数据库备份。支持 sqlite_home / CODEX_SQLITE_HOME。损坏历史、未知数据库版本或并发改动会使同步失败并明确报错，保留现场与备份。

以后若用 CCS 切换，开启 CCS 自己的“统一 Codex 会话历史”，才能持续使用同一历史分类。本工具不修改 CCS 软件、设置或账号库。

## 验证与开发

配置规则：Sources/ConfigRewrite。历史核心：Sources/HistorySync。Windows 界面与文件操作：Windows。macOS：Sources/App。

Windows：pwsh -File Windows/build.ps1，然后运行 dist/windows/YilaiCodexSwitcher.exe --self-test。

macOS：PUBLISH_DIR="$PWD/dist" bash build-macos.sh；正式通用版由 Build macOS app 工作流构建、执行自测、校验 DMG 并截图。

真实运行时验证：pwsh -File Tests/run-runtime.ps1 -Codex <codex.exe绝对路径>。需要 Node.js、Python 和 LLVM-MinGW。测试使用独立数据目录与本机模拟 Responses 服务，验证 auth 删除后无需官方登录、API Key 实际请求认证、内置生图工具、自动同步与继续对话；不调用付费模型。

版本事实见 releases/v3.3.1/RELEASE-MANIFEST.md，制品校验值见 SHA256SUMS.txt。
