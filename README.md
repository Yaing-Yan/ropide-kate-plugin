<p align="center">
  <img src="media/banner.png" alt="RopIDE for Kate" width="100%" />
</p>

# RopIDE for Kate

[![AUTO BUILD](https://github.com/Yaing-Yan/ropide-kate-plugin/actions/workflows/ci.yml/badge.svg)](https://github.com/Yaing-Yan/ropide-kate-plugin/actions/workflows/ci.yml)
[![License: GPL-3.0](https://img.shields.io/badge/License-GPL--3.0-blue.svg)](LICENSE)

A **Kate plugin** built for **`.rop` files** — ROP programs for the **CASIO fx-991 CN X**.
It is a faithful port of [ropide-vscode-plugin](https://github.com/Yaing-Yan/ropide-vscode-plugin):
the editor UI lives in a dockable tool view, while the `.rop` file (a single-line JSON document)
stays open as a normal Kate document and remains the single source of truth.

一个为 **`.rop` 文件**（CASIO fx-991 CN X 的 ROP 程序）打造的 **Kate 插件**，
由 [ropide-vscode-plugin](https://github.com/Yaing-Yan/ropide-vscode-plugin) 移植而来：
整套 IDE 界面位于一个可停靠的工具面板中，`.rop` 文件（单行 JSON）仍作为普通 Kate
文档打开，并始终保持为单一数据源。

A `.rop` file is a single JSON object:

```json
{
  "input": "// ...assembly DSL source...",
  "gadgets": [ { "name": "pop-er0", "addr": "121A8", "desc": "赋值 ER0", "tags": [] } ],
  "leftStartAddress": "E9E0",
  "rightStartAddress": "D710",
  "ideVersion": 100
}
```

## Features / 功能

| | English | 中文 |
| --- | --- | --- |
| 🎨 | DSL syntax highlighting driven by the compiler's own parser (same spans as the web version), warning underline for errors | DSL 语法高亮由编译器解析结果驱动（与网页版同一套 span），错误处波浪线告警 |
| 📍 | **Left / right address gutters**: per-line start address on both sides, current line highlighted | **左右地址栏**：两侧分别显示每行起始地址，当前行高亮 |
| 📊 | Live cursor address `L:xxxx R:xxxx` in the footer | 底部实时显示光标处 `L:xxxx R:xxxx` |
| 🧰 | **Gadgets panel**: search / add / edit / delete, import & export `gadgets.json`, VerF / VerC presets (31 gadgets each), experimental disassembly snippets via `_disas` files | **Gadgets 面板**：搜索/增删改、导入导出 `gadgets.json`、VerF/VerC 内置预设（各 31 条）、实验性 `_disas` 反汇编片段 |
| ⚡ | **Compile**: hexdump with left/right addresses, click a byte to jump to the source position, copy hex / copy hexdump, jump-to-address box | **编译**：带左右地址的 hexdump，点击字节跳回源码位置，复制 hex / hexdump，地址跳转框 |
| 🌍 | **Market**: browse / search programs on [ropide.pages.dev](https://ropide.pages.dev), featured section, unread badge, one-click download (choose a path then it opens), publish with the "expert check" challenge | **程序广场**：浏览/搜索程序、精选分区、未读红点、一键下载（选路径后自动打开）、内行验证发布 |
| 🖥️ | **Overwrite emulator**: write compiled bytes into RAM / write launcher through CasioEmuMsvc's McpPlugin (MCP, port 3001) | **覆写模拟器**：通过 CasioEmuMsvc 的 McpPlugin（MCP，端口 3001）覆写 RAM / launcher |
| ⌨️ | `#` gadget completion, `$` constant/anchor completion, Tab-aligned comments, `Ctrl+/` comment toggle, find & replace inside the DSL editor | `#` gadget 补全、`$` 常量/锚点补全、Tab 对齐注释、`Ctrl+/` 注释切换、DSL 内查找替换 |
| 🌐 | UI language 简体中文 / English (runtime switch) | 界面语言运行时切换 |
| 👋 | Welcome / About dialog, optional on startup | 欢迎/关于页，可设置启动时弹出 |

## AUTO BUILD

Every push is built and tested automatically by GitHub Actions (`.github/workflows/ci.yml`):

1. **Build** in an Arch Linux container against the same Qt6/KF6 versions Kate uses.
2. **Compiler crosscheck** — the original JS compiler (`ropide-vscode-plugin/media/compiler.js`)
   is executed under Node and its output (hex bytes, byte↔source mapping, error counts,
   constants, anchors, highlight spans) is diffed field-by-field against the C++ port over
   [19 test cases](tests/cases.json) including deferred back-patching, anchors on both
   address sides and known edge-case quirks. **19/19 must pass.**
3. **Headless smoke test** — a mini plugin host loads the built `.so` through
   `KPluginFactory`, binds a real `.rop` document and verifies the editor↔document round-trip.
4. Artifacts are uploaded on every build; pushing a tag `v*` automatically creates a
   GitHub Release with the installable tarball.

每次推送都会由 GitHub Actions 自动构建与测试：在 Arch Linux 容器内针对 Kate 同款
Qt6/KF6 编译；用 Node 运行原版 JS 编译器与 C++ 移植版在 19 组用例上**逐字段对拍**
（含延迟回填、双地址锚点、空地址怪癖等，必须 19/19 通过）；再以无头宿主加载 `.so`
做插件加载与文档往返冒烟测试。产物自动上传，推送 `v*` 标签会自动发布 GitHub Release。

## Install / Run

### From source

Requirements / 依赖: Qt 6.5+, KF6 (CoreAddons, Config, I18n, TextEditor, XmlGui), CMake ≥ 3.16.

- Arch Linux: `sudo pacman -S --needed qt6-base kcoreaddons kconfig ki18n ktexteditor kxmlgui cmake make gcc`
- Debian/Ubuntu 24.10+: `sudo apt install qt6-base-dev libkf6coreaddons-dev libkf6config-dev libkf6i18n-dev libkf6texteditor-dev libkf6xmlgui-dev cmake g++`

```bash
./build.sh                # 构建 → build/ropidekate.so
./build.sh --crosscheck   # 构建 + 编译器交叉验证（需 ../ropide-vscode-plugin）
sudo ./install.sh         # 系统安装（或 ./install.sh --user 免 root）
```

Then start Kate → *Settings → Configure Kate → Plugins* → enable **RopIDE** →
open any `.rop` file and dock the **RopIDE** tool view.

启动 Kate → *设置 → 配置 Kate → 插件* → 勾选 **RopIDE** → 打开任意 `.rop` 文件，
停靠 **RopIDE** 工具面板即可开始编辑。

### One-click, no git clone / 免克隆一键安装

```bash
curl -sSL https://raw.githubusercontent.com/Yaing-Yan/ropide-kate-plugin/main/simply-plugin.sh -o /tmp/simply-plugin.sh && bash /tmp/simply-plugin.sh
```

Or download the tarball from [Releases](https://github.com/Yaing-Yan/ropide-kate-plugin/releases)
and run `./install.sh --so ropidekate.so`, or grab the CI artifact from the
AUTO BUILD runs.

### Uninstall / 卸载

```bash
sudo ./uninstall.sh       # 或 ./uninstall.sh --user
```

## ⚠️ Overwrite feature: the emulator must be started this way

"Overwrite RAM / overwrite launcher" writes memory through CasioEmuMsvc's **McpPlugin**
(MCP, `http://127.0.0.1:3001`), so the emulator must be started **from its own directory**
(on Linux/macOS the plugin loader only scans the current working directory):

```bash
cd /path/to/CasioEmuMsvc-mcp          # enter the directory that contains the binary
./CasioEmuMsvc ../models/fx991cnxfVirtual
```

> Verify: visit `http://127.0.0.1:3001/health` — a `{"status":"ok",...}` response means MCP is ready.
> If the port is not 3001, change it in the RopIDE tool view's *Settings* tab.

## Compile rules

The C++ compiler is a faithful port of [rop-ide](https://github.com/WulanOVO/rop-ide)'s
`src/parser.js` and ropide-python's `compiler.py` (same as the VS Code plugin):

- `#gadget;` encodes as 4 bytes (`h3 = ("0" if allow00 else "3") + addr[0]`, `h4 = "00"/"30"`);
  `#-gadget;` forbids `00` bytes.
- `[expression]` encodes as little-endian 2 bytes; forward references to `$constants`
  are deferred and back-patched.
- `<anchor>` records the current byte-stream address (right base; `<-anchor>` uses the left base).
- Raw hex characters merge directly into the byte stream.
- Gutter / status addresses = start address + byte offset of the line / cursor.

The port is kept byte-identical to the original JS implementation — enforced in CI by
the crosscheck described above. 移植与原版 JS 实现逐字节一致，由 CI 对拍强制保证。

## Design notes / 设计说明

Kate (KTextEditor) has no "custom editor" API like VS Code's `CustomTextEditorProvider`,
so the plugin maps the original webview UI onto a **tabbed tool view**
(Editor / Compile / Gadgets / Market / Disas / Settings) instead:

- The Kate document holding the `.rop` JSON is never replaced by the UI — every edit in
  the DSL editor is re-serialized (compact single-line JSON, same as the web version) and
  written back into the document buffer; external edits to the document re-sync into the tool view.
- The DSL editor is a plain `QPlainTextEdit` with two painted address gutters, a
  compiler-span-driven `QSyntaxHighlighter`, a custom completion popup, find/replace bar,
  Tab-comment alignment and rich hover tooltips — mirroring the original webview behavior.
- The menu *RopIDE* (New / Open / Compile / Gadgets / Market / About) is merged via an
  XMLGUI client whose `.rc` file ships **inside the plugin binary** as a Qt resource
  (`:/kxmlgui5/ropidekate/ropidekateui.rc`), so a plugin install is a single `.so` file.

 Kate 没有 VS Code 那样的自定义编辑器 API，因此插件把原 Webview 界面映射为一个带六个
 标签页的工具面板；`.rop` JSON 文档始终是数据源：DSL 面板编辑 → 紧凑单行 JSON 写回文档，
 文档外部改动 → 自动重新同步。菜单合并所需的 `.rc` 文件以 Qt 资源内嵌在插件 `.so` 中，
 安装只需拷贝一个文件。

> Differences vs the VS Code plugin: the per-line right-side gutter lives on the right edge
> of the DSL editor (not the whole Kate view), and the Tab-comment alignment / completion /
> hover apply inside the RopIDE DSL editor. 与 VS Code 版的差异：右侧地址栏位于 DSL 编辑器
> 右缘；Tab 对齐注释、补全、悬停提示均在 RopIDE 面板内的编辑器中生效。

## Directory structure / 目录结构

```
ropide-kate-plugin/
├── CMakeLists.txt
├── build.sh                # 一键构建（--crosscheck 跑对拍）
├── install.sh              # 系统安装（--user 免 root）
├── uninstall.sh
├── simply-plugin.sh        # 免 git clone 一键安装/卸载
├── icon.png / icon.svg
├── media/banner.png
├── data/
│   ├── verf.json / verc.json   # VerF / VerC gadgets 内置预设
│   ├── ropidekateui.rc         # XMLGUI 菜单（以 QRC 内嵌进 .so）
│   └── resources.qrc
├── src/
│   ├── rop.cpp/.h              # .rop JSON 解析/序列化、disas 解析（移植 rop.ts）
│   ├── compiler.cpp/.h         # 编译器/解析器（忠实移植 media/compiler.js）
│   ├── settings.cpp/.h         # 全局设置（ropide.* 配置项对应）
│   ├── i18n.h + i18n_strings.h # 运行时中英文案（转换自 editor.js STR 表）
│   ├── market.cpp/.h           # 程序广场 API（ropide.pages.dev）
│   ├── emu.cpp/.h              # CasioEmuMsvc MCP 客户端（覆写内存）
│   ├── presets.cpp/.h          # VerF/VerC 预设加载
│   ├── ropcodeeditor.cpp/.h    # DSL 编辑器（地址栏/高亮/补全/悬停/查找）
│   ├── roptoolview.cpp/.h      # 工具面板（六标签页 + 文档同步）
│   ├── ropideplugin.cpp/.h     # KTextEditor 插件入口 + XMLGUI 菜单
│   └── ropidekate.json         # 插件元数据
└── tests/
    ├── cases.json              # 编译器对拍用例（19 组）
    ├── crosscheck.mjs          # node 原版 vs C++ 对拍脚本
    ├── test_compiler.cpp       # C++ 侧对拍驱动
    └── test_host.cpp           # 无头宿主冒烟测试
```

## Acknowledgements / 致谢

- **贴吧 @wlyibo** —— RopIDE 作者、[ropide.pages.dev](https://ropide.pages.dev) 网页版作者。他的 RopIDE 项目**推动了全民 ROP**，本插件的一切都建立在它之上。
- **rop-ide**（语法高亮 / 程序广场 / 编译逻辑参考）：https://github.com/WulanOVO/rop-ide
- 模拟器基础：贴吧 @噶么prince 的 CasioEmuMsvc 源码项目
- [ropide-vscode-plugin](https://github.com/Yaing-Yan/ropide-vscode-plugin) —— 本插件的直接移植来源

## License / 许可证

GPL-3.0 —— same as ropide-vscode-plugin. See [LICENSE](LICENSE).
