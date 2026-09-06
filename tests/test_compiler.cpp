/*
 * SPDX-FileCopyrightText: 2026 Yaing-Yan
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * 编译器移植正确性验证：
 * 读取测试用例 JSON 数组，逐条运行 Rop::parseRopInput，输出与
 * tests/crosscheck.mjs（node 运行原版 media/compiler.js）可对拍的 JSON。
 *
 * 用例格式：
 *   [{ "name": "...", "input": "...", "leftStartAddress": "E9E0",
 *      "rightStartAddress": "D710", "gadgets": [...] }]
 */
#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <cstdio>
#include <iostream>

#include "compiler.h"

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    if (argc < 2) {
        std::fprintf(stderr, "usage: ropide-test-compiler <cases.json>\n");
        return 2;
    }
    QFile f(QString::fromLocal8Bit(argv[1]));
    if (!f.open(QIODevice::ReadOnly)) {
        std::fprintf(stderr, "cannot open %s\n", argv[1]);
        return 2;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isArray()) {
        std::fprintf(stderr, "cases must be a JSON array\n");
        return 2;
    }

    for (const QJsonValue &caseValue : doc.array()) {
        const QJsonObject c = caseValue.toObject();
        QVector<Rop::RopGadget> gadgets;
        for (const QJsonValue &g : c.value(QLatin1String("gadgets")).toArray()) {
            const QJsonObject go = g.toObject();
            Rop::RopGadget gadget;
            gadget.name = go.value(QLatin1String("name")).toString();
            gadget.addr = go.value(QLatin1String("addr")).toString();
            gadget.desc = go.value(QLatin1String("desc")).toString();
            for (const QJsonValue &t : go.value(QLatin1String("tags")).toArray()) {
                Rop::RopTag tag;
                tag.name = t.toObject().value(QLatin1String("name")).toString();
                tag.color = t.toObject().value(QLatin1String("color")).toString();
                gadget.tags.push_back(tag);
            }
            gadgets.push_back(gadget);
        }

        const Rop::CompileResult r = Rop::parseRopInput(
            c.value(QLatin1String("input")).toString(),
            gadgets,
            c.value(QLatin1String("leftStartAddress")).toString(),
            c.value(QLatin1String("rightStartAddress")).toString());

        QJsonObject out;
        out.insert(QLatin1String("name"), c.value(QLatin1String("name")));
        out.insert(QLatin1String("hexChars"), r.hexChars);
        QJsonArray byteStarts;
        for (int v : r.byteStartPositions) {
            byteStarts.append(v);
        }
        out.insert(QLatin1String("byteStartPositions"), byteStarts);
        QJsonArray map;
        for (int v : r.charPosInInputMap) {
            map.append(v);
        }
        out.insert(QLatin1String("charPosInInputMap"), map);
        out.insert(QLatin1String("errorCount"), r.errorCount);
        out.insert(QLatin1String("totalBytes"), r.totalBytes);
        QJsonArray highlightLinesOut;
        for (const auto &spans : r.highlightLines) {
            QJsonArray lineSpans;
            for (const auto &sp : spans) {
                QJsonObject so;
                so.insert(QLatin1String("type"), sp.type);
                so.insert(QLatin1String("content"), sp.content);
                lineSpans.append(so);
            }
            highlightLinesOut.append(lineSpans);
        }
        out.insert(QLatin1String("highlightLines"), highlightLinesOut);
        QJsonObject constants;
        for (const QString &k : r.constantOrder) {
            constants.insert(k, r.constants.value(k));
        }
        out.insert(QLatin1String("constants"), constants);
        QJsonObject anchorSides;
        for (auto it = r.anchorSides.constBegin(); it != r.anchorSides.constEnd(); ++it) {
            anchorSides.insert(it.key(), it.value());
        }
        out.insert(QLatin1String("anchorSides"), anchorSides);

        std::cout << QString::fromUtf8(QJsonDocument(out).toJson(QJsonDocument::Compact)).toStdString()
                  << std::endl;
    }
    return 0;
}
