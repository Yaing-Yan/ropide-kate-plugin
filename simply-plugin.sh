#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Yaing-Yan
# SPDX-License-Identifier: GPL-3.0-or-later
#
# 免 git clone 一键安装/卸载（下载 GitHub Release 最新产物）：
#   curl -sSL https://raw.githubusercontent.com/Yaing-Yan/ropide-kate-plugin/main/simply-plugin.sh -o /tmp/simply-plugin.sh && bash /tmp/simply-plugin.sh
set -euo pipefail

REPO="Yaing-Yan/ropide-kate-plugin"
API="https://api.github.com/repos/$REPO/releases/latest"

echo "== RopIDE for Kate 免克隆安装/卸载 =="
echo "  1 安装（系统级，需要 sudo）"
echo "  2 卸载（系统级）"
read -r -p "选择 [1/2]: " choice

if [[ "$choice" == "2" ]]; then
    sudo rm -f /usr/lib/qt6/plugins/kf6/ktexteditor/ropidekate.so
    echo "已卸载。"
    exit 0
fi

if [[ "$choice" != "1" ]]; then
    echo "已取消。"
    exit 0
fi

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

echo "获取最新 Release…"
URL="$(curl -fsSL "$API" | grep -oP '"browser_download_url":\s*"\K[^"]+\.tar\.gz' | head -1)"
if [[ -z "$URL" ]]; then
    echo "未找到 Release 产物，请到 https://github.com/$REPO/releases 手动下载。" >&2
    exit 1
fi

echo "下载 $URL"
curl -fsSL "$URL" -o "$TMP/pkg.tar.gz"
tar xzf "$TMP/pkg.tar.gz" -C "$TMP"

sudo install -Dm 644 "$TMP/ropidekate.so" /usr/lib/qt6/plugins/kf6/ktexteditor/ropidekate.so
echo
echo "安装完成。启动 Kate → 设置 → 配置 Kate → 插件 → 勾选「RopIDE」。"
