# v3.4.1 发布清单

日期：2026-09-30。模型列表只显示一次名称；右上角版本与黄色新版提示保持单行。删除旧对话修复入口、实现及专用 SQLite 编译依赖。macOS 两个配置动作改为等宽按钮。

## 源码与验证

- 发布标签及最终应用源码：`844124c1831c3ec1c1b5961d5c8fac7600bafec4`；源码树：`5fc7ddb8d0aefacdc35886032ae0a0b003c27361`。后续提交仅补文档与验收记录，不改变发布程序。
- Windows 从最终候选重新构建，版本资源 `3.4.1.0`，原生自测及 `Tests/windows-ui.py` 通过。模型名称/count、单行版本、已删除控件、两种可恢复配置失败、隔离配置不变及无进程日志均已检查。
- Windows 更新辅助程序三项通过：正常替换并重开、错误哈希拒绝、启动失败恢复并重开。证据：`dist/update-helper-c6e395a3027547fd87bc86529633088b`。
- 运行时集成通过：Codex `0.159.0`，证据 `dist/runtime-test-NFKLhG/result.json`；配置来源回归通过：`dist/source-test-vf16d5qt/result.json`。共享更新协议及独立模型更新测试通过，配置、登录和历史保留测试通过。测试使用模拟服务和隔离目录，未声明付费上游真实模型调用通过。
- 上述核心回归在删除历史修复模块后运行；其后仅调整 macOS 界面。已核对最终提交的 Windows、共享模块及测试输入与核心回归提交一致；Windows 最终程序另行重跑自测和 GUI 检查。
- macOS 最终工作流 [36730834718](https://github.com/kingduoyu/yilai-codex-switcher/actions/runs/36730834718) 在最终发布提交通过 universal 构建、自测、DMG 校验、实际 helper 安装与重开、保留备份及候选移动失败后的恢复重开。证据：`dist/mac-v3.4.1-36730834718/macos-update-result.json`，`passed=true`、`source_app_unchanged=true`，隔离 Codex 数据不变。
- macOS ZIP CRC、版本 `3.4.1/build32`、Mach-O `x86_64` 与 `arm64` 核验通过。工作流附件 ID `11105506091`，归档 SHA-256：`7cda21b7121e342c9e375fbdf3a21e38a5c7fa63b5d63dbc4f4926cb04056a4f`；程序 SHA-256：`18187b4f41dd9ee7c35643e3ce8023109f729210caae258def6be77ca8099d00`。使用最终工作流产物，未使用上一轮界面产物。
- Windows 与 macOS 原生截图、完整及局部对比已检查，结论见 `design-qa.md`：`passed`。未单独覆盖其他 Windows DPI/字体环境，未声明两种 Mac 架构分别实机测试；macOS 为 ad-hoc 签名、未公证。

## 最终附件

| 固定附件 | 字节数 | SHA-256 |
| --- | ---: | --- |
| YilaiCodexSwitcher.exe | 4272128 | 2063aebb757b09ece2e9ddb4b260024a60e27150ab3c3bd00218aa5379da26cb |
| YilaiCodexSwitcher-macOS-universal.dmg | 5032619 | 9afb6e2170db83f1e2dd850bdd7ea6670ffcd0137dcb26fdead69410d20ae4af |
| YilaiCodexSwitcher-macOS-universal.zip | 4328476 | 6488c5fe2d01e7340baeeaa8b101e42088268f1081be67071e0873fe6907b4d3 |

## 发布与模型频道

- 正式 [Release v3.4.1](https://github.com/kingduoyu/yilai-codex-switcher/releases/tag/v3.4.1)，ID `400151926`，发布时间 `2026-09-30T14:55:15Z`。发布后核验 latest 为 `v3.4.1`，非草稿、非预发布；三个固定附件均为 uploaded，大小和非空 digest 与上表一致，下载地址属于固定仓库和标签。未回下载 Release 安装包。
- 本轮按标签读取草稿返回 404，发布前的草稿元数据门禁未完成；发布后的完整元数据核验已通过，最终三个附件与本地一致。已将实际 Release ID 查询和命令失败立即停止写入两级 README，后续不得在草稿核验失败后继续发布。
- 已验证最终附件复制到 `publish/win-x64` 和 `publish/mac-universal`，固定及带版本文件的哈希均一致。
- 独立模型频道本轮不变，revision `1`、五个模型，目录指向不可变提交 `12d09d97dd7fa66c8a863f579a8ec39829bae0cd`，SHA-256：`710f2306befcf3a31fed58373ee397362e36c2dbd1132bac3c1c600be96ca966`。新增模型继续使用 `Tools/Publish-ModelCatalog.ps1 -Publish`，不要求软件发版；用户在功能区点击“更新模型”校验并写入。

## 教程同步

- 唯一入口 `https://api.yilai-ai.com/docs/tutorial/`；管理及公开设置中的 `usage-guide`（配置教程）均回读为此地址，无版本参数或第二个教程菜单。本次不需修改菜单。
- 中转站目录下的唯一源文件 `01-首充优惠与新手引导/教程/网页成品/index.html` 已显示 `v3.4.1`，更新说明指向本次标签；Windows/macOS 的固定 latest/download 链接保留。
- 服务器 `/srv/sub2api-docs/tutorial/index.html` 经基线检查、可读备份、临时文件哈希核验后原子替换。固定入口及兼容 `/docs/tutorial/index.html` 均 HTTP 200，返回字节与本地一致，SHA-256：`33685ec75ebb59b3012a4a034485e6a2dd5fdf15bc1f464e17a6ed0821507472`。发布后再次核验两个页面及公开菜单通过。
- 原页面备份：`/root/tutorial-backups/index-before-v3.4.1-20260930-844124c.html`，SHA-256：`2d55ba56cdf0c7b68bad6c9abca12088485f8a09b66415a3faafed58822913cb`。本轮未修改应用、数据库或 Caddy，未重启服务；页面部署的临时 ERR 回退保护在验收后已撤销。
- 软件发布、三个附件核验及唯一教程同步已纳入两级 README 固定流程。
