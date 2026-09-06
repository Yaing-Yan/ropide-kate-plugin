/*
 * SPDX-FileCopyrightText: 2026 Yaing-Yan
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * RopIDE 工具视图：Editor / Compile / Gadgets / Market / Disas / Settings 六个标签页，
 * 对应原插件 Webview 的全部面板；绑定的 Kate 文档仍是 .rop JSON（单一数据源）。
 */
#pragma once

#include <QHash>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QWidget>

#include "compiler.h"
#include "market.h"
#include "rop.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QTabWidget;
class QTextBrowser;
class QTimer;

namespace KTextEditor
{
class Document;
class MainWindow;
class View;
}

namespace Rop
{

class RopCodeEditor;
class RopIDEPlugin;

class RopToolView : public QWidget
{
    Q_OBJECT

public:
    RopToolView(RopIDEPlugin *plugin, KTextEditor::MainWindow *mainWindow, QWidget *parent = nullptr);
    ~RopToolView() override;

    /** 活动视图变化时重绑 .rop 文档；非 .rop 文档保持当前绑定。 */
    void setActiveDocument(KTextEditor::Document *doc);

    // 菜单动作
    void activateEditorTab();
    void activateCompileTab();
    void activateGadgetsTab();
    void activateMarketTab();
    void showAboutDialog();
    void newRopFile();
    void openRopFile();

private:
    // UI 构造
    QWidget *buildEditorTab();
    QWidget *buildCompileTab();
    QWidget *buildGadgetsTab();
    QWidget *buildMarketTab();
    QWidget *buildDisasTab();
    QWidget *buildSettingsTab();
    QWidget *buildFindBar();

    // i18n / 杂项
    QString t(const char *key) const;
    void toast(const QString &msg, bool isError = false);
    QIcon pluginIcon() const;

    // 文档同步
    void reloadFromDocument();
    void reparse();
    void markChanged();
    void scheduleWriteBack();
    void writeBack();

    // 编译面板
    void updateCompileTab();
    void updateCompileHexdump();
    QString hexdumpHtml() const;
    void copyHex();
    void copyDump();
    void jumpToAddress();

    // Gadgets
    void refreshGadgetList();
    void editGadget(int index);
    void addGadget();
    void importGadgets();
    void exportGadgets();
    void updateGadgetDisasmPane();

    // 程序广场
    void refreshMarketList();
    void downloadMarketItem(const QString &id);
    void openPublishDialog();
    void checkMarketUnread();
    void updateMarketBadge(int unread);

    // Disas
    void loadDisasForDocument();
    void disasJump();
    void renderDisas();
    int findDisasAddrIndex(const QString &query) const;

    // 模拟器
    void writeRam();
    void writeLauncher();
    void setEmuStatus(const QString &msg, const QString &kind);

    // sidecar（<name>.ropide.json）保存 _disas 路径
    QString sidecarPath() const;
    void loadSidecar();
    void saveSidecar(const QString &disasPath);

    // 设置页
    void syncSettingsUi();

    RopIDEPlugin *m_plugin = nullptr;
    KTextEditor::MainWindow *m_mainWindow = nullptr;
    KTextEditor::Document *m_doc = nullptr;
    QMetaObject::Connection m_docTextChangedConn;
    QMetaObject::Connection m_docDestroyedConn;

    // 数据
    RopDocumentData m_data;
    bool m_valid = true;
    QString m_invalidError;
    CompileResult m_parseResult;
    QTimer *m_saveTimer = nullptr;
    bool m_writing = false;

    // sidecar / disas
    QMap<int, QStringList> m_disasMap;
    QString m_disasFile;
    bool m_disasLoaded = false;
    QString m_sidecarDisasPath;

    // 市场
    QVector<MarketItem> m_marketItems;
    bool m_marketLoading = false;
    QString m_marketError;
    QString m_downloadingId;

    // 发布对话框的临时状态
    QString m_challengeToken;
    bool m_challengeLoading = false;
    qint64 m_challengeOffset = 0;

    // UI：骨架
    QTabWidget *m_tabs = nullptr;
    QLabel *m_toast = nullptr;
    QTimer *m_toastTimer = nullptr;
    QLabel *m_errorBanner = nullptr;

    // Editor tab
    QLineEdit *m_injectAddress = nullptr;
    QPushButton *m_btnWriteRam = nullptr;
    QLineEdit *m_launcherAddr = nullptr;
    QLineEdit *m_launcher = nullptr;
    QPushButton *m_btnWriteLauncher = nullptr;
    QLabel *m_emuStatus = nullptr;
    RopCodeEditor *m_editor = nullptr;
    QLabel *m_bytesInfo = nullptr;
    QLabel *m_cursorInfo = nullptr;
    QComboBox *m_jumpSide = nullptr;
    QLineEdit *m_jumpAddr = nullptr;

    // Find bar
    QWidget *m_findBar = nullptr;
    QWidget *m_replaceRow = nullptr;
    QLineEdit *m_findInput = nullptr;
    QLabel *m_findCount = nullptr;
    QLineEdit *m_replaceInput = nullptr;

    // Compile tab
    QLineEdit *m_leftAddrInput = nullptr;
    QLineEdit *m_rightAddrInput = nullptr;
    QLabel *m_compileInfo = nullptr;
    QTextBrowser *m_hexdump = nullptr;

    // Gadgets tab
    QLineEdit *m_gadgetSearch = nullptr;
    QListWidget *m_gadgetList = nullptr;
    QPlainTextEdit *m_gadgetDisasm = nullptr;

    // Market tab
    QLineEdit *m_marketSearch = nullptr;
    QListWidget *m_marketList = nullptr;
    QString m_pendingDownloadName;
    bool m_publishDialogClosedOk = false;
    QPushButton *m_btnConfirmPublish = nullptr;

    // Disas tab
    QLineEdit *m_disasAddr = nullptr;
    QLabel *m_disasStatus = nullptr;
    QPlainTextEdit *m_disasView = nullptr;

    // Settings tab
    QComboBox *m_selLanguage = nullptr;
    QCheckBox *m_chkWelcomeStartup = nullptr;
    QCheckBox *m_chkDisasm = nullptr;
    QCheckBox *m_chkHoverDisasm = nullptr;
    QLabel *m_disasFileLabel = nullptr;
    QSpinBox *m_portSpin = nullptr;

    friend class RopIDEPlugin;
};

} // namespace Rop
