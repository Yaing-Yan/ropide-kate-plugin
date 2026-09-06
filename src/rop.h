/*
 * SPDX-FileCopyrightText: 2026 Yaing-Yan
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * .rop 文件模型（移植自 ropide-vscode-plugin 的 src/rop.ts）。
 *
 * .rop 文件是单个 JSON 对象：
 *   {
 *     "input": string,               // 汇编 DSL 源码
 *     "gadgets": [{ name, addr, desc, tags: [{ name, color }] }],
 *     "leftStartAddress": "E9E0",    // 左侧起始地址（十六进制字符串）
 *     "rightStartAddress": "D710",   // 右侧起始地址
 *     "ideVersion": 100
 *   }
 *
 * 本插件不涉及 .rin / gadgets.json / config.json，所有数据都在单个 .rop 文件里。
 */
#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

namespace Rop
{

struct RopTag {
    QString name;
    QString color;

    bool operator==(const RopTag &o) const
    {
        return name == o.name && color == o.color;
    }
};

struct RopGadget {
    QString name;
    QString addr;
    QString desc;
    QVector<RopTag> tags;

    bool operator==(const RopGadget &o) const
    {
        return name == o.name && addr == o.addr && desc == o.desc && tags == o.tags;
    }
};

struct RopDocumentData {
    QString input;
    QVector<RopGadget> gadgets;
    QString leftStartAddress;
    QString rightStartAddress;
    int ideVersion = 100;
};

inline constexpr const char *DEFAULT_LEFT_ADDRESS = "E9E0";
inline constexpr const char *DEFAULT_RIGHT_ADDRESS = "D710";
inline constexpr int IDE_VERSION = 100;

struct GadgetsParseResult {
    bool ok = false;
    QVector<RopGadget> gadgets;
    QString error;
};

struct RopDocParseResult {
    bool ok = false;
    RopDocumentData data;
    QString error;
};

/** 解析一个独立的 gadgets.json（顶层数组），用于导入到 .rop 文件的 gadgets 字段。 */
GadgetsParseResult parseGadgetsJson(const QString &text);

/** 解析 .rop 文件内容；失败时 ok=false 并给出错误描述。 */
RopDocParseResult parseRopDocument(const QString &text);

/** 与 ropide.pages.dev / ropide-python 保持一致：紧凑单行 JSON。 */
QString serializeRopDocument(const RopDocumentData &data);

RopDocumentData newRopDocument();

/**
 * 解析 _disas 文本，建立 地址(数字) -> 各行反汇编列表 的映射。
 * map 的键按地址升序返回在 addresses 中。
 */
void parseDisas(const QString &text, QMap<int, QStringList> &map);

/**
 * 从地址起截取反汇编片段，直到包含 POP PC / RET / RT 的行为止。
 * 若无终止指令，最多截取 maxLines 行；无该地址记录返回空列表。
 */
QStringList disasSnippet(const QMap<int, QStringList> &map, int addr, int maxLines = 40);

} // namespace Rop
