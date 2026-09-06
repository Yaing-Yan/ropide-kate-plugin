/*
 * SPDX-FileCopyrightText: 2026 Yaing-Yan
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * 运行时双语文案（简体中文 / English），与原插件 ropide.language 设置对应。
 */
#pragma once

#include <QLatin1String>
#include <QString>

#include "i18n_strings.h"

namespace Rop
{

/** 语言代码："zh-CN" / "en"（其它取值回退到 zh-CN）。 */
inline QString tr2(const QString &lang, const char *key)
{
    const QLatin1String k(key);
    if (lang == QLatin1String("en")) {
        const auto it = enTable().constFind(k);
        if (it != enTable().constEnd()) {
            return it.value();
        }
    }
    const auto it = zhTable().constFind(k);
    return it != zhTable().constEnd() ? it.value() : QString::fromLatin1(key);
}

} // namespace Rop
