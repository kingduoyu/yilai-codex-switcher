# 易来 Codex 配置器 v3.4.1

Windows / macOS 原生小工具：填写 API Key，一键配置易来 API 并启用生图。支持 API 与官方双向切换；常规切换不处理历史对话归属。

稳定下载地址：[Windows EXE](https://github.com/kingduoyu/yilai-codex-switcher/releases/latest/download/YilaiCodexSwitcher.exe) · [macOS DMG](https://github.com/kingduoyu/yilai-codex-switcher/releases/latest/download/YilaiCodexSwitcher-macOS-universal.dmg)。也可以打开 [GitHub Releases](https://github.com/kingduoyu/yilai-codex-switcher/releases/latest) 页面下载。

## 使用

启动后后台检查软件新版；右上角单行显示当前版本，有新版时显示黄色圆点与新版号。点击版本菜单中的 **一键更新**，下载并校验后退出配置器，辅助程序保留旧版、替换并重新打开。失败时显示结果并保留或恢复旧版。可用 **检查更新** 重试，**更新说明** 查看新版说明。macOS 应先把应用复制到可写的 Applications 目录，不能直接在只读 DMG 内更新。

点击 **更新模型** 可独立同步新模型，无需下载新版软件或填写 Key。操作前退出 Codex 和 CC-Switch，更新后重开 Codex。只更新 `CODEX_HOME/yilai-model-catalog.json`，当前模型、账号、登录和历史不变；官方模式下该目录不生效。已更新的目录会在下一次配置易来 API 时继续使用。

1. 完全退出 Codex 和 CC-Switch，包括后台进程。
2. 填写 API Key，点击 **配置易来 API · 启用生图**，完成后重新打开 Codex。
3. **切换到官方** 使用官方认证路由，保留现有 auth.json；缺少官方登录时需要重新登录。

重置配置位于界面底部，仅将当前 config.toml 改名加唯一 disabled 后缀，使其失效。不弹确认、不删除原内容、不恢复停用文件、不动登录和历史。重置后重新配置 API。

## 配置与兼容

- 使用 CCS 兼容的 custom provider，供应商节点写入独立 bearer token，requires_openai_auth=false，启用生图并将当前 CODEX_HOME/auth.json 移入系统回收站或废纸篓。不生成固定名称的登录备份，已有 auth.json.yilai-disabled 不参与切换，也不阻止配置。
- 写入内置的 5.6 Sol、6 Sol、6.1 Sol、5.6 Terra、6 Astra 五个模型和模型目录指向；保留已有合法模型选择，否则默认 5.6 Sol。保留推理档位、MCP、权限等无关配置。配置未显式写权限时，沿用 Codex 桌面端已保存的完全访问模式，不降级为审批模式。
- 主按钮先完成本地连接、模型目录和登录文件处理，再启动隔离的 Codex app-server 做只读最终探测。探测核对实际 provider、地址、认证、模型目录、生图开关和生图授权，并指出覆盖来源；探测或功能冲突不回滚已完成的 API 配置，也不自动修改项目/profile/启动参数。
- 同一 CODEX_HOME 的配置操作互斥。请勿在写入过程中启动 Codex/CCS。默认使用用户 .codex；设置 CODEX_HOME 时跟随它。
- 官方切换拒绝含 NUL 的异常配置；写入与回滚前检查文件状态，检测到外部修改时不覆盖。其他 profile 引用的原连接保留，当前 custom/yilai 官方别名不带第三方凭据。

## 错误提示

API 和官方切换不扫描、迁移、恢复或检查历史对话归属，不读写用户历史 JSONL、SQLite 或已有迁移备份。最终配置核验使用隔离临时数据库，不重建用户历史索引。

配置失败或最终探测发现功能缺失时，界面直接显示脱敏后的具体原因和可识别的配置来源，供截图反馈；不再生成过程 JSON 日志或提供“查看日志”入口。只有连接、模型目录或登录文件在写入阶段形成半套配置时才回滚本轮核心写入；最终探测和功能冲突只告警并保留可用连接。重置只改名 config.toml。未测试生产官方发送，不声明完整复制 CCS 账号管理和自动化功能。

## CCS 使用顺序

先在 CCS 选择 API 供应商，再退出 CCS 和 Codex，运行本配置器。不要在 CCS 仍选中官方时用外部工具改写连接：CCS 后续可能把磁盘配置回存到当前官方条目。本版不修改 CCS 数据库；若 CCS 使用的配置目录、项目配置、profile、启动参数或托管配置造成最终冲突，配置器会保留 API 配置并直接显示原因。

首次配置写入内置五个模型；已有合法更新目录时继续使用该目录。配置按钮不依赖网络；独立“更新模型”从公开 `model-channel.json` 获取不可变提交的目录，核对 SHA-256、结构和已有模型，失败不覆盖原目录。配置更新根配置和当前 profile 的目录指向；其他目录文件不删除。若有其他来源覆盖，最终探测会报告来源，不会因探测不通过而恢复旧连接。

## 维护更新

新增模型：编辑 `model-catalog.json`，在 `main` 运行 `pwsh -File Tools/Publish-ModelCatalog.ps1` 校验，再以 `-Publish` 发布目录（支持 `-WhatIf`）。工具先提交并推送目录，再发布指向该不可变提交的频道及校验值，不打软件版本标签、不创建软件 Release。不得移除已有模型或带入其他未提交改动；发布中断时先核对已有提交及频道，不机械重发。软件版本中的内置目录仍独立保留，供离线首次配置使用。

软件更新仍使用 GitHub stable latest Release。必须同时上传固定名称 `YilaiCodexSwitcher.exe`、`YilaiCodexSwitcher-macOS-universal.dmg` 和自动更新用的 `YilaiCodexSwitcher-macOS-universal.zip`；三者来自同一源码。更新器核对 GitHub 附件的 SHA-256、大小、固定仓库 URL 和实际程序版本，不安装预发布或旧版。Windows 更新辅助程序随 EXE 内嵌，macOS 辅助程序随应用生成；都保留回退副本，不触碰 Codex 数据。传输依赖 HTTPS 与 GitHub 发布权限，未增加独立代码签名服务。

## 开发与验证

使用教程和配置教程统一指向 `https://api.yilai-ai.com/docs/tutorial/`，不使用版本参数或第二套页面。发布只更新唯一教程 HTML，并从公开设置接口读取实际菜单入口核验；旧 `index.html` 兼容入口必须返回同一内容。

发布收尾必须同步使用教程，固定流程见上一级 `配置器/README.md` 的“发布收尾：同步使用教程（必做）”。GitHub Release 完成后，更新教程显示版本及更新说明链接，部署静态页面并核验公开 URL；保留固定下载地址。教程未同步或核验未通过时，不得宣布整个发布完成。每次结果仅记录在对应 `releases/<版本>/RELEASE-MANIFEST.md`。

共享配置规则：Sources/ConfigRewrite；生效来源：Sources/ConfigSources；操作锁：Sources/OperationGuard；错误脱敏：Sources/Diagnostics。Windows 平台代码：Windows；macOS：Sources/App。

Windows：pwsh -File Windows/build.ps1，运行 dist/windows/YilaiCodexSwitcher.exe --self-test，以及 python Tests/windows-ui.py。

更新回归：pwsh -File Tests/update-helper.ps1 验证 Windows 替换、哈希失败和启动失败回退；python Tests/model-update.py dist/test-driver.exe 验证目录独立写入及后续配置保留。macOS 工作流运行 Tests/macos-update.py，验证实际安装、重开、旧版备份及替换失败恢复；新版提示截图必须通过黄色圆点像素检查。

运行时回归：pwsh -File Tests/run-runtime.ps1 -Codex <codex.exe绝对路径>。来源回归：python Tests/source-integration.py dist/test-driver.exe --auto-runtime。测试采用隔离目录和本机模拟服务，不调用付费模型。运行时发现与诊断回归见 Tests/runtime-discovery.py。

macOS：PUBLISH_DIR="$PWD/dist" bash build-macos.sh；公开工作流构建 Intel + Apple Silicon 通用版本，执行自测、DMG 校验和截图。macOS 13+，ad-hoc 签名，未公证。

本版发布证据见 releases/v3.4.1/RELEASE-MANIFEST.md。
