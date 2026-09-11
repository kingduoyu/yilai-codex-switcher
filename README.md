# 易来 Codex 配置器 v3.3.0

Windows 与 macOS 原生重写版。为 CCS 已有连接启用生图，并统一这台电脑已有的 Codex 历史。

## 下载

在本仓库 Releases 下载 Windows 的 YilaiCodexSwitcher.exe，或 macOS 的 YilaiCodexSwitcher-macOS-universal.dmg（Intel / Apple Silicon）。Windows 10/11 x64、macOS 13+。无需额外安装运行库。macOS 采用 ad-hoc 签名，首次打开可能需要右键选择“打开”。

## 操作

操作前完全退出 Codex 和 CC-Switch，包括后台进程。默认使用当前用户的 .codex，设置 CODEX_HOME 时跟随该目录。

- **启用生图**：只启用 image_generation，并给当前自定义 provider 补生图请求头；保留模型、模型目录、其他请求头、认证和无关配置。无需重新填写 Key。TOML 格式会规范化，原注释不保留。
- **配置易来连接**：显式使用输入 Key 配置 CCS 兼容的 custom 连接。保留现有模型及目录，不清理官方登录。请使用 CCS / Codex 选择服务端支持的模型。
- **同步全部本地历史**：先备份，再将 sessions、archived_sessions 和 state_5.sqlite 中已有会话统一归入 custom；同时让当前连接使用这一分类。消息、标题、归档状态和登录凭据不变，不下载云端历史。
- **撤销上次同步**：按备份还原原有会话归属，保留之后新增的消息和会话；若连接配置已被其他工具改动，则不覆盖它。
- **清理旧登录**：仅用于有独立 bearer Key 的易来直连。旧登录及本工具旧备份移入 Windows 回收站 / macOS 废纸篓，解除本工具旧 yilai-model-catalog.json 的引用。保留 CCS 和其他自定义目录文件、历史及系统钥匙串。官方模式和依赖官方认证的连接禁止清理。

## 与 CCS 配合

切回官方请使用 CCS。若要在 CCS 的官方/第三方切换后持续共享同一历史分类，请在 CCS 开启“统一 Codex 会话历史”；否则需要在切换后再次点击本工具的同步。单纯更换登录账号不会改变同一 CODEX_HOME 的已同步记录。

本工具不修改 CCS 程序或其设置。同步机制参考 CCS v3.20.2：统一 provider 分类、官方使用原生认证、同时迁移 JSONL 与 state 索引，并保留备份。不同账号只共享同一电脑、同一 Codex 数据目录中已有的记录；归档会话仍保持归档。

CCS 的模型获取和目录由其配置决定，并非本工具自动更新。v3.3.0 不再生成固定三个模型的目录，也不强制切换到 Sol。模型名能填写/出现在目录中，不等于服务端已授权调用该模型。

## 历史保护

备份在 CODEX_HOME/yilai-history-backups，每次同步有独立目录，包含 config、完整会话文件、SQLite 备份与迁移清单。备份可能包含私密消息和连接凭据，请只留在本机。

只更新会话归属字段。同步发现损坏 JSONL、重复会话 ID、未知 state 数据库版本或并发改动时停止并回滚；失败日志及备份应保留。若操作被中断，可用“撤销上次同步”恢复未完成的同步。支持 config 的 sqlite_home 或 CODEX_SQLITE_HOME 指向的额外 state_5.sqlite。系统目录别名会先解析；历史目录内部的链接不迁移。若同步使用外部 SQLite 目录，撤销时请保留原 sqlite_home / CODEX_SQLITE_HOME 设置。

## 开发

全新源码：Sources/ConfigRewrite（配置规则）、Sources/HistorySync（历史迁移与撤销）、Sources/App（macOS）、Windows/App.cpp 与 Platform.cpp（Windows）。旧版本可从 v3.2.4 等 Git 标签获取；新应用不依赖旧版实现或固定模型目录。

Windows：pwsh -File Windows/build.ps1，随后执行 dist/windows/YilaiCodexSwitcher.exe --self-test。

macOS：`PUBLISH_DIR="$PWD/dist" bash build-macos.sh`；正式 DMG/ZIP 由 Build macOS app 工作流构建、执行自测并生成界面截图。

真实运行时集成验证（Windows，需 Node.js、Python 及 LLVM-MinGW）：`pwsh -File Tests/run-runtime.ps1 -Codex <codex.exe绝对路径>`。测试使用隔离数据目录和本机模拟 Responses 接口，不读取真实登录，不调用付费模型。覆盖模型列表、内置生图工具声明、旧对话列表/读取/继续、撤销保留新消息、官方配置及模拟凭据更换；不代表已验证生产服务生成图片。

校验值见 SHA256SUMS.txt，发布事实见 releases/v3.3.0/RELEASE-MANIFEST.md。
