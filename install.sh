#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Yaing-Yan
# SPDX-License-Identifier: GPL-3.0-or-later
#
# RopIDE for Kate 安装脚本
#   ./install.sh            # 系统安装（需要 sudo，推荐）
#   ./install.sh --user     # 用户安装（免 root，需设置 QT_PLUGIN_PATH）
#   ./install.sh --so FILE  # 从指定 .so 安装（默认 build/ropidekate.so 或已下载的发布包）
set -euo pipefail
cd "$(dirname "$0")"

MODE="${1:-}"
SO_FILE="${SO:-}"

# 定位 .so
for cand in "$SO_FILE" build/ropidekate.so ropidekate.so; do
    if [[ -n "$cand" && -f "$cand" ]]; then
        SO_FILE="$cand"
        break
    fi
done
if [[ -z "$SO_FILE" || ! -f "$SO_FILE" ]]; then
    echo "未找到 ropidekate.so，请先 ./build.sh 或解压发布包后用 --so 指定。" >&2
    exit 1
fi

if [[ "$MODE" == "--user" ]]; then
    DEST_DIR="$HOME/.local/lib/qt6/plugins/kf6/ktexteditor"
    mkdir -p "$DEST_DIR"
    install -m 644 "$SO_FILE" "$DEST_DIR/ropidekate.so"
    echo "已安装到 $DEST_DIR/ropidekate.so"
    echo
    echo "用户安装需要让 Qt 能扫描到该目录，请把下面一行加入会话环境"
    echo "（~/.config/plasma-workspace/env/*.sh 或 ~/.xprofile 等）："
    echo
    echo "  export QT_PLUGIN_PATH=\"\$HOME/.local/lib/qt6/plugins\${QT_PLUGIN_PATH:+:\$QT_PLUGIN_PATH}\""
    echo
    echo "然后重新登录并启动 Kate。"
else
    DEST_DIR="/usr/lib/qt6/plugins/kf6/ktexteditor"
    sudo install -Dm 644 "$SO_FILE" "$DEST_DIR/ropidekate.so"
    echo "已安装到 $DEST_DIR/ropidekate.so"
fi

echo
echo "启用：启动 Kate → 设置 → 配置 Kate → 插件 → 勾选「RopIDE」。"
echo "卸载：./uninstall.sh（加 --user 对应用户安装）。"
