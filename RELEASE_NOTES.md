# v3.3.1 — 两个按钮，一键切换

- 按 Product Design 流程重做 Windows 和 macOS 原生界面：清晰的 Key 输入、两个主要切换按钮和简短状态。
- 切换到易来 API 时自动启用生图，两个切换都自动同步本地历史；不再显示独立生图、同步、撤销等按钮。
- 采用 CCS 关闭官方登录的 API 认证方式：供应商内独立 token、requires_openai_auth=false，并删除当前 auth.json；不写空登录文件、不清扫系统凭据。
- 低调的“重置配置”直接将 config.toml 改名加唯一后缀，不弹确认，保留原配置，不处理登录或历史。
- 官方连接可直接切换；切回后重新打开 Codex 登录即可。模型、CCS 目录和其他 profile 的连接按作用域保留。
- 普通文件删除、写入或历史同步失败会明确报错并回滚，避免显示未完成的切换为成功。

切换前完全退出 Codex 和 CC-Switch。后续用 CCS 切换时，请开启它的“统一 Codex 会话历史”。同步仅包含同一 CODEX_HOME 的本地记录。

已用 Codex app-server 0.153.4 验证：删除 auth 后 account=null、requiresOpenaiAuth=false，请求使用输入的 bearer Key，生图工具存在，历史自动同步后可读取并继续对话。验证使用本机模拟服务；生产实际出图未进行付费测试。最终两端构建、自测、截图及校验值见发布清单。
