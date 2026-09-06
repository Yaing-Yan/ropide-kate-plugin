#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Yaing-Yan
# SPDX-License-Identifier: GPL-3.0-or-later
#
# RopIDE for Kate 一键构建 + 测试：
#   ./build.sh                 # 构建 ropidekate.so 与测试工具
#   ./build.sh --crosscheck    # 构建并运行编译器交叉验证（需原版 JS 编译器）
#
# 交叉验证默认在 ../ropide-vscode-plugin 找原版编译器，
# 也可用环境变量 ROPIDE_VSCODE_PLUGIN_DIR 指定路径。
set -euo pipefail
cd "$(dirname "$0")"

BUILD_TYPE="${CMAKE_BUILD_TYPE:-Release}"

cmake -B build -DCMAKE_BUILD_TYPE="$BUILD_TYPE"
cmake --build build -j"$(nproc)"

echo
echo "== 构建完成: build/ropidekate.so =="

if [[ "${1:-}" == "--crosscheck" ]]; then
    VSCODE_DIR="${ROPIDE_VSCODE_PLUGIN_DIR:-../ropide-vscode-plugin}"
    if [[ ! -f "$VSCODE_DIR/media/compiler.js" ]]; then
        echo "找不到原版编译器：$VSCODE_DIR/media/compiler.js" >&2
        echo "请 git clone https://github.com/Yaing-Yan/ropide-vscode-plugin 或设置 ROPIDE_VSCODE_PLUGIN_DIR" >&2
        exit 1
    fi
    echo "== 运行编译器交叉验证（node 原版 vs C++ 移植） =="
    ROPIDE_VSCODE_PLUGIN_DIR="$VSCODE_DIR" \
        CPP_BIN="$PWD/build/ropide-test-compiler" \
        node tests/crosscheck.mjs
fi
