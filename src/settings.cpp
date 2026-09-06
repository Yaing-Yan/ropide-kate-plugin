/*
 * SPDX-FileCopyrightText: 2026 Yaing-Yan
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "settings.h"

#include <KConfig>
#include <KConfigGroup>
#include <KSharedConfig>

namespace Rop
{

Settings::Settings(QObject *parent)
    : QObject(parent)
{
}

void Settings::load()
{
    KConfig config(QStringLiteral("ropidekaterc"));
    const KConfigGroup g = config.group(QStringLiteral("General"));
    const QString lang = g.readEntry("language", QStringLiteral("zh-CN"));
    language = (lang == QLatin1String("en")) ? lang : QStringLiteral("zh-CN");
    showWelcomeOnStartup = g.readEntry("showWelcomeOnStartup", true);
    showGadgetDisasm = g.readEntry("showGadgetDisasm", false);
    showGadgetHoverDisasm = g.readEntry("showGadgetHoverDisasm", false);
    casioemuMcpPort = g.readEntry("casioemuMcpPort", 3001);
    injectAddress = g.readEntry("injectAddress", QString());
    launcher = g.readEntry("launcher", QString());
    launcherAddr = g.readEntry("launcherAddr", QStringLiteral("D180"));
    if (launcherAddr.isEmpty()) {
        launcherAddr = QStringLiteral("D180");
    }
    marketLastSeen = g.readEntry("marketLastSeen", (qint64)0);
}

void Settings::save()
{
    KConfig config(QStringLiteral("ropidekaterc"));
    KConfigGroup g = config.group(QStringLiteral("General"));
    g.writeEntry("language", language);
    g.writeEntry("showWelcomeOnStartup", showWelcomeOnStartup);
    g.writeEntry("showGadgetDisasm", showGadgetDisasm);
    g.writeEntry("showGadgetHoverDisasm", showGadgetHoverDisasm);
    g.writeEntry("casioemuMcpPort", casioemuMcpPort);
    g.writeEntry("injectAddress", injectAddress);
    g.writeEntry("launcher", launcher);
    g.writeEntry("launcherAddr", launcherAddr);
    g.writeEntry("marketLastSeen", marketLastSeen);
    config.sync();
}

void Settings::markChanged()
{
    save();
    Q_EMIT changed();
}

} // namespace Rop
