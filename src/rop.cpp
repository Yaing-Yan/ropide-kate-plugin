/*
 * SPDX-FileCopyrightText: 2026 Yaing-Yan
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "rop.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QRegularExpression>

namespace Rop
{

static RopGadget normalizeGadget(const QJsonValue &raw)
{
    RopGadget g;
    if (!raw.isObject()) {
        return g;
    }
    const QJsonObject o = raw.toObject();
    g.name = o.value(QLatin1String("name")).toString(QString());
    g.addr = o.value(QLatin1String("addr")).toString(QString());
    g.desc = o.value(QLatin1String("desc")).toString(QString());
    const QJsonValue tags = o.value(QLatin1String("tags"));
    if (tags.isArray()) {
        for (const QJsonValue &t : tags.toArray()) {
            if (!t.isObject()) {
                continue;
            }
            const QJsonObject to = t.toObject();
            RopTag tag;
            tag.name = to.value(QLatin1String("name")).toString(QString());
            tag.color = to.value(QLatin1String("color")).toString(QLatin1String("gray"));
            g.tags.push_back(tag);
        }
    }
    return g;
}

GadgetsParseResult parseGadgetsJson(const QString &text)
{
    GadgetsParseResult result;
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(text.toUtf8(), &err);
    if (doc.isNull()) {
        result.error = QStringLiteral("不是合法的 JSON：%1").arg(err.errorString());
        return result;
    }
    if (!doc.isArray()) {
        result.error = QStringLiteral("gadgets.json 顶层必须是数组。");
        return result;
    }
    for (const QJsonValue &v : doc.array()) {
        result.gadgets.push_back(normalizeGadget(v));
    }
    result.ok = true;
    return result;
}

RopDocParseResult parseRopDocument(const QString &text)
{
    RopDocParseResult result;
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(text.toUtf8(), &err);
    if (doc.isNull()) {
        result.error = QStringLiteral("不是合法的 JSON：%1").arg(err.errorString());
        return result;
    }
    if (!doc.isObject()) {
        result.error = QStringLiteral(".rop 文件顶层必须是 JSON 对象。");
        return result;
    }
    const QJsonObject obj = doc.object();
    result.data.input = obj.value(QLatin1String("input")).toString(QString());
    const QJsonValue gadgets = obj.value(QLatin1String("gadgets"));
    if (gadgets.isArray()) {
        for (const QJsonValue &v : gadgets.toArray()) {
            result.data.gadgets.push_back(normalizeGadget(v));
        }
    }
    result.data.leftStartAddress =
        obj.value(QLatin1String("leftStartAddress")).toString(QLatin1String(DEFAULT_LEFT_ADDRESS));
    result.data.rightStartAddress =
        obj.value(QLatin1String("rightStartAddress")).toString(QLatin1String(DEFAULT_RIGHT_ADDRESS));
    result.data.ideVersion = obj.value(QLatin1String("ideVersion")).toInt(IDE_VERSION);
    result.ok = true;
    return result;
}

QString serializeRopDocument(const RopDocumentData &data)
{
    QJsonDocument doc;
    QJsonObject obj;
    obj.insert(QLatin1String("input"), data.input);
    QJsonArray gadgets;
    for (const RopGadget &g : data.gadgets) {
        QJsonObject go;
        go.insert(QLatin1String("name"), g.name);
        go.insert(QLatin1String("addr"), g.addr);
        go.insert(QLatin1String("desc"), g.desc);
        QJsonArray tags;
        for (const RopTag &t : g.tags) {
            QJsonObject to;
            to.insert(QLatin1String("name"), t.name);
            to.insert(QLatin1String("color"), t.color);
            tags.append(to);
        }
        go.insert(QLatin1String("tags"), tags);
        gadgets.append(go);
    }
    obj.insert(QLatin1String("gadgets"), gadgets);
    obj.insert(QLatin1String("leftStartAddress"), data.leftStartAddress);
    obj.insert(QLatin1String("rightStartAddress"), data.rightStartAddress);
    obj.insert(QLatin1String("ideVersion"), data.ideVersion);
    doc.setObject(obj);
    // JSON.stringify 默认不会把非 ASCII 字符转义为 \uXXXX，这里保持同样行为。
    return QString::fromUtf8(doc.toJson(QJsonDocument::Compact));
}

RopDocumentData newRopDocument()
{
    RopDocumentData data;
    data.input = QStringLiteral("// new.rop\n");
    data.leftStartAddress = QLatin1String(DEFAULT_LEFT_ADDRESS);
    data.rightStartAddress = QLatin1String(DEFAULT_RIGHT_ADDRESS);
    data.ideVersion = IDE_VERSION;
    return data;
}

void parseDisas(const QString &text, QMap<int, QStringList> &map)
{
    map.clear();
    static const QRegularExpression headRe(QStringLiteral("^([0-9A-Fa-f]{4,6})\\s{2,}"));
    const QStringList lines = text.split(QLatin1Char('\n'));
    for (const QString &lineRaw : lines) {
        const auto m = headRe.match(lineRaw);
        if (m.hasMatch()) {
            const int addr = m.captured(1).toInt(nullptr, 16);
            map[addr].push_back(lineRaw);
        }
    }
    // 与 JS 版一致：行尾空白在入表前未去掉，这里截断尾随空白便于展示。
    for (auto it = map.begin(); it != map.end(); ++it) {
        for (QString &l : it.value()) {
            while (l.endsWith(QLatin1Char(' ')) || l.endsWith(QLatin1Char('\t')) || l.endsWith(QLatin1Char('\r'))) {
                l.chop(1);
            }
        }
    }
}

QStringList disasSnippet(const QMap<int, QStringList> &map, int addr, int maxLines)
{
    if (!map.contains(addr)) {
        return {};
    }
    static const QRegularExpression terminalRe(
        QStringLiteral("\\bPOP\\s+PC\\b|\\bRT\\b|\\bRET\\b"),
        QRegularExpression::CaseInsensitiveOption);
    QStringList lines;
    for (auto it = map.lowerBound(addr); it != map.end(); ++it) {
        for (const QString &line : it.value()) {
            lines.push_back(line);
            if (terminalRe.match(line).hasMatch()) {
                return lines;
            }
            if (lines.size() >= maxLines) {
                return lines;
            }
        }
    }
    return lines;
}

} // namespace Rop
