#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Yaing-Yan
# SPDX-License-Identifier: GPL-3.0-or-later
#
# RopIDE for Kate 卸载脚本
#   ./uninstall.sh           # 卸载系统安装
#   ./uninstall.sh --user    # 卸载用户安装
set -euo pipefail

if [[ "${1:-}" == "--user" ]]; then
    FILE="$HOME/.local/lib/qt6/plugins/kf6/ktexteditor/ropidekate.so"
else
    FILE="/usr/lib/qt6/plugins/kf6/ktexteditor/ropidekate.so"
fi

if [[ -f "$FILE" ]]; then
    if [[ "${1:-}" == "--user" ]]; then
        rm -f "$FILE"
    else
        sudo rm -f "$FILE"
    fi
    echo "已删除 $FILE"
else
    echo "未找到 $FILE（无需卸载）"
fi
