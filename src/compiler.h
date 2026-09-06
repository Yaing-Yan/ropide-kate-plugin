/*
 * SPDX-FileCopyrightText: 2026 Yaing-Yan
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * RopIDE 编译器 / 解析器（移植自 ropide-vscode-plugin 的 media/compiler.js，
 * 后者忠实移植自 rop-ide 的 src/parser.js 与 ropide-python 的 compiler.py）。
 *
 * 输出：
 *   hexChars            最终十六进制字符串（大写）
 *   highlightLines      每个语法 span 的 { type, content } 列表
 *   byteStartPositions  每个字节在 input 字符串中的起始字符位置（最终字节序）
 *   errorCount          语法错误数量
 *   totalBytes          总字节数
 */
#pragma once

#include <QHash>
#include <QString>
#include <QVector>

#include "rop.h"

namespace Rop
{

struct RopSpan {
    QString type;    // 如 "comment" / "gadget,closed" / "value,closed,warning"，空串表示 ';'
    QString content;
};

struct CompileResult {
    QString hexChars;
    // 与 JS 实现一致的扁平映射： gadget/value 每字节 2 个条目 (start,end)，
    // 裸十六进制每字符 1 个条目；请勿"修正"，地址换算依赖该行为。
    QVector<int> charPosInInputMap;
    QVector<QVector<RopSpan>> highlightLines;
    int errorCount = 0;
    QVector<int> byteStartPositions;
    QHash<QString, int> constants;
    QStringList constantOrder; // constants 的插入顺序（供补全列表）
    QHash<QString, QString> anchorSides;
    int totalBytes = 0;
};

CompileResult parseRopInput(const QString &input,
                             const QVector<RopGadget> &gadgets,
                             const QString &leftStartAddress,
                             const QString &rightStartAddress);

} // namespace Rop
