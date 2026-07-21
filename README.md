# PDF Rights Cleaner（PDF 权限清理器）

轻量、便携、完全本地运行的 Windows PDF 权限清理工具。

它用于批量移除“无需密码即可打开，但限制打印、复制或编辑”的 PDF 所有者权限。程序不会猜测或破解打开密码，也不会上传文件。

> 当前版本：**1.1** · 作者：[曹虎男](https://github.com/Hona-Cao)

## 下载

请从 [Releases](https://github.com/Hona-Cao/PDF-Rights-Cleaner/releases) 下载最新版 `PDF-Rights-Cleaner-v1.1-Windows-x64.exe`。这是单文件便携版，无需安装 .NET、Java、Python 或 qpdf。

## 功能

- 批量拖入 PDF 或文件夹，递归扫描文件夹中的 PDF。
- 自动区分权限受限、普通未加密、需要打开密码及损坏文件。
- 待处理列表支持单选、多选、右键菜单和拖出移除；不会删除源文件。
- 结果列表支持单选、多选、批量另存和拖到资源管理器等文件接收窗口。
- 输出先进入可配置的缓存目录，重启后可恢复处理结果。
- 永不覆盖源文件，全程离线运行。
- Per-Monitor DPI Awareness V2，支持不同分辨率与缩放比例。
- 原生 Win32 C++，启动迅速，单文件体积小于 10 MB。

## 支持范围

| PDF 类型 | 处理方式 |
| --- | --- |
| 无需打开密码，但限制打印、复制或编辑 | 支持解除权限限制 |
| 普通未加密 PDF | 标记为无需处理 |
| 需要打开密码 | 不支持，不进行密码猜测 |
| 损坏或无法识别的 PDF | 标记失败并保留源文件 |

修改 PDF 会使该文件已有的数字签名失效。请只处理你拥有或获准修改的文件。

## 列表与拖放

- 两个列表均支持 `Ctrl`/`Shift` 多选和右键菜单。
- 对外拖放由 Windows Shell 生成真实文件数据对象，包含 `CF_HDROP`、Shell ID 列表及复制效果。
- 窗口缩放采用整帧重排和全量重绘，并设置安全最小尺寸，避免标签残影和控件覆盖。

## 从源码构建

需要：

- Visual Studio C++ Build Tools
- Windows SDK
- 官方 qpdf 12.3.2 Windows x64 发行包

将 qpdf 解压到：

```text
third_party/qpdf-12.3.2/qpdf-12.3.2-msvc64
```

然后运行：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\build.ps1 -Clean
```

成品位于 `dist/PDF-Rights-Cleaner.exe`。构建脚本会在文件超过 10 MB 时失败。

## 技术与许可

- 应用源代码采用 [MIT License](LICENSE)。
- PDF 处理引擎为 [qpdf 12.3.2](https://qpdf.sourceforge.io/)，采用 Apache License 2.0。
- qpdf 许可文本已嵌入 EXE，可在软件“关于”窗口中查看。

## 支持项目

如果这个工具对你有帮助，欢迎在仓库右上角点亮 **Star**。后续版本会发布在：

- 项目主页：https://github.com/Hona-Cao/PDF-Rights-Cleaner
- 作者主页：https://github.com/Hona-Cao
