# v3.2.2 Windows / macOS 发布清单

- 日期：2026-09-07。用户已授权发布；Windows 与 macOS 制品验证通过，待上传并公开 Release。
- 范围：Windows C++ 与 macOS Swift 配置服务、现有隔离自测、版本及切换提示；本地源码与公开仓库同步。macOS 额外清理旧版 auth.json.yilai-session-* 文件。
- 行为：两个方向均删除 auth.json 与 auth.json.yilai-disabled，不再备份或恢复登录。清理旧 manifest.json、备份 config.toml；旧目录中其他文件保留。
- 官方模式清理第三方 provider、Key、模型目录引用，保留其他配置。两个模式均使用 file 凭据存储，不修改系统凭据库。
- 失败时以内存快照尝试回滚；不保证异常终止或断电后的恢复。运行前须退出 Codex 和 CC-Switch。

## 验证

- LLVM-MinGW Release 构建成功。
- 候选 EXE --self-test 退出码 0。
- 覆盖新用户、重复切换、双认证文件冲突、损坏的旧清单、旧目录升级、无关文件保留，以及锁定停用文件后的两个方向事务回滚。
- 自测仅使用临时目录，没有修改真实用户配置或认证文件。
- macOS Actions：34131789343，结论 success；源码提交 `0a37338c803a1b3002905b06cae382f8b1763d3b`。
- macOS arm64 / x86_64 构建、通用包隔离自测、DMG hdiutil verify 均成功；截图已核对 v3.2.2，布局无异常。
- Mac 自测覆盖双认证冲突、旧归档清理、坏清单、缺失旧备份、Keychain 隔离、重复切换、当前通用设置保留，以及不可删除文件触发的两个方向部分清理回滚。
- 未进行真实账户登录验收；所有自测均未操作真实用户配置。

## 制品

- 本地：配置器/publish/candidate-no-auth-backup/win-x64/YilaiCodexSwitcher.exe
- Windows EXE SHA-256：BFAF18F6A22230D0E75357DB572A0E9CB0823E531B126463B711B86E8B5025E1
- Mac 制品目录：`dist/release-macos-34131789343/`。
- Mac DMG SHA-256：E3923DAF404447849AD2923E4D5B9627C19CCBE6D809A80DA166708D4D44CD37
- Mac ZIP SHA-256：184C6B24D7F0C062E682593FE554441F4A84F3447B301166597016EDB84BAB82
- Mac 截图：`dist/release-macos-34131789343/implementation-macos.png`。
