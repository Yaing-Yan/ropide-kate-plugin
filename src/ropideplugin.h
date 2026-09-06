/*
 * SPDX-FileCopyrightText: 2026 Yaing-Yan
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * RopIDE for Kate —— KTextEditor 应用插件入口。
 * 移植自 ropide-vscode-plugin（https://github.com/Yaing-Yan/ropide-vscode-plugin）。
 */
#pragma once

#include <KTextEditor/Plugin>
#include <KXMLGUIClient>

#include <QMap>
#include <QVariant>

#include "rop.h"

namespace Rop
{

class EmuClient;
class MarketClient;
class Settings;
class RopToolView;

class RopIDEPlugin : public KTextEditor::Plugin
{
    Q_OBJECT

public:
    explicit RopIDEPlugin(QObject *parent, const QVariantList &args = QVariantList());
    ~RopIDEPlugin() override;

    QObject *createView(KTextEditor::MainWindow *mainWindow) override;

    Settings *settings() const
    {
        return m_settings;
    }
    MarketClient *market() const
    {
        return m_market;
    }
    EmuClient *emu() const
    {
        return m_emu;
    }

    /** _disas 文件解析缓存（避免对同一大文件重复解析）。 */
    bool loadDisasCache(const QString &path, QMap<int, QStringList> &map);

    /** 启动时按设置弹出欢迎页（每次进程只弹一次）。 */
    void maybeShowWelcome(KTextEditor::MainWindow *mainWindow);

private:
    Settings *m_settings = nullptr;
    MarketClient *m_market = nullptr;
    EmuClient *m_emu = nullptr;
    QHash<QString, QMap<int, QStringList>> m_disasCache;
    bool m_welcomeShown = false;
};

class RopIDEPluginView : public QObject, public KXMLGUIClient
{
    Q_OBJECT

public:
    RopIDEPluginView(RopIDEPlugin *plugin, KTextEditor::MainWindow *mainWindow);
    ~RopIDEPluginView() override;

private:
    void updateActions();
    bool activeDocumentIsRop() const;

    RopIDEPlugin *m_plugin = nullptr;
    KTextEditor::MainWindow *m_mainWindow = nullptr;
    RopToolView *m_view = nullptr;
};

} // namespace Rop
