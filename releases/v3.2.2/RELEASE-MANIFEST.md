# v3.2.2 Windows / macOS 发布清单

- 日期：2026-09-07。用户已授权发布；Windows 候选已通过自测，macOS 正式 Actions 构建待完成，Release 尚未公开。
- 范围：Windows C++ 与 macOS Swift 配置服务、现有隔离自测、版本及切换提示；本地源码与公开仓库同步。macOS 额外清理旧版 auth.json.yilai-session-* 文件。
- 行为：两个方向均删除 auth.json 与 auth.json.yilai-disabled，不再备份或恢复登录。清理旧 manifest.json、备份 config.toml；旧目录中其他文件保留。
- 官方模式清理第三方 provider、Key、模型目录引用，保留其他配置。两个模式均使用 file 凭据存储，不修改系统凭据库。
- 失败时以内存快照尝试回滚；不保证异常终止或断电后的恢复。运行前须退出 Codex 和 CC-Switch。

## 验证

- LLVM-MinGW Release 构建成功。
- 候选 EXE --self-test 退出码 0。
- 覆盖新用户、重复切换、双认证文件冲突、损坏的旧清单、旧目录升级、无关文件保留，以及锁定停用文件后的两个方向事务回滚。
- 自测仅使用临时目录，没有修改真实用户配置或认证文件。
- 未进行真实账户登录验收；macOS 正式双架构构建、自测和 UI 截图待核对。

## 制品

- 本地：配置器/publish/candidate-no-auth-backup/win-x64/YilaiCodexSwitcher.exe
- SHA-256：BFAF18F6A22230D0E75357DB572A0E9CB0823E531B126463B711B86E8B5025E1
