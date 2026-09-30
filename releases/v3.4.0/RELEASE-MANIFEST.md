# v3.4.0 发布清单

日期：2026-09-30。软件更新放在右上角版本菜单，黄色圆点提示新版；独立模型列表和“更新模型”位于配置 API 按钮上方。

## 源码与验证

- 发布标签及应用源码：`967ac583f33972be17120165a11ecb3607afc02e`；源码树：`7516b2a602863a3cae269cae296f1890baab143f`。后续发布记录提交仅改文档。
- Windows 最终程序原生自测、版本资源 `3.4.0.0`、`Tests/windows-ui.py` 通过；最终截图已检查，右上角黄色点像素验证通过。Windows 在 `0bb590c` 修复后构建，已核验该提交与发布标签的 Windows 及共享构建输入完全一致。
- `Tests/update-helper.ps1` 三项通过：正常替换并启动、错误哈希拒绝且保留原程序、启动失败后恢复并重开旧版。证据目录：`dist/update-helper-32d73b0001c345da88e723e928152b9a`。
- 共享更新协议测试通过，验证稳定版本、固定附件地址、SHA-256、不可变模型 URL、模型重复及移除拒绝；模型独立写入回归通过，登录、当前配置和历史不变，新增模型在后续 API 配置时保留。
- 运行时集成通过：Codex `0.159.0`，`dist/runtime-test-oBt18f/result.json`；配置来源回归通过：`dist/source-test-19599q1h/result.json`。验证后共享配置、模型和探测实现未改变；仅调整原生更新辅助程序、界面及对应测试。使用隔离目录与本机模拟服务，未声明付费上游真实模型调用通过。
- macOS 最终工作流 `36694209074` 在发布源码提交通过双架构构建、自测、DMG 校验、实际 helper 安装与重开、保留旧版备份，以及候选移动失败后的恢复与重开。证据：`dist/macos-v3.4.0-release/macos-update-result.json`，`passed=true`、`source_app_unchanged=true`。
- macOS 最终截图已检查，右上角黄色点独立像素检查为 24 个；ZIP CRC、版本 `3.4.0/build31`、Mach-O `x86_64` 与 `arm64` 核验通过。构建产物哈希与工作流输出一致。未声明两种架构分别实机测试；ad-hoc 签名、未公证。
- 使用真实 WinHTTP 下载器核验 latest Release、独立模型频道和不可变目录哈希，均通过；维护发布工具 dry-run 与 `-Publish -WhatIf` 通过。

## SHA-256

| 固定附件 | SHA-256 |
| --- | --- |
| YilaiCodexSwitcher.exe | 365cbf11b46f1105bb46d946b6d22d7b9bab2ad5891b6ba1041138f8a9d4d6eb |
| YilaiCodexSwitcher-macOS-universal.dmg | 119441f961dc1c62c795d7947bf3e4dd7464d889d81f6cb7921e96d36b732f08 |
| YilaiCodexSwitcher-macOS-universal.zip | f4b166e88922339a4e68c6ee20513c81e4d03ae74c97bb8611ca887b91382479 |

## 发布与模型频道

- 正式 Release：https://github.com/kingduoyu/yilai-codex-switcher/releases/tag/v3.4.0 。latest 指向 `v3.4.0`，非草稿、非预发布；三个固定附件均为 uploaded，远端大小与 digest 非空且与本地一致。未回下载 Release 附件。
- 已验证的三个固定文件复制到 `publish/win-x64` 与 `publish/mac-universal`，复制后哈希一致。
- 独立模型频道 `model-channel.json` 已随 main 上线；revision `1`，包含五个模型，指向不可变提交 `12d09d97dd7fa66c8a863f579a8ec39829bae0cd` 的 `model-catalog.json`。目录 SHA-256：`710f2306befcf3a31fed58373ee397362e36c2dbd1132bac3c1c600be96ca966`，实际在线下载校验通过。
- 今后新增模型使用 `Tools/Publish-ModelCatalog.ps1 -Publish`，只发布目录和频道，不打软件标签。旧版用户需下载本版一次，此后可在软件内更新。官方模式不启用易来目录。

## 教程同步

- 唯一入口 `https://api.yilai-ai.com/docs/tutorial/`，公开菜单 `usage-guide`（配置教程）回读一致，无版本参数。
- 本地唯一教程 HTML 与线上页面已同步显示 `v3.4.0`、对应更新说明链接及新的更新入口说明；两个 `releases/latest/download` 地址保持不变。
- 固定地址及兼容 `/docs/tutorial/index.html` 均为 HTTP 200，返回完全相同的页面字节，SHA-256：`2d55ba56cdf0c7b68bad6c9abca12088485f8a09b66415a3faafed58822913cb`。
- 原子替换前保留可读备份：`/root/tutorial-backups/index-before-v3.4.0-20260930.html`，SHA-256：`28949e7b071f2850f6382712f68b5a993b837416d57c29a0f525c10d55c9670c`。未修改应用、数据库或 Caddy，未重启服务。
- 软件三附件、模型独立维护及唯一教程同步已纳入 `配置器/README.md` 和源码 README 的固定发布流程。
