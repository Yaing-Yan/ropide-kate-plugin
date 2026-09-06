/*
 * SPDX-FileCopyrightText: 2026 Yaing-Yan
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "compiler.h"

#include <QRegularExpression>

#include <algorithm>

namespace Rop
{

namespace
{

// JS 中 parseInt(value, 16)：parseInt 在基数 16 下接受 "0x" 前缀，仅在合法 hex
// 前缀上调用（调用点均已先行正则校验），失败视为 NaN。
static bool jsHexInt(const QString &s, long long &out)
{
    QString t = s;
    if (t.startsWith(QLatin1String("0x"), Qt::CaseInsensitive)) {
        t.remove(0, 2);
    }
    if (t.isEmpty()) {
        return false;
    }
    bool ok = false;
    const long long v = t.toLongLong(&ok, 16);
    if (!ok) {
        return false;
    }
    out = v;
    return true;
}

struct EvalResult {
    long long value = 0;
    bool hasErrors = false;
    bool deferred = false;
};

// 表达式求值：inner 按单个空格切分，$常量 可前向引用（allowUndefinedAsDeferred）。
EvalResult evalExpression(const QString &inner, bool allowUndefinedAsDeferred, const CompileResult &ctx)
{
    EvalResult r;
    long long value = 0x0000;
    QString symbol = QStringLiteral("+");

    const QStringList parts = inner.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    for (const QString &part : parts) {
        if (part.startsWith(QLatin1Char('$'))) {
            const QString constantName = part.mid(1);
            const auto it = ctx.constants.constFind(constantName);
            if (it != ctx.constants.constEnd()) {
                if (symbol == QLatin1String("+")) {
                    value += it.value();
                } else if (symbol == QLatin1String("-")) {
                    value -= it.value();
                } else {
                    r.hasErrors = true;
                    break;
                }
            } else {
                if (allowUndefinedAsDeferred) {
                    r.deferred = true;
                    // value += 0 / -= 0，与 JS 保持一致（无效符号仍报错）
                    if (symbol != QLatin1String("+") && symbol != QLatin1String("-")) {
                        r.hasErrors = true;
                        break;
                    }
                } else {
                    r.hasErrors = true;
                    break;
                }
            }
            symbol = QString();
        } else if (part == QLatin1String("+") || part == QLatin1String("-")) {
            if (symbol.isEmpty()) {
                symbol = part;
            } else {
                r.hasErrors = true;
                break;
            }
        } else {
            static const QRegularExpression numberRe(QStringLiteral("^-?[0-9a-fA-F]+$"));
            if (!numberRe.match(part).hasMatch()) {
                r.hasErrors = true;
                break;
            }
            const long long v = part.toLongLong(nullptr, 16);
            if (symbol == QLatin1String("+")) {
                value += v;
            } else if (symbol == QLatin1String("-")) {
                value -= v;
            } else {
                r.hasErrors = true;
                break;
            }
            symbol = QString();
        }
    }

    if (!symbol.isEmpty()) {
        r.hasErrors = true;
    } else if (!(allowUndefinedAsDeferred && r.deferred) && (value > 0xffff || value < -0x8000)) {
        r.hasErrors = true;
    }
    if (value < 0) {
        value = 0xffff + value + 1; // 负数转补码
    }
    r.value = value;
    return r;
}

void pushHexChars(CompileResult &r, const QString &hex, int startPosInLine, int endPosInLine, int repeatBytes)
{
    if (hex.isEmpty()) {
        return;
    }
    r.hexChars += hex.toUpper();
    for (int k = 0; k < repeatBytes; k++) {
        r.charPosInInputMap.push_back(startPosInLine);
        r.charPosInInputMap.push_back(endPosInLine);
    }
}

// gadget.addr 是 5 位十六进制字符串；极端情况下（如空串）JS 会拼出 "undefined"，
// 这里原样复刻该行为以保持输出一致。
static QString jsCharAt(const QString &s, int i)
{
    return i >= 0 && i < s.size() ? QString(s.at(i)) : QStringLiteral("undefined");
}

static QString jsSlice(const QString &s, int a, int b)
{
    if (a >= s.size() || a >= b) {
        return QString();
    }
    return s.mid(a, b - a);
}

static bool hexDigit(QChar c)
{
    return (c >= QLatin1Char('0') && c <= QLatin1Char('9'))
        || (c >= QLatin1Char('a') && c <= QLatin1Char('f'))
        || (c >= QLatin1Char('A') && c <= QLatin1Char('F'));
}

// JS 的 s.split(/(\s*=\s*)/) 语义：按 "=" 切分并把捕获组（连同两侧空白）保留在结果里。
// Qt 的 QString::split(QRegularExpression) 不保留捕获组，因此手动实现。
static QStringList jsSplitAssign(const QString &s)
{
    QStringList out;
    int pos = 0;
    while (pos <= s.size()) {
        const int eq = s.indexOf(QLatin1Char('='), pos);
        if (eq < 0) {
            break;
        }
        int start = eq;
        while (start > pos && s.at(start - 1).isSpace()) {
            start--;
        }
        int end = eq + 1;
        while (end < s.size() && s.at(end).isSpace()) {
            end++;
        }
        out << s.mid(pos, start - pos);
        out << s.mid(start, end - start);
        pos = end;
    }
    out << s.mid(pos);
    return out;
}

} // namespace

CompileResult parseRopInput(const QString &input,
                             const QVector<RopGadget> &gadgets,
                             const QString &leftStartAddress,
                             const QString &rightStartAddress)
{
    // 与 JS 版一致：空地址按 "0" 处理
    const QString leftBase = leftStartAddress.isEmpty() ? QStringLiteral("0") : leftStartAddress;
    const QString rightBase = rightStartAddress.isEmpty() ? QStringLiteral("0") : rightStartAddress;

    CompileResult r;
    const QStringList lines = input.split(QLatin1Char('\n'));

    struct DeferredPatch {
        int startHexIndex;
        QString expression;
        int startPosInLine;
        int endPosInLine;
        int bytesToInsert;
    };
    struct HighlightPatch {
        int lineIndex;
        int spanIndex;
        QString expression;
    };
    QVector<DeferredPatch> deferredValuePatches;
    QVector<HighlightPatch> deferredHighlightPatches;

    int posInInput = 0;

    const QString otherStop = QStringLiteral("0123456789abcdefABCDEF/$#[<");
    // JS \s 为 Unicode 空白集合（含 \ufeff）
    auto isJsSpace = [](QChar c) {
        return c == QChar(0xFEFF) || c.isSpace();
    };

    for (int lineIndex = 0; lineIndex < lines.size(); lineIndex++) {
        const QString &line = lines.at(lineIndex);
        const int lineStartPosInInput = posInInput;
        QVector<RopSpan> spans;
        auto pushSpan = [&spans](const QString &type, const QString &content) {
            spans.push_back(RopSpan{type, content});
        };

        int i = 0;
        while (i < line.size()) {
            const QChar ch = line.at(i);
            const int charPosInInput = lineStartPosInInput + i;

            // 注释 //...
            if (ch == QLatin1Char('/') && i + 1 < line.size() && line.at(i + 1) == QLatin1Char('/')) {
                pushSpan(QStringLiteral("comment"), line.mid(i));
                break;
            }

            // 常量 $a = 0x...;
            else if (ch == QLatin1Char('$')) {
                int j = i + 1;
                while (j < line.size() && line.at(j) != QLatin1Char(';')) {
                    j++;
                }
                const bool hasSemicolon = j < line.size() && line.at(j) == QLatin1Char(';');
                const QString constantContent = line.mid(i, hasSemicolon ? j + 1 - i : j - i);

                if (hasSemicolon) {
                    QString constantStr = line.mid(i + 1, j - (i + 1));
                    constantStr.remove(QRegularExpression(QStringLiteral("\\s+")));
                    const QStringList parts = constantStr.split(QLatin1Char('='));
                    if (parts.size() == 2) {
                        long long intValue = 0;
                        static const QRegularExpression valueRe(QStringLiteral("^-?[0-9a-fA-F]+$"));
                        const bool valid = valueRe.match(parts.at(1)).hasMatch()
                            && jsHexInt(parts.at(1), intValue) && intValue <= 0xffff && intValue >= -0x8000;
                        if (valid && r.constants.contains(parts.at(0))) {
                            r.errorCount++;
                            pushSpan(QStringLiteral("constant,name,warning"), constantContent);
                            i = j + 1;
                            continue;
                        }
                        if (valid) {
                            r.constants.insert(parts.at(0), (int)intValue);
                            r.constantOrder.append(parts.at(0));
                        } else {
                            r.errorCount++;
                            pushSpan(QStringLiteral("constant,value,warning"), constantContent);
                            i = j + 1;
                            continue;
                        }
                    }

                    const QStringList partsForHighlight = jsSplitAssign(constantContent);
                    if (partsForHighlight.size() == 3) {
                        pushSpan(QStringLiteral("constant,name"), partsForHighlight.at(0));
                        pushSpan(QStringLiteral("constant,equal"), partsForHighlight.at(1));
                        QString v = partsForHighlight.at(2);
                        v.chop(1);
                        pushSpan(QStringLiteral("constant,value"), v);
                        pushSpan(QString(), QStringLiteral(";"));
                    } else {
                        r.errorCount++;
                        pushSpan(QStringLiteral("constant,name,warning"), constantContent);
                    }
                } else {
                    r.errorCount++;
                    pushSpan(QStringLiteral("constant,name"), constantContent);
                }

                i = j + (hasSemicolon ? 1 : 0);
            }

            // gadget #...;
            else if (ch == QLatin1Char('#')) {
                int j = i + 1;
                while (j < line.size() && line.at(j) != QLatin1Char(';') && line.at(j) != QLatin1Char(' ')) {
                    j++;
                }
                const bool hasSemicolon = j < line.size() && line.at(j) == QLatin1Char(';');
                const QString gadgetContent = line.mid(i, hasSemicolon ? j + 1 - i : j - i);

                if (hasSemicolon) {
                    QString gadgetName = gadgetContent.mid(1, gadgetContent.size() - 2);
                    const bool allow00 = !gadgetName.startsWith(QLatin1Char('-'));
                    if (!allow00) {
                        gadgetName.remove(0, 1);
                    }

                    const RopGadget *gadget = nullptr;
                    for (const RopGadget &g : gadgets) {
                        if (g.name == gadgetName) {
                            gadget = &g;
                            break;
                        }
                    }
                    if (!gadget) {
                        r.errorCount++;
                        pushSpan(QStringLiteral("gadget,warning"), gadgetContent);
                    } else {
                        pushSpan(QStringLiteral("gadget,closed"), gadgetContent);

                        const QString addr = gadget->addr;
                        QString hex;
                        hex += jsSlice(addr, 3, 5);
                        if (hex == QLatin1String("00") && !allow00) {
                            hex = QStringLiteral("01");
                        }
                        hex += jsSlice(addr, 1, 3);
                        hex += (allow00 ? QStringLiteral("0") : QStringLiteral("3")) + jsCharAt(addr, 0);
                        hex += allow00 ? QStringLiteral("00") : QStringLiteral("30");
                        pushHexChars(r, hex, charPosInInput, lineStartPosInInput + j, 4);
                    }
                } else {
                    pushSpan(QStringLiteral("gadget"), gadgetContent);
                }

                i = j + (hasSemicolon ? 1 : 0);
            }

            // 数值块 [...]
            else if (ch == QLatin1Char('[')) {
                int j = i + 1;
                while (j < line.size() && line.at(j) != QLatin1Char(']')) {
                    j++;
                }
                const bool hasBracket = j < line.size() && line.at(j) == QLatin1Char(']');
                const QString valueContent = line.mid(i, hasBracket ? j + 1 - i : j - i);

                if (hasBracket) {
                    const QString inner = line.mid(i + 1, j - (i + 1));
                    const EvalResult firstPass = evalExpression(inner, true, r);
                    const bool deferred = firstPass.deferred || inner.contains(QLatin1Char('$'));

                    if (firstPass.hasErrors) {
                        pushSpan(QStringLiteral("value,closed,warning"), valueContent);
                        i = j + 1;
                        continue;
                    }
                    pushSpan(QStringLiteral("value,closed"), valueContent);

                    const QString addrStr = QString::number((ulong)firstPass.value, 16).toUpper().rightJustified(4, QLatin1Char('0'));
                    const QString littleEndian = addrStr.mid(2, 2) + addrStr.mid(0, 2);

                    if (!deferred) {
                        pushHexChars(r, littleEndian, charPosInInput, lineStartPosInInput + j, 2);
                    } else {
                        deferredValuePatches.push_back(
                            DeferredPatch{(int)r.hexChars.size(), inner, charPosInInput, lineStartPosInInput + j, 2});
                        deferredHighlightPatches.push_back(
                            HighlightPatch{lineIndex, (int)spans.size() - 1, inner});
                    }
                } else {
                    pushSpan(QStringLiteral("value"), valueContent);
                }

                i = j + (hasBracket ? 1 : 0);
            }

            // 地址锚点 <...> / <-...>
            else if (ch == QLatin1Char('<')) {
                int j = i + 1;
                while (j < line.size() && line.at(j) != QLatin1Char('>') && line.at(j) != QLatin1Char(' ')) {
                    j++;
                }
                const bool hasClose = j < line.size() && line.at(j) == QLatin1Char('>');
                const QString anchorContent = line.mid(i, hasClose ? j + 1 - i : j - i);

                if (hasClose) {
                    pushSpan(QStringLiteral("anchor,closed"), anchorContent);

                    QString anchorName = line.mid(i + 1, j - (i + 1));
                    long long addrStart = 0;
                    if (!jsHexInt(rightBase, addrStart)) {
                        addrStart = 0;
                    }
                    QString side = QStringLiteral("right");
                    if (anchorName.startsWith(QLatin1Char('-'))) {
                        anchorName.remove(0, 1);
                        if (!jsHexInt(leftBase, addrStart)) {
                            addrStart = 0;
                        }
                        side = QStringLiteral("left");
                    }

                    int deferredBytesBeforeAnchor = 0;
                    for (const DeferredPatch &p : deferredValuePatches) {
                        if (p.startHexIndex <= r.hexChars.size()) {
                            deferredBytesBeforeAnchor += p.bytesToInsert;
                        }
                    }
                    const long long addr = addrStart + (r.hexChars.size() + 1) / 2 + deferredBytesBeforeAnchor;
                    r.constants.insert(anchorName, (int)addr);
                    if (!r.constantOrder.contains(anchorName)) {
                        r.constantOrder.append(anchorName);
                    }
                    r.anchorSides.insert(anchorName, side);
                } else {
                    pushSpan(QStringLiteral("anchor"), anchorContent);
                }

                i = j + (hasClose ? 1 : 0);
            }

            // 裸十六进制字符
            else if (hexDigit(ch)) {
                int j = i + 1;
                while (j < line.size() && (hexDigit(line.at(j)) || isJsSpace(line.at(j)))) {
                    j++;
                }
                pushSpan(QStringLiteral("hex"), line.mid(i, j - i));

                for (int k = i; k < j; k++) {
                    const QChar c = line.at(k);
                    if (hexDigit(c)) {
                        r.hexChars += QString(c).toUpper();
                        r.charPosInInputMap.push_back(lineStartPosInInput + k);
                    }
                }
                i = j;
            }

            // 其他字符
            else {
                int j = i + 1;
                while (j < line.size() && !otherStop.contains(line.at(j))) {
                    j++;
                }
                pushSpan(QStringLiteral("other"), line.mid(i, j - i));
                i = j;
            }
        }

        r.highlightLines.push_back(spans);
        posInInput += line.size() + 1;
    }

    // 第二遍：按位置顺序插入延迟字节
    {
        int insertedHexCount = 0;
        QVector<DeferredPatch> patches = deferredValuePatches;
        std::sort(patches.begin(), patches.end(), [](const DeferredPatch &a, const DeferredPatch &b) {
            return a.startHexIndex < b.startHexIndex;
        });
        for (const DeferredPatch &patch : patches) {
            const EvalResult e = evalExpression(patch.expression, false, r);
            if (e.hasErrors) {
                r.errorCount++;
                continue;
            }
            const QString addrStr = QString::number((ulong)e.value, 16).toUpper().rightJustified(4, QLatin1Char('0'));
            const QString littleEndian = addrStr.mid(2, 2) + addrStr.mid(0, 2);
            const int insertPos = patch.startHexIndex + insertedHexCount;

            r.hexChars.insert(insertPos, littleEndian);
            insertedHexCount += littleEndian.size();

            QVector<int> mapping;
            for (int k = 0; k < patch.bytesToInsert; k++) {
                mapping.push_back(patch.startPosInLine);
                mapping.push_back(patch.endPosInLine);
            }
            r.charPosInInputMap.insert(insertPos, mapping.size(), 0);
            std::copy(mapping.cbegin(), mapping.cend(), r.charPosInInputMap.begin() + insertPos);
        }
    }

    for (const HighlightPatch &h : deferredHighlightPatches) {
        const EvalResult e = evalExpression(h.expression, false, r);
        if (e.hasErrors) {
            if (h.lineIndex < r.highlightLines.size() && h.spanIndex < r.highlightLines.at(h.lineIndex).size()) {
                RopSpan &span = r.highlightLines[h.lineIndex][h.spanIndex];
                if (!span.type.contains(QLatin1String("warning"))) {
                    span.type += QStringLiteral(",warning");
                }
            }
        }
    }

    const int byteCount = r.charPosInInputMap.size() / 2;
    for (int b = 0; b < byteCount; b++) {
        r.byteStartPositions.push_back(r.charPosInInputMap.at(b * 2));
    }

    r.totalBytes = r.hexChars.size() / 2;
    return r;
}

} // namespace Rop
