# 架构文档与发布前复核

本轮不修改 app/core/驱动的业务行为，只补充当前架构、数据链路与技术取舍，整理公开文档入口，并复核待发布内容。

- `preflight.json`：16 个业务/构建/测试文件与前轮被测工作树 SHA-256 一致，39 个本地链接通过；记录发布前 main 指向与保护状态。
- `windows-regression.log`：发布前重新执行的 Windows Qt 四项 CTest，全部通过。
- `mermaid-validation.log`：两张 flowchart 通过 Mermaid 11.12.0 解析。
- `validate-diagrams.mjs`：该次解析脚本；将它复制到安装依赖的目录运行。
- `file-manifest.json`：公开文档整理后的工作树源文件 SHA-256。Git 文本换行规范化可能使下载文件的逐字节指纹不同，应结合 Git 提交/文件与原始测试指纹核对。

上一阶段 `2026-11-17-local-monitor/source-manifest.json` 是测试时的快照，其中旧文档路径保留用于追溯。原始日志、失败截图和源码指纹不因公开入口调整而改写。未参与测试的本地工作区状态清单不进入发布内容。

Mermaid 检查命令：

```powershell
npm install --prefix build-doc-checks --ignore-scripts --no-audit --no-fund mermaid@11.12.0 jsdom@26.1.0
Copy-Item evidence/2026-11-17-publication/validate-diagrams.mjs build-doc-checks/validate.mjs
node build-doc-checks/validate.mjs docs/overview/architecture.md
```

Node 依赖只用于文档解析，不属于主程序运行或构建依赖。本次没有目标板部署或硬件测试。
