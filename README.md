# 易来 Codex 配置器 v3.3.16

Windows / macOS 原生小工具：填写 API Key，一键配置易来 API 并启用生图。支持 API 与官方双向切换，切换时自动检查旧易来对话归属，无需额外按钮。

稳定下载地址：[Windows EXE](https://github.com/kingduoyu/yilai-codex-switcher/releases/latest/download/YilaiCodexSwitcher.exe) · [macOS DMG](https://github.com/kingduoyu/yilai-codex-switcher/releases/latest/download/YilaiCodexSwitcher-macOS-universal.dmg)。也可以打开 [GitHub Releases](https://github.com/kingduoyu/yilai-codex-switcher/releases/latest) 页面下载。

## 使用

1. 完全退出 Codex 和 CC-Switch，包括后台进程。
2. 填写 API Key，点击 **配置易来 API · 启用生图**，完成后重新打开 Codex。
3. 旧易来对话检查在连接配置成功后自动执行；只有 `yilai` 记录才修复，已是 `custom` 或其他来源的不改写。异常会话会跳过，不影响当前连接。
4. **切换到官方** 使用官方认证路由，保留现有 auth.json；缺少官方登录时需要重新登录。

重置配置位于界面底部，仅将当前 config.toml 改名加唯一 disabled 后缀，使其失效。不弹确认、不删除原内容、不恢复停用文件、不动登录和历史。重置后重新配置 API。

## 配置与兼容

- 使用 CCS 兼容的 custom provider，供应商节点写入独立 bearer token，requires_openai_auth=false，启用生图并删除当前 CODEX_HOME/auth.json。
- 写入内置的 sol、terra、astra 三个模型和模型目录指向；保留已有合法模型选择，否则默认 sol。保留推理档位、MCP、权限等无关配置。配置未显式写权限时，沿用 Codex 桌面端已保存的完全访问模式，不降级为审批模式。
- 主按钮先完成本地连接、模型目录和登录文件处理，再启动隔离的 Codex app-server 做只读最终探测。探测核对实际 provider、地址、认证、模型目录、生图开关和生图授权，并指出覆盖来源；探测或功能冲突不回滚已完成的 API 配置，也不自动修改项目/profile/启动参数。
- 同一 CODEX_HOME 的配置操作互斥。请勿在写入过程中启动 Codex/CCS。默认使用用户 .codex；设置 CODEX_HOME 时跟随它。
- 官方切换拒绝含 NUL 的异常配置；写入与回滚前检查文件状态，检测到外部修改时不覆盖。其他 profile 引用的原连接保留，当前 custom/yilai 官方别名不带第三方凭据。

## 旧易来对话与错误提示

API/官方切换时自动检查旧易来 yilai 对话：只将 JSONL 第一条 session_meta 和 SQLite 索引中的 yilai 改成 custom。openai、ccswitch、custom 等其他来源只读元数据前缀，不读取完整正文、不迁移；正文、标题、归档和父对话元数据不修改。不增加历史按钮。

连接配置、模型目录和登录处理是主流程，完成后不因本地历史异常回滚。历史同步按文件尽量执行：重复 session ID 不再阻断；无法读取、JSONL 损坏、备份或写入失败的单个会话会记录告警并跳过，其余会话继续。成功迁移的内容备份到 yilai-history-backups，已是 custom 的不改写；待迁移文件仍做完整 JSONL 校验。重复检查走前缀和待迁移数据库行查询，不再反复读取大体积正文或完整数据库。API 核心写入完成后启动隔离运行时做只读最终探测；探测和历史告警均不回滚连接。官方当前路由与 API 均采用 custom，认证方式随当前选择变化；没有官方 auth.json 时需重新登录。

配置失败或最终探测发现功能缺失时，界面直接显示脱敏后的具体原因和可识别的配置来源，供截图反馈；不再生成过程 JSON 日志或提供“查看日志”入口。只有连接、模型目录或登录文件在写入阶段形成半套配置时才回滚本轮核心写入；最终探测、功能冲突和历史异常只告警并保留可用连接。重置只改名 config.toml。未测试生产官方发送，不声明完整复制 CCS 账号管理和自动化功能。

## CCS 使用顺序

先在 CCS 选择 API 供应商，再退出 CCS 和 Codex，运行本配置器。不要在 CCS 仍选中官方时用外部工具改写连接：CCS 后续可能把磁盘配置回存到当前官方条目。本版不修改 CCS 数据库；若 CCS 使用的配置目录、项目配置、profile、启动参数或托管配置造成最终冲突，配置器会保留 API 配置并直接显示原因。

每次配置都会写入内置 model-catalog.json 中固定的 gpt-5.6-sol、gpt-5.6-terra、gpt-6-astra，到 CODEX_HOME/yilai-model-catalog.json，并更新根配置和当前 profile 的指向；不联网获取模型。其他目录文件不删除。若有其他来源覆盖，最终探测会报告功能缺失和来源，不会因探测不通过而恢复旧连接；重置按钮只改名当前 config.toml，随后重新配置。

## 开发与验证

共享配置规则：Sources/ConfigRewrite；生效来源：Sources/ConfigSources；操作锁：Sources/OperationGuard；错误脱敏：Sources/Diagnostics。Windows 平台代码：Windows；macOS：Sources/App。

Windows：pwsh -File Windows/build.ps1，运行 dist/windows/YilaiCodexSwitcher.exe --self-test，以及 python Tests/windows-ui.py。

运行时回归：pwsh -File Tests/run-runtime.ps1 -Codex <codex.exe绝对路径>。来源回归：python Tests/source-integration.py dist/test-driver.exe --auto-runtime。测试采用隔离目录和本机模拟服务，不调用付费模型。运行时发现与诊断回归见 Tests/runtime-discovery.py。

macOS：PUBLISH_DIR="$PWD/dist" bash build-macos.sh；公开工作流构建 Intel + Apple Silicon 通用版本，执行自测、DMG 校验和截图。macOS 13+，ad-hoc 签名，未公证。

v3.3.16 构建、附件与发布验证见 releases/v3.3.16/RELEASE-MANIFEST.md。
