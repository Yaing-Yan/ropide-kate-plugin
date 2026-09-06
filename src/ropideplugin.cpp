/*
 * SPDX-FileCopyrightText: 2026 Yaing-Yan
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "ropideplugin.h"

#include "emu.h"
#include "market.h"
#include "roptoolview.h"
#include "settings.h"

#include <KActionCollection>
#include <KXMLGUIFactory>
#include <QHBoxLayout>
#include <QTextBlock>

#include <KLocalizedString>
#include <KPluginFactory>
#include <KTextEditor/Document>
#include <KTextEditor/MainWindow>
#include <KTextEditor/View>

#include <QAction>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QIcon>

namespace Rop
{

RopIDEPlugin::RopIDEPlugin(QObject *parent, const QVariantList &args)
    : KTextEditor::Plugin(parent)
{
    Q_UNUSED(args);
    m_settings = new Settings(this);
    m_settings->load();
    m_market = new MarketClient(this);
    m_emu = new EmuClient(this);
}

RopIDEPlugin::~RopIDEPlugin() = default;

QObject *RopIDEPlugin::createView(KTextEditor::MainWindow *mainWindow)
{
    auto *view = new RopIDEPluginView(this, mainWindow);
    maybeShowWelcome(mainWindow);
    return view;
}

bool RopIDEPlugin::loadDisasCache(const QString &path, QMap<int, QStringList> &map)
{
    const auto it = m_disasCache.constFind(path);
    if (it != m_disasCache.constEnd()) {
        map = it.value();
        return true;
    }
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        return false;
    }
    parseDisas(QString::fromUtf8(f.readAll()), map);
    m_disasCache.insert(path, map);
    return true;
}

void RopIDEPlugin::maybeShowWelcome(KTextEditor::MainWindow *mainWindow)
{
    if (m_welcomeShown || !m_settings->showWelcomeOnStartup) {
        return;
    }
    m_welcomeShown = true;
    // 延迟到事件循环，等主窗口完全就绪
    QMetaObject::invokeMethod(
        this,
        [this, mainWindow]() {
            if (auto *v = qobject_cast<RopToolView *>(mainWindow->pluginView(QStringLiteral("ropidekate")))) {
                v->showAboutDialog();
            }
        },
        Qt::QueuedConnection);
}

/* ---------------- 每窗口视图 ---------------- */

RopIDEPluginView::RopIDEPluginView(RopIDEPlugin *plugin, KTextEditor::MainWindow *mainWindow)
    : m_plugin(plugin)
    , m_mainWindow(mainWindow)
{
    setComponentName(QStringLiteral("ropidekate"), i18n("RopIDE"));
    setXMLFile(QStringLiteral("ropidekateui.rc"));

    auto *ac = actionCollection();
    auto *actNew = ac->addAction(QStringLiteral("ropide_new"));
    actNew->setText(i18n("RopIDE: New .rop File"));
    actNew->setIcon(QIcon::fromTheme(QStringLiteral("document-new")));
    connect(actNew, &QAction::triggered, this, [this]() {
        m_view->newRopFile();
    });

    auto *actOpen = ac->addAction(QStringLiteral("ropide_open"));
    actOpen->setText(i18n("RopIDE: Open .rop File"));
    actOpen->setIcon(QIcon::fromTheme(QStringLiteral("document-open")));
    connect(actOpen, &QAction::triggered, this, [this]() {
        m_view->openRopFile();
    });

    auto *actCompile = ac->addAction(QStringLiteral("ropide_compile"));
    actCompile->setText(i18n("RopIDE: Compile"));
    actCompile->setIcon(QIcon::fromTheme(QStringLiteral("media-playback-start")));
    connect(actCompile, &QAction::triggered, this, [this]() {
        m_view->activateCompileTab();
    });

    auto *actGadgets = ac->addAction(QStringLiteral("ropide_gadgets"));
    actGadgets->setText(i18n("RopIDE: Show Gadgets"));
    actGadgets->setIcon(QIcon::fromTheme(QStringLiteral("view-list-details")));
    connect(actGadgets, &QAction::triggered, this, [this]() {
        m_view->activateGadgetsTab();
    });

    auto *actMarket = ac->addAction(QStringLiteral("ropide_market"));
    actMarket->setText(i18n("RopIDE: Program Market"));
    actMarket->setIcon(QIcon::fromTheme(QStringLiteral("applications-internet")));
    connect(actMarket, &QAction::triggered, this, [this]() {
        m_view->activateMarketTab();
    });

    auto *actAbout = ac->addAction(QStringLiteral("ropide_about"));
    actAbout->setText(i18n("RopIDE: About"));
    actAbout->setIcon(QIcon::fromTheme(QStringLiteral("help-about")));
    connect(actAbout, &QAction::triggered, this, [this]() {
        m_view->showAboutDialog();
    });

    // 工具视图（RopIDE 面板）
    m_view = new RopToolView(m_plugin, m_mainWindow);
    QWidget *toolView = m_mainWindow->createToolView(
        m_plugin, QStringLiteral("RopIDEToolView"), KTextEditor::MainWindow::Right,
        QIcon(QStringLiteral(":/ropidekate/icon.png")), i18n("RopIDE"));
    if (toolView) {
        auto *layout = new QHBoxLayout(toolView);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->addWidget(m_view);
        m_view->setParent(toolView);
    } else {
        // 主窗口不可用时兜底：视图直接挂着（无工具视图）
        m_view->setParent(nullptr);
        m_view->show();
    }

    // 菜单合并 + 活动视图跟踪
    m_mainWindow->guiFactory()->addClient(this);
    connect(m_mainWindow, &KTextEditor::MainWindow::viewChanged, this, [this](KTextEditor::View *view) {
        if (view) {
            m_view->setActiveDocument(view->document());
        }
        updateActions();
    });

    if (KTextEditor::View *active = m_mainWindow->activeView()) {
        m_view->setActiveDocument(active->document());
    }
    updateActions();
}

RopIDEPluginView::~RopIDEPluginView()
{
    m_mainWindow->guiFactory()->removeClient(this);
}

bool RopIDEPluginView::activeDocumentIsRop() const
{
    KTextEditor::View *view = m_mainWindow->activeView();
    return view && view->document()->url().fileName().endsWith(QLatin1String(".rop"), Qt::CaseInsensitive);
}

void RopIDEPluginView::updateActions()
{
    if (QAction *act = actionCollection()->action(QStringLiteral("ropide_compile"))) {
        act->setEnabled(true); // 未绑定文档时面板会提示先打开 .rop 文件
    }
}

} // namespace Rop

K_PLUGIN_FACTORY_WITH_JSON(
    RopIDEKatePluginFactory,
    "ropidekate.json",
    registerPlugin<Rop::RopIDEPlugin>();)

#include "ropideplugin.moc"
