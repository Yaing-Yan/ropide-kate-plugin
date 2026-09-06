/*
 * SPDX-FileCopyrightText: 2026 Yaing-Yan
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "presets.h"

#include <QFile>
#include <QJsonDocument>

namespace Rop
{

QVector<RopGadget> loadPreset(const QString &name)
{
    const QString path = QStringLiteral(":/ropidekate/presets/%1.json").arg(name);
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        return {};
    }
    const auto r = parseGadgetsJson(QString::fromUtf8(f.readAll()));
    return r.ok ? r.gadgets : QVector<RopGadget>();
}

} // namespace Rop
