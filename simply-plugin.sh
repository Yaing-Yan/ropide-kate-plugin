#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Yaing-Yan
# SPDX-License-Identifier: GPL-3.0-or-later
#
# 免 git clone 一键安装/卸载（下载 GitHub Release 最新产物）：
#   curl -sSL https://raw.githubusercontent.com/Yaing-Yan/ropide-kate-plugin/main/simply-plugin.sh -o /tmp/simply-plugin.sh && bash /tmp/simply-plugin.sh
#
# 下载优先使用 releases/latest/download 直链（不占 GitHub API 配额）；
# 仅在直链失败时回退到 API，并自动附带 GITHUB_TOKEN（如已设置）避免限流 403。
set -euo pipefail

REPO="Yaing-Yan/ropide-kate-plugin"
ASSET="ropide-kate-plugin.tar.gz"
DEST="/usr/lib/qt6/plugins/kf6/ktexteditor/ropidekate.so"

echo "== RopIDE for Kate 免克隆安装/卸载 =="
echo "  1 安装（系统级，需要 sudo）"
echo "  2 卸载（系统级）"
read -r -p "选择 [1/2]: " choice

if [[ "$choice" == "2" ]]; then
    sudo rm -f "$DEST"
    echo "已卸载。"
    exit 0
fi
if [[ "$choice" != "1" ]]; then
    echo "已取消。"
    exit 0
fi

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

fetch() {
    # fetch <url> <输出文件>
    if command -v curl >/dev/null 2>&1; then
        curl -fSL --retry 3 --connect-timeout 15 "$1" -o "$2"
    else
        wget -T 15 --tries=3 -O "$2" "$1"
    fi
}

download_release() {
    # 1) 直链（推荐，无 API 配额限制）
    if fetch "https://github.com/$REPO/releases/latest/download/$ASSET" "$TMP/pkg.tar.gz"; then
        return 0
    fi
    echo "直链下载失败，尝试 GitHub API…" >&2
    # 2) 回退 API；设置了 GITHUB_TOKEN 则认证（未认证 API 易触发 403 限流）
    local auth=()
    [[ -n "${GITHUB_TOKEN:-}" ]] && auth=(-H "Authorization: Bearer $GITHUB_TOKEN")
    local url=""
    if fetch "https://api.github.com/repos/$REPO/releases/latest" "$TMP/rel.json" 2>/dev/null; then
        url="$(grep -oP "\"browser_download_url\":\s*\"\K[^\"]+$ASSET\"" "$TMP/rel.json" | head -1 || true)"
    fi
    if [[ -z "$url" ]]; then
        echo "无法自动获取下载地址（GitHub API 可能限流 403；设置 GITHUB_TOKEN 可提高限额）。" >&2
        echo "请手动下载 https://github.com/$REPO/releases 里的 $ASSET 后运行：" >&2
        echo "  ./install.sh --so ropidekate.so" >&2
        return 1
    fi
    fetch "$url" "$TMP/pkg.tar.gz"
}

echo "获取最新 Release…"
download_release
tar xzf "$TMP/pkg.tar.gz" -C "$TMP"

sudo install -Dm 644 "$TMP/ropidekate.so" "$DEST"
echo
echo "安装完成。启动 Kate → 设置 → 配置 Kate → 插件 → 勾选「RopIDE」。"
