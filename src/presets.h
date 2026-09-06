/*
 * SPDX-FileCopyrightText: 2026 Yaing-Yan
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * VerF / VerC 内置 gadgets 预设（数据移植自 ropide-python / ropide-vscode-plugin）。
 */
#pragma once

#include <QString>
#include <QVector>

#include "rop.h"

namespace Rop
{

/** 从 Qt 资源加载内置预设；name 为 "verf" 或 "verc"。失败返回空列表。 */
QVector<RopGadget> loadPreset(const QString &name);

} // namespace Rop
