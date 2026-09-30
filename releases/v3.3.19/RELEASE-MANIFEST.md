# v3.3.19 发布清单

日期：2026-09-30。新增 `gpt-6.1-sol`，保留原有四个模型、默认选择和配置行为。

## 源码与验证

- 应用源码提交：`fe3f3f0e593d244188e8be598eec63c759f42acc`。后续发布记录提交仅修改文档。
- Windows 最终构建、自测、`Tests/windows-ui.py` 通过，截图已检查。
- 运行时集成通过：`dist/runtime-test-1dh8Kb/result.json`，Codex `0.158.0-alpha.2.1` 枚举五个模型；使用隔离目录和模拟服务，未验证付费上游真实调用。该验证后仅更改版本信息及发布文档。
- 配置来源回归通过：`dist/source-test-rwqfa4ib/result.json`。
- macOS 工作流 `36654381144` 在应用源码提交通过双架构构建、原生自测、DMG 校验及截图生成；截图已检查。
- macOS ZIP CRC、版本 `3.3.19/build30`、Mach-O `x86_64` 与 `arm64` 已独立核验；下载的构建产物哈希与工作流日志一致。未声明 Intel 实机运行；ad-hoc 签名，未公证。

## SHA-256

| 文件 | SHA-256 |
| --- | --- |
| YilaiCodexSwitcher.exe | 589b434f1297165b90f5c6ca2069f84d7fa12e05fc9106e2cd6bb742301433ca |
| YilaiCodexSwitcher-macOS-universal.dmg | d1ae45386eec4d2df48409fbebdfc66fdfd99dc724703a4746cd0cba2b7a9f4e |
| YilaiCodexSwitcher-v3.3.19-macOS-universal.zip | 8480d054b41ac48296be5cb76b04b714b2b2e0c048cb8bf19453666c5f680fef |

## 发布

已发布：https://github.com/kingduoyu/yilai-codex-switcher/releases/tag/v3.3.19 。GitHub latest 指向 `v3.3.19`，非草稿、非预发布。标签提交 `63addc0`，应用源码与上述构建提交一致。

两个固定名称附件状态均为 uploaded，远端元数据 SHA-256 与上表一致。未回下载 Release 附件。已验证产物复制至 `publish/win-x64` 与 `publish/mac-universal`，固定名称副本哈希核验通过。

