/*
 * SPDX-FileCopyrightText: 2026 Yaing-Yan
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "roptoolview.h"

#include "emu.h"
#include "i18n.h"
#include "market.h"
#include "presets.h"
#include "ropcodeeditor.h"
#include "ropideplugin.h"
#include "settings.h"

#include <KTextEditor/Application>
#include <KTextEditor/Document>
#include <KTextEditor/Editor>
#include <KTextEditor/MainWindow>
#include <KTextEditor/View>

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCursor>
#include <QDateTime>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPalette>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QShortcut>
#include <QSpinBox>
#include <QTabWidget>
#include <QTextBrowser>
#include <QTextEdit>
#include <QClipboard>
#include <QTextBlock>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>
#include <QVBoxLayout>

#include <memory>

namespace Rop
{

namespace
{

QString hexAddr(long long v)
{
    if (v < 0) {
        v = 0;
    }
    return QString::number((ulong)v, 16).toUpper().rightJustified(4, QLatin1Char('0'));
}

long long parseBase(const QString &s)
{
    QString t = s;
    if (t.isEmpty()) {
        t = QStringLiteral("0");
    }
    if (t.startsWith(QLatin1String("0x"), Qt::CaseInsensitive)) {
        t.remove(0, 2);
    }
    bool ok = false;
    const long long v = t.toLongLong(&ok, 16);
    return ok ? v : 0;
}

bool isHexAddrInput(const QString &s)
{
    static const QRegularExpression re(QStringLiteral("^[0-9A-Fa-f]{1,5}$"));
    return re.match(s).hasMatch();
}

void stripHexFilter(QLineEdit *edit)
{
    // 与原插件一致：只允许 hex 字符
    QString text = edit->text();
    QString out;
    for (const QChar c : text) {
        if ((c >= QLatin1Char('0') && c <= QLatin1Char('9'))
            || (c >= QLatin1Char('a') && c <= QLatin1Char('f'))
            || (c >= QLatin1Char('A') && c <= QLatin1Char('F'))) {
            out.append(c);
        }
    }
    if (out.size() > 5) {
        out = out.left(5);
    }
    if (out != text) {
        edit->setText(out);
    }
}

QString escapeHtml(const QString &s)
{
    QString r = s;
    r.replace(QLatin1Char('&'), QLatin1String("&amp;"));
    r.replace(QLatin1Char('<'), QLatin1String("&lt;"));
    r.replace(QLatin1Char('>'), QLatin1String("&gt;"));
    r.replace(QLatin1Char('"'), QLatin1String("&quot;"));
    return r;
}

const char *TAG_COLORS[] = {"gray", "blue", "yellow", "orange", "green", "purple"};

QString sanitizeFileName(const QString &name)
{
    static const QRegularExpression bad(QStringLiteral("[\\\\/:*?\"<>|]"));
    QString cleaned = name;
    cleaned.replace(bad, QStringLiteral(" "));
    cleaned.replace(QRegularExpression(QStringLiteral("\\s+")), QStringLiteral(" "));
    cleaned = cleaned.trimmed();
    cleaned.remove(QRegularExpression(QStringLiteral("\\.rop$"), QRegularExpression::CaseInsensitiveOption));
    return cleaned.isEmpty() ? QStringLiteral("program") : cleaned;
}

bool isTerminalLine(const QString &text)
{
    static const QRegularExpression re(QStringLiteral("\\bPOP\\s+PC\\b|\\bRT\\b|\\bRET\\b"),
                                       QRegularExpression::CaseInsensitiveOption);
    return re.match(text).hasMatch();
}

} // namespace

/* ---------------- 构造 ---------------- */

RopToolView::RopToolView(RopIDEPlugin *plugin, KTextEditor::MainWindow *mainWindow, QWidget *parent)
    : QWidget(parent)
    , m_plugin(plugin)
    , m_mainWindow(mainWindow)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_errorBanner = new QLabel(this);
    m_errorBanner->setObjectName(QStringLiteral("ropideErrorBanner"));
    m_errorBanner->setWordWrap(true);
    m_errorBanner->setVisible(false);
    m_errorBanner->setStyleSheet(
        QStringLiteral("QLabel#ropideErrorBanner { background: palette(alternate-base); color: #f48771; padding: 6px; }"));
    layout->addWidget(m_errorBanner);

    m_tabs = new QTabWidget(this);
    layout->addWidget(m_tabs, 1);

    m_tabs->addTab(buildEditorTab(), t("compile") /*占位，立即重设*/);
    m_tabs->setTabText(0, QStringLiteral("Editor"));
    m_tabs->addTab(buildCompileTab(), QStringLiteral("Compile"));
    m_tabs->addTab(buildGadgetsTab(), QStringLiteral("Gadgets"));
    m_tabs->addTab(buildMarketTab(), t("market"));
    m_tabs->addTab(buildDisasTab(), t("disas"));
    m_tabs->addTab(buildSettingsTab(), t("settings"));

    m_saveTimer = new QTimer(this);
    m_saveTimer->setSingleShot(true);
    m_saveTimer->setInterval(300);
    connect(m_saveTimer, &QTimer::timeout, this, &RopToolView::writeBack);

    m_toastTimer = new QTimer(this);
    m_toastTimer->setSingleShot(true);
    connect(m_toastTimer, &QTimer::timeout, this, [this]() {
        m_toast->setVisible(false);
    });

    // 市场未读小红点：打开面板时查一次（不标记已读）
    checkMarketUnread();

    // 市场信号
    connect(m_plugin->market(), &MarketClient::itemFinished, this,
            [this](const QString &id, bool ok, const QString &ropJson, const QString &error) {
                Q_UNUSED(id);
                m_downloadingId.clear();
                if (!ok) {
                    toast(t("downloadFail") + error, true);
                }
                if (ok) {
                    // 让用户指定保存路径，然后打开（对应原插件的 market:get 流程）
                    const QString name = m_pendingDownloadName;
                    const QString path = QFileDialog::getSaveFileName(
                        this, QStringLiteral("保存下载的程序"),
                        sanitizeFileName(name) + QStringLiteral(".rop"),
                        QStringLiteral("Rop File (*.rop)"));
                    if (path.isEmpty()) {
                        toast(t("cancelled"));
                    } else {
                        QFile f(path);
                        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                            toast(QStringLiteral("写入失败：%1").arg(f.errorString()), true);
                        } else {
                            f.write(ropJson.toUtf8());
                            f.close();
                            if (KTextEditor::Editor::instance()->application()) {
                                KTextEditor::Editor::instance()->application()->openUrl(
                                    QUrl::fromLocalFile(path));
                            }
                            toast(t("savedOpened"));
                        }
                    }
                }
                refreshMarketList();
            });

    // challenge / publish 信号在 openPublishDialog 内以对话框为接收者连接

    // 模拟器信号
    connect(m_plugin->emu(), &EmuClient::writeFinished, this,
            [this](bool ok, const QString &code, const QString &error) {
                if (ok) {
                    setEmuStatus(QString(), QString());
                    toast(t("written"));
                } else {
                    const QString err = code == QLatin1String("not-running")
                        ? t("emuNotRunning")
                        : (error.isEmpty() ? t("writeFail") : error);
                    setEmuStatus(err, QStringLiteral("error"));
                }
            });

    connect(m_plugin->settings(), &Settings::changed, this, &RopToolView::syncSettingsUi);

    // 直接点击「程序广场」标签页时也要拉取列表（原来只在菜单动作里拉取，导致空白）
    connect(m_tabs, &QTabWidget::currentChanged, this, [this](int idx) {
        if (idx == 3) {
            openMarketTab();
        }
    });
}

RopToolView::~RopToolView()
{
    if (m_doc) {
        disconnect(m_docTextChangedConn);
        disconnect(m_docDestroyedConn);
    }
}

QString RopToolView::t(const char *key) const
{
    return tr2(m_plugin->settings()->language, key);
}

QIcon RopToolView::pluginIcon() const
{
    return QIcon(QStringLiteral(":/ropidekate/icon.png"));
}

void RopToolView::toast(const QString &msg, bool isError)
{
    if (!m_toast) {
        return;
    }
    m_toast->setText(msg);
    m_toast->setStyleSheet(isError ? QStringLiteral("color: #f48771; padding: 2px;")
                                   : QStringLiteral("padding: 2px;"));
    m_toast->setVisible(true);
    m_toastTimer->start(2600);
}

/* ---------------- Editor 标签页 ---------------- */

QWidget *RopToolView::buildEditorTab()
{
    auto *root = new QWidget(this);
    auto *layout = new QVBoxLayout(root);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);

    // 顶部工具栏：覆写模拟器（对应原插件 toolbar）
    auto *emuBar = new QWidget(root);
    auto *emuLayout = new QHBoxLayout(emuBar);
    emuLayout->setContentsMargins(4, 2, 4, 2);

    auto *injectLabel = new QLabel(t("injectAddr"), emuBar);
    m_injectAddress = new QLineEdit(emuBar);
    m_injectAddress->setMaxLength(5);
    m_injectAddress->setFixedWidth(70);
    m_btnWriteRam = new QPushButton(t("writeRam"), emuBar);
    auto *launcherAddrLabel = new QLabel(t("addr"), emuBar);
    m_launcherAddr = new QLineEdit(QStringLiteral("D180"), emuBar);
    m_launcherAddr->setMaxLength(5);
    m_launcherAddr->setFixedWidth(60);
    m_launcher = new QLineEdit(emuBar);
    m_launcher->setPlaceholderText(t("launcherPh"));
    m_btnWriteLauncher = new QPushButton(t("writeLauncher"), emuBar);
    m_emuStatus = new QLabel(emuBar);
    m_emuStatus->setVisible(false);
    m_emuStatus->setStyleSheet(QStringLiteral("color: #e51400;"));

    emuLayout->addWidget(injectLabel);
    emuLayout->addWidget(m_injectAddress);
    emuLayout->addWidget(m_btnWriteRam);
    emuLayout->addSpacing(8);
    emuLayout->addWidget(launcherAddrLabel);
    emuLayout->addWidget(m_launcherAddr);
    emuLayout->addWidget(m_launcher, 1);
    emuLayout->addWidget(m_btnWriteLauncher);
    emuLayout->addWidget(m_emuStatus);
    layout->addWidget(emuBar);

    connect(m_injectAddress, &QLineEdit::textChanged, this, [this](const QString &) {
        stripHexFilter(m_injectAddress);
    });
    connect(m_injectAddress, &QLineEdit::editingFinished, this, [this]() {
        m_plugin->settings()->injectAddress = m_injectAddress->text().trimmed();
        m_plugin->settings()->save();
    });
    connect(m_launcher, &QLineEdit::editingFinished, this, [this]() {
        m_plugin->settings()->launcher = m_launcher->text();
        m_plugin->settings()->save();
    });
    connect(m_launcherAddr, &QLineEdit::textChanged, this, [this](const QString &) {
        stripHexFilter(m_launcherAddr);
    });
    connect(m_launcherAddr, &QLineEdit::editingFinished, this, [this]() {
        m_plugin->settings()->launcherAddr =
            m_launcherAddr->text().trimmed().isEmpty() ? QStringLiteral("D180")
                                                       : m_launcherAddr->text().trimmed();
        m_launcherAddr->setText(m_plugin->settings()->launcherAddr);
        m_plugin->settings()->save();
    });
    connect(m_btnWriteRam, &QPushButton::clicked, this, &RopToolView::writeRam);
    connect(m_btnWriteLauncher, &QPushButton::clicked, this, &RopToolView::writeLauncher);

    // 查找替换条
    m_findBar = buildFindBar();
    m_findBar->setVisible(false);
    layout->addWidget(m_findBar);

    // DSL 编辑器
    m_editor = new RopCodeEditor(root);
    layout->addWidget(m_editor, 1);
    connect(m_editor, &RopCodeEditor::textEditedByUser, this, [this]() {
        reparse();
        scheduleWriteBack();
    });
    connect(m_editor, &RopCodeEditor::cursorAddressChanged, this, [this](const QString &l, const QString &r) {
        m_cursorInfo->setText(QStringLiteral("L:%1  R:%2").arg(l, r));
    });

    // 底部：字节数/错误 + 跳转地址 + 光标地址 + toast
    auto *footer = new QWidget(root);
    auto *footerLayout = new QHBoxLayout(footer);
    footerLayout->setContentsMargins(6, 2, 6, 2);
    m_bytesInfo = new QLabel(QStringLiteral("0 bytes · 0 errors"), footer);
    m_bytesInfo->setStyleSheet(QStringLiteral("QLabel { padding: 1px 4px; border-radius: 3px; background: palette(mid); }"));
    m_cursorInfo = new QLabel(footer);
    auto *jumpLabel = new QLabel(t("jumpLabel"), footer);
    m_jumpSide = new QComboBox(footer);
    m_jumpSide->addItem(t("left"), QStringLiteral("left"));
    m_jumpSide->addItem(t("right"), QStringLiteral("right"));
    m_jumpAddr = new QLineEdit(footer);
    m_jumpAddr->setMaxLength(5);
    m_jumpAddr->setPlaceholderText(t("jumpPh"));
    m_jumpAddr->setFixedWidth(80);
    m_toast = new QLabel(footer);
    m_toast->setVisible(false);

    footerLayout->addWidget(m_bytesInfo);
    footerLayout->addStretch(1);
    footerLayout->addWidget(m_toast);
    footerLayout->addStretch(1);
    footerLayout->addWidget(jumpLabel);
    footerLayout->addWidget(m_jumpSide);
    footerLayout->addWidget(m_jumpAddr);
    footerLayout->addWidget(m_cursorInfo);
    layout->addWidget(footer);

    connect(m_jumpAddr, &QLineEdit::textChanged, this, [this](const QString &) {
        stripHexFilter(m_jumpAddr);
    });
    connect(m_jumpAddr, &QLineEdit::returnPressed, this, &RopToolView::jumpToAddress);
    connect(m_jumpSide, &QComboBox::currentIndexChanged, this, [this](int) {
        if (!m_jumpAddr->text().trimmed().isEmpty()) {
            jumpToAddress();
        }
    });

    // 编辑器内快捷键：查找 / 替换（对应原插件 Ctrl+F / Ctrl+H）
    auto *findSc = new QShortcut(QKeySequence::Find, m_editor);
    connect(findSc, &QShortcut::activated, this, [this]() {
        m_findBar->setVisible(true);
        m_findInput->setFocus();
        m_findInput->selectAll();
    });
    auto *replaceSc = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_H), m_editor);
    connect(replaceSc, &QShortcut::activated, this, [this]() {
        m_findBar->setVisible(true);
        m_replaceRow->setVisible(true);
        m_replaceInput->setFocus();
    });

    return root;
}

QWidget *RopToolView::buildFindBar()
{
    auto *bar = new QWidget(this);
    auto *v = new QVBoxLayout(bar);
    v->setContentsMargins(4, 2, 4, 2);
    v->setSpacing(2);

    auto *row1 = new QWidget(bar);
    auto *h1 = new QHBoxLayout(row1);
    h1->setContentsMargins(0, 0, 0, 0);
    m_findInput = new QLineEdit(row1);
    m_findInput->setPlaceholderText(QStringLiteral("查找…"));
    m_findCount = new QLabel(row1);
    auto *prev = new QPushButton(QStringLiteral("▲"), row1);
    auto *next = new QPushButton(QStringLiteral("▼"), row1);
    auto *toggleReplace = new QPushButton(QStringLiteral("⤵"), row1);
    auto *close = new QPushButton(QStringLiteral("×"), row1);
    for (auto *b : {prev, next, toggleReplace, close}) {
        b->setFixedWidth(28);
    }
    h1->addWidget(m_findInput, 1);
    h1->addWidget(m_findCount);
    h1->addWidget(prev);
    h1->addWidget(next);
    h1->addWidget(toggleReplace);
    h1->addWidget(close);
    v->addWidget(row1);

    m_replaceRow = new QWidget(bar);
    auto *h2 = new QHBoxLayout(m_replaceRow);
    h2->setContentsMargins(0, 0, 0, 0);
    m_replaceInput = new QLineEdit(m_replaceRow);
    m_replaceInput->setPlaceholderText(QStringLiteral("替换…"));
    auto *replaceOne = new QPushButton(QStringLiteral("替换"), m_replaceRow);
    auto *replaceAll = new QPushButton(QStringLiteral("全部替换"), m_replaceRow);
    h2->addWidget(m_replaceInput, 1);
    h2->addWidget(replaceOne);
    h2->addWidget(replaceAll);
    m_replaceRow->setVisible(false);
    v->addWidget(m_replaceRow);

    // 查找逻辑
    auto searchFrom = [this](const QTextCursor &from, QTextDocument::FindFlags flags) -> QTextCursor {
        return m_editor->document()->find(m_findInput->text(), from, flags);
    };
    auto doSearch = [this, searchFrom]() -> int {
        const QString q = m_findInput->text();
        if (q.isEmpty()) {
            m_findCount->setText(QString());
            return 0;
        }
        int count = 0;
        QTextCursor cur = m_editor->document()->find(q, 0, QTextDocument::FindFlags());
        while (!cur.isNull()) {
            count++;
            cur = searchFrom(cur, QTextDocument::FindFlags());
        }
        return count;
    };
    auto goTo = [this, doSearch, searchFrom](bool backward) {
        const int total = doSearch();
        if (total == 0) {
            m_findCount->setText(QStringLiteral("0/0"));
            return;
        }
        QTextDocument::FindFlags flags;
        if (backward) {
            flags |= QTextDocument::FindBackward;
        }
        QTextCursor found = searchFrom(m_editor->textCursor(), flags);
        if (found.isNull()) {
            // 循环
            QTextCursor start(m_editor->document());
            if (backward) {
                start.movePosition(QTextCursor::End);
            }
            found = searchFrom(start, flags);
        }
        if (!found.isNull()) {
            m_editor->setTextCursor(found);
        }
        m_findCount->setText(QString());
    };

    connect(m_findInput, &QLineEdit::textChanged, this, [this, doSearch]() {
        const int n = doSearch();
        m_findCount->setText(n ? QStringLiteral("1/%1").arg(n) : QStringLiteral("0/0"));
    });
    connect(m_findInput, &QLineEdit::returnPressed, this, [goTo]() { goTo(false); });
    connect(prev, &QPushButton::clicked, this, [goTo]() { goTo(true); });
    connect(next, &QPushButton::clicked, this, [goTo]() { goTo(false); });
    connect(toggleReplace, &QPushButton::clicked, this, [this]() {
        m_replaceRow->setVisible(!m_replaceRow->isVisible());
    });
    connect(close, &QPushButton::clicked, this, [this]() {
        m_findBar->setVisible(false);
        m_replaceRow->setVisible(false);
        m_editor->setFocus();
    });
    connect(replaceOne, &QPushButton::clicked, this, [this]() {
        QTextCursor c = m_editor->textCursor();
        if (c.hasSelection() && c.selectedText() == m_findInput->text()) {
            c.insertText(m_replaceInput->text());
        }
        // 继续找下一个
        QTextCursor found = m_editor->document()->find(m_findInput->text(), m_editor->textCursor());
        if (!found.isNull()) {
            m_editor->setTextCursor(found);
        }
    });
    connect(replaceAll, &QPushButton::clicked, this, [this]() {
        const QString q = m_findInput->text();
        const QString r = m_replaceInput->text();
        if (q.isEmpty()) {
            return;
        }
        QTextCursor c(m_editor->document());
        c.beginEditBlock();
        int count = 0;
        forever {
            QTextCursor found = m_editor->document()->find(q, c);
            if (found.isNull()) {
                break;
            }
            found.insertText(r);
            count++;
            c = found;
        }
        c.endEditBlock();
        if (count > 0) {
            m_findBar->setVisible(false);
            m_replaceRow->setVisible(false);
        }
    });

    return bar;
}

/* ---------------- Compile 标签页 ---------------- */

QWidget *RopToolView::buildCompileTab()
{
    auto *root = new QWidget(this);
    auto *layout = new QVBoxLayout(root);
    layout->setContentsMargins(4, 4, 4, 4);

    auto *addrRow = new QWidget(root);
    auto *addrLayout = new QHBoxLayout(addrRow);
    addrLayout->setContentsMargins(0, 0, 0, 0);
    addrLayout->addWidget(new QLabel(t("leftAddr"), addrRow));
    m_leftAddrInput = new QLineEdit(addrRow);
    m_leftAddrInput->setMaxLength(5);
    m_leftAddrInput->setFixedWidth(70);
    addrLayout->addWidget(m_leftAddrInput);
    addrLayout->addSpacing(8);
    addrLayout->addWidget(new QLabel(t("rightAddr"), addrRow));
    m_rightAddrInput = new QLineEdit(addrRow);
    m_rightAddrInput->setMaxLength(5);
    m_rightAddrInput->setFixedWidth(70);
    addrLayout->addWidget(m_rightAddrInput);
    addrLayout->addStretch(1);
    layout->addWidget(addrRow);

    auto bindAddr = [this](QLineEdit *edit, bool left) {
        connect(edit, &QLineEdit::textChanged, this, [this, edit, left](const QString &) {
            stripHexFilter(edit);
            const QString v = edit->text().isEmpty() ? QStringLiteral("0") : edit->text();
            if (left) {
                m_data.leftStartAddress = v;
            } else {
                m_data.rightStartAddress = v;
            }
            updateCompileTab();
            scheduleWriteBack();
        });
    };
    bindAddr(m_leftAddrInput, true);
    bindAddr(m_rightAddrInput, false);

    auto *actions = new QWidget(root);
    auto *actionsLayout = new QHBoxLayout(actions);
    actionsLayout->setContentsMargins(0, 0, 0, 0);
    auto *btnCopyHex = new QPushButton(t("copyHex"), actions);
    auto *btnCopyDump = new QPushButton(t("copyDump"), actions);
    actionsLayout->addWidget(btnCopyHex);
    actionsLayout->addWidget(btnCopyDump);
    actionsLayout->addStretch(1);
    layout->addWidget(actions);

    connect(btnCopyHex, &QPushButton::clicked, this, &RopToolView::copyHex);
    connect(btnCopyDump, &QPushButton::clicked, this, &RopToolView::copyDump);

    m_compileInfo = new QLabel(root);
    layout->addWidget(m_compileInfo);
    auto *hint = new QLabel(t("compileHint"), root);
    hint->setWordWrap(true);
    hint->setStyleSheet(QStringLiteral("color: palette(placeholder-text);"));
    layout->addWidget(hint);

    m_hexdump = new QTextBrowser(root);
    m_hexdump->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_hexdump->setOpenLinks(false);
    layout->addWidget(m_hexdump, 1);
    connect(m_hexdump, &QTextBrowser::anchorClicked, this, [this](const QUrl &url) {
        if (url.scheme() == QLatin1String("ropbyte")) {
            const int idx = url.path().toInt();
            m_editor->selectByteToken(idx);
            // 显示 #i L: R:（对应原插件 selectByte 的 cursorInfo）
            const int off = idx;
            m_cursorInfo->setText(QStringLiteral("#%1  L:%2  R:%3")
                                      .arg(idx)
                                      .arg(hexAddr(parseBase(m_data.leftStartAddress) + off))
                                      .arg(hexAddr(parseBase(m_data.rightStartAddress) + off)));
        }
    });

    return root;
}

void RopToolView::updateCompileTab()
{
    if (m_leftAddrInput->text() != m_data.leftStartAddress) {
        QSignalBlocker b1(m_leftAddrInput);
        m_leftAddrInput->setText(m_data.leftStartAddress);
    }
    if (m_rightAddrInput->text() != m_data.rightStartAddress) {
        QSignalBlocker b2(m_rightAddrInput);
        m_rightAddrInput->setText(m_data.rightStartAddress);
    }
    updateCompileHexdump();
}

void RopToolView::updateCompileHexdump()
{
    const bool dark = palette().color(QPalette::Window).lightness() < 128;
    const QString addrColor = dark ? QStringLiteral("#858585") : QStringLiteral("#6a737d");
    const QString zeroColor = dark ? QStringLiteral("#4a4a4a") : QStringLiteral("#c0c0c0");

    QString html = QStringLiteral("<style>td { padding: 0 6px; white-space: pre; }</style>");
    const QString hex = m_parseResult.hexChars;
    const long long leftBase = parseBase(m_data.leftStartAddress);
    const long long rightBase = parseBase(m_data.rightStartAddress);
    const int bytes = hex.size() / 2;

    for (int row = 0; row * 16 < bytes; row++) {
        html += QStringLiteral("<div>");
        html += QStringLiteral("<span style=\"color:%1\">%2</span> ").arg(addrColor, hexAddr(leftBase + row * 16));
        for (int col = 0; col < 16 && row * 16 + col < bytes; col++) {
            const int idx = row * 16 + col;
            const QString b = hex.mid(idx * 2, 2);
            const QString color = b == QLatin1String("00") ? zeroColor : QString();
            if (color.isEmpty()) {
                html += QStringLiteral("<a href=\"ropbyte:%1\" style=\"text-decoration:none\">%2</a> ").arg(idx).arg(b);
            } else {
                html += QStringLiteral("<a href=\"ropbyte:%1\" style=\"text-decoration:none;color:%2\">%3</a> ").arg(idx).arg(color, b);
            }
        }
        html += QStringLiteral("<span style=\"color:%1\">%2</span>").arg(addrColor, hexAddr(rightBase + row * 16));
        html += QStringLiteral("</div>");
    }
    if (bytes == 0) {
        html += QStringLiteral("<span style=\"color:palette(placeholder-text)\">%1</span>").arg(escapeHtml(t("noBytes")));
    }
    m_hexdump->setHtml(html);

    const int errorCount = m_parseResult.errorCount;
    m_compileInfo->setText(QStringLiteral("<b>%1</b> bytes · <span style=\"color:%2\">%3 errors</span>")
                               .arg(m_parseResult.totalBytes)
                               .arg(errorCount > 0 ? QStringLiteral("#f48771") : QStringLiteral("palette(text)"))
                               .arg(errorCount));
    const int gadgetCount = m_data.gadgets.size();
    m_bytesInfo->setText(QStringLiteral("%1 bytes · %2 errors").arg(m_parseResult.totalBytes).arg(errorCount));
    if (m_tabs) {
        m_tabs->setTabText(2, gadgetCount > 0 ? QStringLiteral("Gadgets (%1)").arg(gadgetCount)
                                              : QStringLiteral("Gadgets"));
    }
}

void RopToolView::copyHex()
{
    QApplication::clipboard()->setText(m_parseResult.hexChars);
    toast(t("copied"));
}

void RopToolView::copyDump()
{
    const QString hex = m_parseResult.hexChars;
    QStringList dump;
    for (int i = 0; i < hex.size(); i += 32) {
        QString row;
        for (int j = i; j < qMin(i + 32, hex.size()); j += 2) {
            row += hex.mid(j, 2) + QLatin1Char(' ');
        }
        dump.push_back(row.trimmed());
    }
    QApplication::clipboard()->setText(dump.join(QLatin1Char('\n')));
    toast(t("copied"));
}

void RopToolView::jumpToAddress()
{
    QString raw = m_jumpAddr->text().trimmed();
    if (raw.startsWith(QLatin1String("0x"), Qt::CaseInsensitive)) {
        raw.remove(0, 2);
    }
    if (!isHexAddrInput(raw)) {
        toast(t("invalidJumpAddr"), true);
        m_jumpAddr->setFocus();
        return;
    }
    const long long addr = raw.toLongLong(nullptr, 16);
    const long long base = m_jumpSide->currentData().toString() == QLatin1String("left")
        ? parseBase(m_data.leftStartAddress)
        : parseBase(m_data.rightStartAddress);
    const int total = m_parseResult.totalBytes;
    if (total == 0) {
        toast(t("noJumpBytes"), true);
        return;
    }
    const long long byteIndex = addr - base;
    if (byteIndex < 0 || byteIndex >= total) {
        toast(QStringLiteral("0x%1 ~ 0x%2").arg(hexAddr(base), hexAddr(base + total - 1)), true);
        return;
    }
    m_tabs->setCurrentIndex(1); // compile
    m_editor->selectByteToken((int)byteIndex);
    m_cursorInfo->setText(QStringLiteral("#%1  L:%2  R:%3")
                              .arg((int)byteIndex)
                              .arg(hexAddr(parseBase(m_data.leftStartAddress) + byteIndex))
                              .arg(hexAddr(parseBase(m_data.rightStartAddress) + byteIndex)));
}

/* ---------------- Gadgets 标签页 ---------------- */

QWidget *RopToolView::buildGadgetsTab()
{
    auto *root = new QWidget(this);
    auto *layout = new QVBoxLayout(root);
    layout->setContentsMargins(4, 4, 4, 4);

    auto *bar = new QWidget(root);
    auto *barLayout = new QHBoxLayout(bar);
    barLayout->setContentsMargins(0, 0, 0, 0);
    m_gadgetSearch = new QLineEdit(bar);
    m_gadgetSearch->setPlaceholderText(t("gadgetSearchPh"));
    auto *btnAdd = new QPushButton(t("add"), bar);
    auto *btnImport = new QPushButton(t("import"), bar);
    auto *btnExport = new QPushButton(t("export"), bar);
    barLayout->addWidget(m_gadgetSearch, 1);
    barLayout->addWidget(btnAdd);
    barLayout->addWidget(btnImport);
    barLayout->addWidget(btnExport);
    layout->addWidget(bar);

    m_gadgetList = new QListWidget(root);
    layout->addWidget(m_gadgetList, 1);
    m_gadgetDisasm = new QPlainTextEdit(root);
    m_gadgetDisasm->setReadOnly(true);
    m_gadgetDisasm->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_gadgetDisasm->setVisible(false);
    layout->addWidget(m_gadgetDisasm, 1);

    connect(m_gadgetSearch, &QLineEdit::textChanged, this, &RopToolView::refreshGadgetList);
    connect(btnAdd, &QPushButton::clicked, this, &RopToolView::addGadget);
    connect(btnImport, &QPushButton::clicked, this, &RopToolView::importGadgets);
    connect(btnExport, &QPushButton::clicked, this, &RopToolView::exportGadgets);
    connect(m_gadgetList, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *item) {
        editGadget(item->data(Qt::UserRole).toInt());
    });
    connect(m_gadgetList, &QListWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
        QListWidgetItem *item = m_gadgetList->itemAt(pos);
        if (!item) {
            return;
        }
        const int index = item->data(Qt::UserRole).toInt();
        QMenu menu(this);
        QAction *editAct = menu.addAction(t("edit"));
        QAction *delAct = menu.addAction(t("delete"));
        QAction *chosen = menu.exec(m_gadgetList->mapToGlobal(pos));
        if (chosen == editAct) {
            editGadget(index);
        } else if (chosen == delAct) {
            m_data.gadgets.remove(index);
            refreshGadgetList();
            markChanged();
        }
    });
    connect(m_gadgetList, &QListWidget::itemSelectionChanged, this, &RopToolView::updateGadgetDisasmPane);
    m_gadgetList->setContextMenuPolicy(Qt::CustomContextMenu);

    return root;
}

void RopToolView::refreshGadgetList()
{
    const QString q = m_gadgetSearch->text().trimmed().toLower();
    m_gadgetList->clear();
    if (m_data.gadgets.isEmpty()) {
        m_gadgetList->addItem(t("noGadgets"));
        m_gadgetDisasm->setVisible(false);
        return;
    }
    for (int i = 0; i < m_data.gadgets.size(); i++) {
        const RopGadget &g = m_data.gadgets.at(i);
        if (!q.isEmpty()) {
            bool match = g.name.toLower().contains(q) || g.addr.toLower().contains(q)
                || g.desc.toLower().contains(q);
            for (const RopTag &tag : g.tags) {
                match = match || tag.name.toLower().contains(q);
            }
            if (!match) {
                continue;
            }
        }
        QString tags;
        for (const RopTag &tag : g.tags) {
            tags += QStringLiteral("[%1] ").arg(tag.name);
        }
        auto *item = new QListWidgetItem(
            QStringLiteral("%1   %2   %3\n%4").arg(g.name, g.addr, tags.trimmed(), g.desc),
            m_gadgetList);
        item->setData(Qt::UserRole, i);
        if (g.name.isEmpty()) {
            item->setText(QStringLiteral("(未命名)   %1\n%2").arg(g.addr, g.desc));
        }
    }
    if (m_gadgetList->count() == 0) {
        m_gadgetList->addItem(t("noGadgetMatch"));
    }
    m_gadgetDisasm->setVisible(m_plugin->settings()->showGadgetDisasm && m_disasLoaded);
    updateGadgetDisasmPane();
}

void RopToolView::updateGadgetDisasmPane()
{
    if (!m_plugin->settings()->showGadgetDisasm || !m_disasLoaded) {
        m_gadgetDisasm->setVisible(false);
        return;
    }
    QListWidgetItem *item = m_gadgetList->currentItem();
    if (!item) {
        m_gadgetDisasm->setVisible(true);
        m_gadgetDisasm->setPlainText(QString());
        return;
    }
    const int index = item->data(Qt::UserRole).toInt();
    if (index < 0 || index >= m_data.gadgets.size()) {
        return;
    }
    QString addr = m_data.gadgets.at(index).addr;
    addr.remove(QRegularExpression(QStringLiteral("^0x"), QRegularExpression::CaseInsensitiveOption));
    const QStringList snippet = disasSnippet(m_disasMap, addr.toInt(nullptr, 16));
    m_gadgetDisasm->setPlainText(snippet.isEmpty() ? t("disasmFail") : snippet.join(QLatin1Char('\n')));
    m_gadgetDisasm->setVisible(true);
}

void RopToolView::addGadget()
{
    m_gadgetSearch->clear();
    RopGadget g;
    m_data.gadgets.push_back(g);
    refreshGadgetList();
    editGadget(m_data.gadgets.size() - 1);
}

void RopToolView::editGadget(int index)
{
    if (index < 0 || index >= m_data.gadgets.size()) {
        return;
    }
    RopGadget g = m_data.gadgets.at(index);

    QDialog dialog(this);
    dialog.setWindowTitle(t("gadgetsTitle"));
    auto *form = new QFormLayout(&dialog);

    auto *nameEdit = new QLineEdit(g.name, &dialog);
    auto *addrEdit = new QLineEdit(g.addr, &dialog);
    auto *descEdit = new QPlainTextEdit(g.desc, &dialog);
    descEdit->setFixedHeight(64);
    form->addRow(t("name"), nameEdit);
    form->addRow(t("addrHex"), addrEdit);
    form->addRow(t("desc"), descEdit);

    // 标签编辑
    auto *tagList = new QListWidget(&dialog);
    for (const RopTag &tag : g.tags) {
        tagList->addItem(QStringLiteral("%1 (%2)").arg(tag.name, tag.color));
    }
    auto *tagRow = new QWidget(&dialog);
    auto *tagLayout = new QHBoxLayout(tagRow);
    tagLayout->setContentsMargins(0, 0, 0, 0);
    auto *tagName = new QLineEdit(tagRow);
    tagName->setPlaceholderText(t("tagNamePh"));
    auto *tagColor = new QComboBox(tagRow);
    for (const char *c : TAG_COLORS) {
        tagColor->addItem(QString::fromLatin1(c));
    }
    auto *btnAddTag = new QPushButton(t("addTag"), tagRow);
    auto *btnDelTag = new QPushButton(QStringLiteral("-"), tagRow);
    btnDelTag->setFixedWidth(28);
    tagLayout->addWidget(tagName, 1);
    tagLayout->addWidget(tagColor);
    tagLayout->addWidget(btnAddTag);
    tagLayout->addWidget(btnDelTag);
    form->addRow(t("tag"), tagRow);
    form->addRow(QString(), tagList);

    connect(btnAddTag, &QPushButton::clicked, this, [&]() {
        const QString n = tagName->text().trimmed();
        if (!n.isEmpty()) {
            tagList->addItem(QStringLiteral("%1 (%2)").arg(n, tagColor->currentText()));
            tagName->clear();
        }
    });
    connect(btnDelTag, &QPushButton::clicked, this, [&]() {
        const int row = tagList->currentRow();
        if (row >= 0) {
            delete tagList->takeItem(row);
        }
    });

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addRow(buttons);

    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    QString name = nameEdit->text();
    name.remove(QRegularExpression(QStringLiteral("\\s+")));
    if (name.trimmed().isEmpty()) {
        return;
    }
    QString addr = addrEdit->text().toUpper();
    addr.remove(QRegularExpression(QStringLiteral("[^0-9A-F]")));
    while (addr.size() < 5) {
        addr.prepend(QLatin1Char('0'));
    }
    g.name = name;
    g.addr = addr;
    g.desc = descEdit->toPlainText();
    g.tags.clear();
    for (int i = 0; i < tagList->count(); i++) {
        const QString text = tagList->item(i)->text();
        const int p = text.lastIndexOf(QStringLiteral(" ("));
        RopTag tag;
        tag.name = p > 0 ? text.left(p) : text;
        tag.color = p > 0 ? text.mid(p + 2, text.size() - p - 3) : QStringLiteral("gray");
        g.tags.push_back(tag);
    }
    m_data.gadgets[index] = g;
    refreshGadgetList();
    markChanged();
}

void RopToolView::importGadgets()
{
    const QString path = QFileDialog::getOpenFileName(this, t("importTitle"), QString(),
                                                      QStringLiteral("Gadgets JSON (*.json)"));
    if (path.isEmpty()) {
        toast(t("cancelled"));
        return;
    }
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        toast(t("importFail") + f.errorString(), true);
        return;
    }
    const auto r = parseGadgetsJson(QString::fromUtf8(f.readAll()));
    if (!r.ok) {
        toast(t("importFail") + r.error, true);
        return;
    }
    QMenu modeMenu(this);
    QAction *replace = modeMenu.addAction(QStringLiteral("覆盖（替换全部）"));
    QAction *merge = modeMenu.addAction(QStringLiteral("补全（仅添加缺失的）"));
    QAction *chosen = modeMenu.exec(QCursor::pos());
    if (chosen == replace) {
        m_data.gadgets = r.gadgets;
    } else if (chosen == merge) {
        // 按地址判重，地址重复（哪怕说明不同）保留原有
        for (const RopGadget &g : r.gadgets) {
            bool dup = false;
            if (!g.addr.isEmpty()) {
                for (const RopGadget &x : m_data.gadgets) {
                    if (x.addr == g.addr) {
                        dup = true;
                        break;
                    }
                }
            } else {
                for (const RopGadget &x : m_data.gadgets) {
                    if (x.name == g.name) {
                        dup = true;
                        break;
                    }
                }
            }
            if (!dup) {
                m_data.gadgets.push_back(g);
            }
        }
    } else {
        toast(t("cancelled"));
        return;
    }
    refreshGadgetList();
    markChanged();
    toast(t("imported"));
}

void RopToolView::exportGadgets()
{
    const QString path = QFileDialog::getSaveFileName(this, t("exportTitle"), QStringLiteral("gadgets.json"),
                                                      QStringLiteral("Gadgets JSON (*.json)"));
    if (path.isEmpty()) {
        toast(t("cancelled"));
        return;
    }
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        toast(t("exportFail") + f.errorString(), true);
        return;
    }
    QJsonDocument doc;
    QJsonArray arr;
    for (const RopGadget &g : m_data.gadgets) {
        QJsonObject go;
        go.insert(QLatin1String("name"), g.name);
        go.insert(QLatin1String("addr"), g.addr);
        go.insert(QLatin1String("desc"), g.desc);
        QJsonArray tags;
        for (const RopTag &tag : g.tags) {
            QJsonObject to;
            to.insert(QLatin1String("name"), tag.name);
            to.insert(QLatin1String("color"), tag.color);
            tags.append(to);
        }
        go.insert(QLatin1String("tags"), tags);
        arr.append(go);
    }
    doc.setArray(arr);
    f.write(doc.toJson(QJsonDocument::Indented));
    f.close();
    toast(t("exported"));
}

/* ---------------- Market 标签页 ---------------- */

QWidget *RopToolView::buildMarketTab()
{
    auto *root = new QWidget(this);
    auto *layout = new QVBoxLayout(root);
    layout->setContentsMargins(4, 4, 4, 4);

    auto *bar = new QWidget(root);
    auto *barLayout = new QHBoxLayout(bar);
    barLayout->setContentsMargins(0, 0, 0, 0);
    m_marketSearch = new QLineEdit(bar);
    m_marketSearch->setPlaceholderText(t("marketSearchPh"));
    auto *btnPublish = new QPushButton(t("publish"), bar);
    barLayout->addWidget(m_marketSearch, 1);
    barLayout->addWidget(btnPublish);
    layout->addWidget(bar);

    m_marketList = new QListWidget(root);
    m_marketList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    layout->addWidget(m_marketList, 1);
    m_marketList->addItem(t("marketEmpty"));

    connect(m_marketSearch, &QLineEdit::textChanged, this, &RopToolView::refreshMarketList);
    connect(btnPublish, &QPushButton::clicked, this, &RopToolView::openPublishDialog);
    return root;
}

void RopToolView::refreshMarketList()
{
    m_marketList->clear();
    if (m_marketLoading) {
        m_marketList->addItem(t("loading"));
        return;
    }
    if (!m_marketError.isEmpty()) {
        m_marketList->addItem(t("loadFail") + m_marketError);
        return;
    }
    const QString q = m_marketSearch->text().trimmed().toLower();
    QVector<MarketItem> items;
    for (const MarketItem &it : m_marketItems) {
        if (q.isEmpty() || it.name.toLower().contains(q) || it.author.toLower().contains(q)
            || it.model.toLower().contains(q) || it.description.toLower().contains(q)) {
            items.push_back(it);
        }
    }
    if (items.isEmpty()) {
        m_marketList->addItem(q.isEmpty() ? t("marketEmpty") : t("noMarketMatch"));
        return;
    }
    QVector<MarketItem> featured;
    QVector<MarketItem> normal;
    for (const MarketItem &it : items) {
        if (it.featured) {
            featured.push_back(it);
        } else {
            normal.push_back(it);
        }
    }
    auto addCard = [this](const MarketItem &it, bool isFeatured) {
        auto *item = new QListWidgetItem(m_marketList);
        item->setData(Qt::UserRole, it.id);
        QString title = it.name.isEmpty() ? t("unnamed") : it.name;
        if (isFeatured) {
            title += QStringLiteral(" ★");
        }
        auto *card = new QWidget(this);
        auto *cardLayout = new QHBoxLayout(card);
        cardLayout->setContentsMargins(6, 4, 6, 4);
        auto *info = new QLabel(QStringLiteral("<b>%1</b><br/><span style=\"color:palette(placeholder-text)\">%2%3%4</span>")
                                    .arg(escapeHtml(title),
                                         t("byAuthor") + escapeHtml(it.author.isEmpty() ? QStringLiteral("-") : it.author),
                                         QStringLiteral("  "),
                                         t("byModel") + escapeHtml(it.model.isEmpty() ? QStringLiteral("-") : it.model))
                                + (it.description.isEmpty()
                                       ? QString()
                                       : QStringLiteral("<br/>%1").arg(escapeHtml(it.description))),
                                card);
        info->setTextFormat(Qt::RichText);
        info->setAlignment(Qt::AlignLeft | Qt::AlignTop);
        info->setWordWrap(true);
        auto *btn = new QPushButton(m_downloadingId == it.id ? t("downloading") : t("download"), card);
        const QString id = it.id;
        connect(btn, &QPushButton::clicked, this, [this, id]() {
            downloadMarketItem(id);
        });
        btn->setEnabled(m_downloadingId != it.id);
        cardLayout->addWidget(info, 1);
        cardLayout->addWidget(btn);
        // 宽度贴合视口（出现下载按钮的最小宽度），高度按内容；避免横向滚动条
        const int w = qMax(240, m_marketList->viewport()->width() - 10);
        item->setSizeHint(QSize(w, card->sizeHint().height()));
        m_marketList->setItemWidget(item, card);
    };
    for (const MarketItem &it : featured) {
        addCard(it, true);
    }
    for (const MarketItem &it : normal) {
        addCard(it, false);
    }
}

void RopToolView::downloadMarketItem(const QString &id)
{
    QString name;
    for (const MarketItem &it : m_marketItems) {
        if (it.id == id) {
            name = it.name;
            break;
        }
    }
    m_downloadingId = id;
    m_pendingDownloadName = name;
    refreshMarketList();
    m_plugin->market()->fetchItem(id);
}

void RopToolView::checkMarketUnread()
{
    // 仅查未读数（不标记已读）；打开市场标签页后由下面的连接标记已读
    connect(m_plugin->market(), &MarketClient::listFinished, this,
            [this](const MarketListResult &r) {
                if (!r.ok) {
                    return;
                }
                int unread = 0;
                for (const MarketItem &it : r.items) {
                    if (MarketClient::itemTime(it) > m_plugin->settings()->marketLastSeen) {
                        unread++;
                    }
                }
                updateMarketBadge(unread);
            },
            Qt::UniqueConnection);
    m_plugin->market()->fetchList();
}

void RopToolView::updateMarketBadge(int unread)
{
    const QString base = t("market");
    if (unread > 0) {
        m_tabs->setTabText(3, QStringLiteral("%1 (%2)").arg(base).arg(unread > 99 ? QStringLiteral("99+") : QString::number(unread)));
    } else {
        m_tabs->setTabText(3, base);
    }
}

/* ---------------- 发布对话框 ---------------- */

void RopToolView::openPublishDialog()
{
    QDialog dialog(this);
    dialog.setWindowTitle(t("publishTitle"));
    dialog.resize(420, 480);
    auto *form = new QFormLayout(&dialog);

    const QString docName = m_doc ? m_doc->url().fileName() : QString();
    auto *nameEdit = new QLineEdit(docName, &dialog);
    QString nameDefault = docName;
    nameDefault.remove(QRegularExpression(QStringLiteral("\\.rop$"),
                                          QRegularExpression::CaseInsensitiveOption));
    nameEdit->setText(nameDefault);
    auto *authorEdit = new QLineEdit(&dialog);
    authorEdit->setPlaceholderText(t("authorPh"));
    auto *modelCombo = new QComboBox(&dialog);
    modelCombo->addItem(t("modelSel"), QString());
    modelCombo->addItem(QStringLiteral("fx-991CNX (VerC)"), QStringLiteral("fx-991CNX (VerC)"));
    modelCombo->addItem(QStringLiteral("fx-991CNX (VerF)"), QStringLiteral("fx-991CNX (VerF)"));
    modelCombo->addItem(t("other"), QStringLiteral("other"));
    auto *otherModel = new QLineEdit(&dialog);
    otherModel->setPlaceholderText(t("otherModelPh"));
    otherModel->setVisible(false);
    auto *descEdit = new QPlainTextEdit(&dialog);
    descEdit->setPlaceholderText(t("descPh"));
    auto *challengeHint = new QLabel(t("challengeLoading"), &dialog);
    challengeHint->setWordWrap(true);
    auto *challengeAnswer = new QLineEdit(&dialog);
    challengeAnswer->setMaxLength(9);
    challengeAnswer->setPlaceholderText(t("challengePh"));
    challengeAnswer->setEnabled(false);

    form->addRow(t("progName"), nameEdit);
    form->addRow(t("author"), authorEdit);
    form->addRow(t("model"), modelCombo);
    form->addRow(QString(), otherModel);
    form->addRow(t("desc"), descEdit);
    form->addRow(t("challenge"), challengeHint);
    form->addRow(QString(), challengeAnswer);

    connect(modelCombo, &QComboBox::currentIndexChanged, &dialog, [modelCombo, otherModel](int) {
        otherModel->setVisible(modelCombo->currentData().toString() == QLatin1String("other"));
    });

    // 内行验证：题目与发布结果都以对话框为接收者，对话框销毁后自动断开
    auto okButton = std::make_shared<QPushButton *>(nullptr);
    m_challengeToken.clear();
    m_challengeLoading = true;
    auto fetch = [this, &dialog, challengeHint, challengeAnswer]() {
        m_challengeToken.clear();
        m_challengeLoading = true;
        challengeAnswer->setEnabled(false);
        challengeHint->setText(t("challengeLoading"));
        auto conn = std::make_shared<QMetaObject::Connection>();
        *conn = connect(m_plugin->market(), &MarketClient::challengeFinished, &dialog,
                        [this, conn, challengeHint, challengeAnswer](const MarketChallengeResult &r) {
                            QObject::disconnect(*conn);
                            m_challengeLoading = false;
                            if (r.ok) {
                                m_challengeToken = r.token;
                                m_challengeOffset = r.offset;
                                challengeHint->setText(t("challengeText").replace(
                                    QStringLiteral("{addr}"), hexAddr(r.offset)));
                                challengeAnswer->setEnabled(true);
                            } else {
                                challengeHint->setText(t("challengeFail") + r.error);
                            }
                        });
        m_plugin->market()->fetchChallenge();
    };
    fetch();

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    *okButton = buttons->button(QDialogButtonBox::Ok);
    (*okButton)->setText(t("publish"));
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addRow(buttons);

    // 确认发布：校验表单 → 提交（与原插件 confirmPublish 一致）
    auto doPublish = [this, &dialog, nameEdit, authorEdit, modelCombo, otherModel, descEdit,
                      challengeAnswer, btn = okButton, &fetch]() {
        const QString name = nameEdit->text().trimmed();
        const QString author = authorEdit->text().trimmed();
        QString model = modelCombo->currentData().toString();
        if (model == QLatin1String("other")) {
            model = otherModel->text().trimmed();
        }
        const QString description = descEdit->toPlainText().trimmed();
        if (name.isEmpty()) {
            toast(t("needName"), true);
            return;
        }
        if (author.isEmpty()) {
            toast(t("needAuthor"), true);
            return;
        }
        if (model.isEmpty()) {
            toast(t("needModel"), true);
            return;
        }
        if (description.isEmpty()) {
            toast(t("needDesc"), true);
            return;
        }
        if (m_challengeToken.isEmpty()) {
            if (m_challengeLoading) {
                toast(t("challengeWait"), true);
            } else {
                toast(t("challengeRetry"), true);
                fetch();
            }
            return;
        }
        QString answer = challengeAnswer->text();
        answer.remove(QRegularExpression(QStringLiteral("[^0-9a-fA-F]")));
        answer = answer.toLower();
        if (!QRegularExpression(QStringLiteral("^[0-9a-f]{4}$")).match(answer).hasMatch()) {
            toast(t("needAnswer"), true);
            return;
        }
        (*btn)->setEnabled(false);
        m_plugin->market()->publish(name, author, model, description, serializeRopDocument(m_data),
                                    m_challengeToken, answer);
    };
    connect(*okButton, &QPushButton::clicked, &dialog, doPublish);

    connect(m_plugin->market(), &MarketClient::publishFinished, &dialog,
            [this, &dialog, challengeHint, challengeAnswer, btn = okButton, &fetch](const MarketPublishResult &r) {
                (*btn)->setEnabled(true);
                if (r.ok) {
                    toast(t("published"));
                    m_publishDialogClosedOk = true;
                    dialog.accept();
                } else if (r.code == QLatin1String("wrong")) {
                    toast(t("challengeWrong"), true);
                    challengeAnswer->clear();
                    fetch(); // 已更换新题目
                } else if (r.code == QLatin1String("expired")) {
                    toast(t("challengeExpired"), true);
                    challengeAnswer->clear();
                    fetch();
                } else {
                    toast(t("publishFail") + r.error, true);
                }
            });

    m_publishDialogClosedOk = false;
    const int code = dialog.exec();

    if (code != QDialog::Accepted || !m_publishDialogClosedOk) {
        return;
    }

    // 发布成功后刷新列表
    m_marketLoading = true;
    refreshMarketList();
    auto conn = std::make_shared<QMetaObject::Connection>();
    *conn = connect(m_plugin->market(), &MarketClient::listFinished, this, [this, conn](const MarketListResult &r) {
        disconnect(*conn);
        m_marketLoading = false;
        if (r.ok) {
            m_marketItems = r.items;
            m_marketError.clear();
        } else {
            m_marketError = r.error;
        }
        refreshMarketList();
    });
    m_plugin->market()->fetchList();
}

/* ---------------- Disas 标签页 ---------------- */

QWidget *RopToolView::buildDisasTab()
{
    auto *root = new QWidget(this);
    auto *layout = new QVBoxLayout(root);
    layout->setContentsMargins(4, 4, 4, 4);

    auto *bar = new QWidget(root);
    auto *barLayout = new QHBoxLayout(bar);
    barLayout->setContentsMargins(0, 0, 0, 0);
    m_disasAddr = new QLineEdit(bar);
    m_disasAddr->setPlaceholderText(t("disasAddrPh"));
    m_disasStatus = new QLabel(bar);
    barLayout->addWidget(m_disasAddr, 1);
    barLayout->addWidget(m_disasStatus);
    layout->addWidget(bar);

    m_disasView = new QPlainTextEdit(root);
    m_disasView->setReadOnly(true);
    m_disasView->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    layout->addWidget(m_disasView, 1);

    connect(m_disasAddr, &QLineEdit::returnPressed, this, &RopToolView::disasJump);
    return root;
}

void RopToolView::loadDisasForDocument()
{
    m_disasMap.clear();
    m_disasFile.clear();
    m_disasLoaded = false;
    loadSidecar();
    if (m_sidecarDisasPath.isEmpty()) {
        renderDisas();
        return;
    }
    if (m_plugin->loadDisasCache(m_sidecarDisasPath, m_disasMap)) {
        m_disasFile = QFileInfo(m_sidecarDisasPath).fileName();
        m_disasLoaded = true;
    }
    renderDisas();
}

void RopToolView::renderDisas()
{
    if (!m_disasLoaded) {
        m_disasView->setPlainText(t("disasNeedFile"));
        return;
    }
    QStringList lines;
    for (auto it = m_disasMap.constBegin(); it != m_disasMap.constEnd(); ++it) {
        for (const QString &l : it.value()) {
            lines.push_back(l);
        }
    }
    m_disasView->setPlainText(lines.join(QLatin1Char('\n')));
}

int RopToolView::findDisasAddrIndex(const QString &queryRaw) const
{
    // 移植 editor.js 的 findDisasAddrIndex：精确 / 通配 X / 后缀 / 包含 / 最近
    QString q = queryRaw.toUpper();
    if (q.startsWith(QLatin1String("0X"))) {
        q.remove(0, 2);
    }
    QList<int> addrs = m_disasMap.keys();
    auto matchExact = [&addrs](const QString &a) -> int {
        for (int i = 0; i < addrs.size(); i++) {
            if (QString::number(addrs.at(i), 16).toUpper() == a) {
                return i;
            }
        }
        return -1;
    };
    int idx = matchExact(q);
    if (idx >= 0) {
        return idx;
    }
    if (q.contains(QLatin1Char('X'))) {
        const QString pattern = q;
        QString reStr;
        for (const QChar c : pattern) {
            reStr += c == QLatin1Char('X') ? QStringLiteral("[0-9A-F]") : QString(c);
        }
        QRegularExpression full(QRegularExpression::anchoredPattern(reStr));
        QRegularExpression sub(reStr);
        for (int i = 0; i < addrs.size(); i++) {
            if (full.match(QString::number(addrs.at(i), 16).toUpper()).hasMatch()) {
                return i;
            }
        }
        for (int i = 0; i < addrs.size(); i++) {
            if (sub.match(QString::number(addrs.at(i), 16).toUpper()).hasMatch()) {
                return i;
            }
        }
        return -1;
    }
    static const QRegularExpression hexOnly(QStringLiteral("^[0-9A-F]+$"));
    if (hexOnly.match(q).hasMatch()) {
        for (int i = 0; i < addrs.size(); i++) {
            if (QString::number(addrs.at(i), 16).toUpper().endsWith(q)) {
                return i;
            }
        }
        for (int i = 0; i < addrs.size(); i++) {
            if (QString::number(addrs.at(i), 16).toUpper().contains(q)) {
                return i;
            }
        }
        bool ok = false;
        const long long target = q.toLongLong(&ok, 16);
        if (ok) {
            for (int i = 0; i < addrs.size(); i++) {
                if ((long long)addrs.at(i) >= target) {
                    return i;
                }
            }
            return addrs.size() - 1;
        }
    }
    return -1;
}

void RopToolView::disasJump()
{
    const QString raw = m_disasAddr->text().trimmed();
    if (raw.isEmpty() || !m_disasLoaded) {
        return;
    }
    int targetIdx = -1;
    QString status;
    auto gm = QRegularExpression(QStringLiteral("^#([^;]+);?$"), QRegularExpression::CaseInsensitiveOption)
                  .match(raw.startsWith(QLatin1String("0x"), Qt::CaseInsensitive) ? raw.mid(2) : raw);
    if (gm.hasMatch()) {
        const QString gName = gm.captured(1);
        const RopGadget *g = nullptr;
        for (const RopGadget &x : m_data.gadgets) {
            if (x.name == gName) {
                g = &x;
                break;
            }
        }
        if (g) {
            QString addr = g->addr.toUpper();
            addr.remove(QRegularExpression(QStringLiteral("^0X")));
            targetIdx = findDisasAddrIndex(addr);
            if (targetIdx < 0) {
                status = QStringLiteral("未找到地址 %1").arg(addr);
            }
        } else {
            status = QStringLiteral("未找到 gadget: %1").arg(gName);
        }
    } else {
        targetIdx = findDisasAddrIndex(raw);
        if (targetIdx < 0) {
            status = QStringLiteral("未找到匹配地址");
        }
    }
    if (targetIdx < 0) {
        m_disasStatus->setText(status);
        return;
    }

    // 目标地址在展开行列表中的起始行
    QList<int> addrs = m_disasMap.keys();
    int lineStart = 0;
    for (int i = 0; i < targetIdx; i++) {
        lineStart += m_disasMap.value(addrs.at(i)).size();
    }

    // 展开行列表（与 renderDisas 的顺序一致），用于查找终止指令行
    QStringList flatLines;
    for (auto it = m_disasMap.constBegin(); it != m_disasMap.constEnd(); ++it) {
        for (const QString &l : it.value()) {
            flatLines.push_back(l);
        }
    }
    int termLine = -1;
    for (int i = lineStart; i < flatLines.size(); i++) {
        if (isTerminalLine(flatLines.at(i))) {
            termLine = i;
            break;
        }
    }

    QTextDocument *doc = m_disasView->document();
    QTextBlock targetBlock = doc->findBlockByNumber(lineStart);

    QList<QTextEdit::ExtraSelection> selections;
    if (targetBlock.isValid()) {
        QTextEdit::ExtraSelection sel;
        sel.cursor = QTextCursor(targetBlock);
        sel.cursor.select(QTextCursor::LineUnderCursor);
        sel.format.setBackground(palette().alternateBase());
        selections.push_back(sel);
        m_disasView->setTextCursor(QTextCursor(targetBlock));
        m_disasView->ensureCursorVisible();
    }
    if (termLine >= 0) {
        QTextBlock termBlock = doc->findBlockByNumber(termLine);
        if (termBlock.isValid()) {
            QTextEdit::ExtraSelection sel;
            sel.cursor = QTextCursor(termBlock);
            sel.cursor.select(QTextCursor::LineUnderCursor);
            sel.format.setForeground(QColor(0x5c, 0x94, 0x0d));
            selections.push_back(sel);
        }
    }
    m_disasView->setExtraSelections(selections);

    const QString addrText = QString::number(addrs.at(targetIdx), 16).toUpper();
    m_disasStatus->setText((termLine >= 0 ? QStringLiteral("行 %1 ").arg(termLine + 1) : QString()) + addrText);
}

/* ---------------- Settings 标签页 ---------------- */

QWidget *RopToolView::buildSettingsTab()
{
    auto *root = new QWidget(this);
    auto *layout = new QVBoxLayout(root);
    layout->setContentsMargins(8, 8, 8, 8);

    auto *langRow = new QWidget(root);
    auto *langLayout = new QHBoxLayout(langRow);
    langLayout->setContentsMargins(0, 0, 0, 0);
    langLayout->addWidget(new QLabel(t("lang"), langRow));
    m_selLanguage = new QComboBox(langRow);
    m_selLanguage->addItem(QStringLiteral("简体中文"), QStringLiteral("zh-CN"));
    m_selLanguage->addItem(QStringLiteral("English"), QStringLiteral("en"));
    langLayout->addWidget(m_selLanguage);
    langLayout->addStretch(1);
    layout->addWidget(langRow);
    connect(m_selLanguage, &QComboBox::currentIndexChanged, this, [this](int) {
        m_plugin->settings()->language = m_selLanguage->currentData().toString();
        m_plugin->settings()->markChanged();
    });

    m_chkWelcomeStartup = new QCheckBox(t("welcomeStartup"), root);
    layout->addWidget(m_chkWelcomeStartup);
    connect(m_chkWelcomeStartup, &QCheckBox::toggled, this, [this](bool on) {
        m_plugin->settings()->showWelcomeOnStartup = on;
        m_plugin->settings()->save();
    });

    m_chkDisasm = new QCheckBox(t("disasmExp"), root);
    layout->addWidget(m_chkDisasm);
    auto *disasmHint = new QLabel(t("disasmHint"), root);
    disasmHint->setWordWrap(true);
    disasmHint->setStyleSheet(QStringLiteral("color: palette(placeholder-text);"));
    layout->addWidget(disasmHint);
    connect(m_chkDisasm, &QCheckBox::toggled, this, [this](bool on) {
        m_plugin->settings()->showGadgetDisasm = on;
        m_plugin->settings()->markChanged();
    });

    auto *disasRow = new QWidget(root);
    auto *disasLayout = new QHBoxLayout(disasRow);
    disasLayout->setContentsMargins(0, 0, 0, 0);
    auto *btnChooseDisas = new QPushButton(t("chooseFile"), disasRow);
    m_disasFileLabel = new QLabel(disasRow);
    disasLayout->addWidget(btnChooseDisas);
    disasLayout->addWidget(m_disasFileLabel, 1);
    layout->addWidget(disasRow);
    connect(btnChooseDisas, &QPushButton::clicked, this, [this]() {
        const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("选择 _disas 反汇编文件"), QString(),
                                                          QStringLiteral("Disassembly (_disas) (*.txt *.disas *.asm);;All Files (*)"));
        if (path.isEmpty()) {
            return;
        }
        saveSidecar(path);
        loadDisasForDocument();
        toast(t("disasmLoaded") + m_disasFile);
        syncSettingsUi();
    });

    m_chkHoverDisasm = new QCheckBox(t("disasmHoverExp"), root);
    layout->addWidget(m_chkHoverDisasm);
    auto *hoverHint = new QLabel(t("disasmHoverHint"), root);
    hoverHint->setWordWrap(true);
    hoverHint->setStyleSheet(QStringLiteral("color: palette(placeholder-text);"));
    layout->addWidget(hoverHint);
    connect(m_chkHoverDisasm, &QCheckBox::toggled, this, [this](bool on) {
        m_plugin->settings()->showGadgetHoverDisasm = on;
        m_plugin->settings()->markChanged();
    });

    auto *portRow = new QWidget(root);
    auto *portLayout = new QHBoxLayout(portRow);
    portLayout->setContentsMargins(0, 0, 0, 0);
    portLayout->addWidget(new QLabel(QStringLiteral("CasioEmuMsvc MCP Port"), portRow));
    m_portSpin = new QSpinBox(portRow);
    m_portSpin->setRange(1, 65535);
    portLayout->addWidget(m_portSpin);
    portLayout->addStretch(1);
    layout->addWidget(portRow);
    connect(m_portSpin, &QSpinBox::valueChanged, this, [this](int v) {
        m_plugin->settings()->casioemuMcpPort = v;
        m_plugin->settings()->save();
    });

    layout->addStretch(1);
    return root;
}

void RopToolView::syncSettingsUi()
{
    const Settings *s = m_plugin->settings();
    const int langIdx = m_selLanguage->findData(s->language);
    if (langIdx >= 0 && m_selLanguage->currentIndex() != langIdx) {
        QSignalBlocker b(m_selLanguage);
        m_selLanguage->setCurrentIndex(langIdx);
    }
    QSignalBlocker b1(m_chkWelcomeStartup);
    m_chkWelcomeStartup->setChecked(s->showWelcomeOnStartup);
    QSignalBlocker b2(m_chkDisasm);
    m_chkDisasm->setChecked(s->showGadgetDisasm);
    QSignalBlocker b3(m_chkHoverDisasm);
    m_chkHoverDisasm->setChecked(s->showGadgetHoverDisasm && s->showGadgetDisasm);
    QSignalBlocker b4(m_portSpin);
    m_portSpin->setValue(s->casioemuMcpPort);
    m_disasFileLabel->setText(m_disasFile.isEmpty() ? QString() : t("disasmLoaded") + m_disasFile);
    if (m_tabs->currentIndex() == 4) {
        m_tabs->setTabVisible(4, m_disasLoaded && s->showGadgetDisasm);
    }
    m_gadgetDisasm->setVisible(s->showGadgetDisasm && m_disasLoaded);
    refreshGadgetList();
}

/* ---------------- 模拟器覆写 ---------------- */

void RopToolView::setEmuStatus(const QString &msg, const QString &kind)
{
    if (msg.isEmpty()) {
        m_emuStatus->setVisible(false);
        m_btnWriteRam->setEnabled(true);
        m_btnWriteLauncher->setEnabled(true);
        return;
    }
    m_emuStatus->setText(msg);
    m_emuStatus->setToolTip(msg);
    m_emuStatus->setStyleSheet(kind == QLatin1String("error") ? QStringLiteral("color: #e51400;")
                                                              : QStringLiteral("color: palette(text);"));
    m_emuStatus->setVisible(true);
    m_btnWriteRam->setEnabled(kind != QLatin1String("busy"));
    m_btnWriteLauncher->setEnabled(kind != QLatin1String("busy"));
}

void RopToolView::writeRam()
{
    QString raw = m_injectAddress->text().trimmed();
    if (raw.isEmpty()) {
        raw = m_data.leftStartAddress;
    }
    raw = raw.toUpper();
    if (raw.startsWith(QLatin1String("0X"))) {
        raw.remove(0, 2);
    }
    if (!isHexAddrInput(raw)) {
        setEmuStatus(t("invalidAddr"), QStringLiteral("error"));
        return;
    }
    const QString hex = m_parseResult.hexChars;
    if (hex.isEmpty()) {
        setEmuStatus(t("noCompileResult"), QStringLiteral("error"));
        return;
    }
    setEmuStatus(t("writingEmu"), QStringLiteral("busy"));
    m_plugin->emu()->writeMemory((int)raw.toLongLong(nullptr, 16), EmuClient::parseHexBytes(hex),
                                 m_plugin->settings()->casioemuMcpPort);
}

void RopToolView::writeLauncher()
{
    const QString hex = m_launcher->text().trimmed();
    if (hex.isEmpty()) {
        setEmuStatus(t("needLauncher"), QStringLiteral("error"));
        return;
    }
    QString raw = m_launcherAddr->text().trimmed().toUpper();
    if (raw.startsWith(QLatin1String("0X"))) {
        raw.remove(0, 2);
    }
    if (raw.isEmpty()) {
        raw = QStringLiteral("D180");
    }
    if (!isHexAddrInput(raw)) {
        setEmuStatus(t("invalidLauncherAddr"), QStringLiteral("error"));
        return;
    }
    setEmuStatus(t("writingEmu"), QStringLiteral("busy"));
    m_plugin->emu()->writeMemory((int)raw.toLongLong(nullptr, 16), EmuClient::parseHexBytes(hex),
                                 m_plugin->settings()->casioemuMcpPort);
}

/* ---------------- 文档绑定与同步 ---------------- */

void RopToolView::setActiveDocument(KTextEditor::Document *doc)
{
    if (doc == m_doc) {
        return;
    }
    // 只绑定 .rop 文档；其它文档保持当前绑定不动
    if (doc && !doc->url().fileName().endsWith(QLatin1String(".rop"), Qt::CaseInsensitive)) {
        return;
    }
    if (m_doc) {
        disconnect(m_docTextChangedConn);
        disconnect(m_docDestroyedConn);
    }
    m_doc = doc;
    if (m_doc) {
        m_docTextChangedConn = connect(m_doc, &KTextEditor::Document::textChanged, this, [this](KTextEditor::Document *) {
            if (m_writing) {
                return;
            }
            reloadFromDocument();
        });
        m_docDestroyedConn = connect(m_doc, &QObject::destroyed, this, [this]() {
            m_doc = nullptr;
        });
        reloadFromDocument();
        loadDisasForDocument();
        syncSettingsUi();
    }
}

void RopToolView::reloadFromDocument()
{
    if (!m_doc) {
        return;
    }
    const QString text = m_doc->text();
    const auto parsed = parseRopDocument(text);
    if (!parsed.ok) {
        m_valid = false;
        m_invalidError = parsed.error;
        m_errorBanner->setText(QStringLiteral("⚠ ") + parsed.error);
        m_errorBanner->setVisible(true);
        m_editor->setReadOnly(true);
        return;
    }
    m_valid = true;
    m_invalidError.clear();
    m_errorBanner->setVisible(false);
    m_editor->setReadOnly(false);
    m_data = parsed.data;
    m_editor->setPlainTextSilently(m_data.input);
    reparse();
    refreshGadgetList();
    syncSettingsUi();
}

void RopToolView::reparse()
{
    m_data.input = m_editor->toPlainText();
    m_parseResult = parseRopInput(m_data.input, m_data.gadgets, m_data.leftStartAddress, m_data.rightStartAddress);
    m_editor->setParseData(&m_parseResult, &m_data.gadgets);
    m_editor->setAddressBases(parseBase(m_data.leftStartAddress), parseBase(m_data.rightStartAddress));
    updateCompileTab();
    if (m_injectAddress->text().isEmpty()) {
        m_injectAddress->setPlaceholderText(m_data.leftStartAddress);
    }
}

void RopToolView::markChanged()
{
    reparse();
    scheduleWriteBack();
}

void RopToolView::scheduleWriteBack()
{
    if (!m_valid) {
        return;
    }
    m_saveTimer->start();
}

void RopToolView::writeBack()
{
    if (!m_doc || !m_valid || m_writing) {
        return;
    }
    const QString json = serializeRopDocument(m_data);
    if (json == m_doc->text()) {
        return;
    }
    m_writing = true;
    m_doc->setText(json);
    m_writing = false;
}

/* ---------------- sidecar ---------------- */

QString RopToolView::sidecarPath() const
{
    if (!m_doc) {
        return QString();
    }
    QString path = m_doc->url().toLocalFile();
    path.remove(QRegularExpression(QStringLiteral("\\.rop$"), QRegularExpression::CaseInsensitiveOption));
    return path + QStringLiteral(".ropide.json");
}

void RopToolView::loadSidecar()
{
    m_sidecarDisasPath.clear();
    QFile f(sidecarPath());
    if (!f.open(QIODevice::ReadOnly)) {
        return;
    }
    const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
    m_sidecarDisasPath = o.value(QLatin1String("disasPath")).toString(QString());
}

void RopToolView::saveSidecar(const QString &disasPath)
{
    QFile f(sidecarPath());
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return;
    }
    QJsonObject o;
    o.insert(QLatin1String("disasPath"), disasPath);
    f.write(QJsonDocument(o).toJson(QJsonDocument::Indented));
    f.close();
}

/* ---------------- 菜单动作 ---------------- */

void RopToolView::activateEditorTab()
{
    m_tabs->setCurrentIndex(0);
}

void RopToolView::activateCompileTab()
{
    if (!m_doc) {
        QMessageBox::information(this, QStringLiteral("RopIDE"), QStringLiteral("请先打开一个 .rop 文件。"));
        return;
    }
    m_tabs->setCurrentIndex(1);
}

void RopToolView::activateGadgetsTab()
{
    if (!m_doc) {
        QMessageBox::information(this, QStringLiteral("RopIDE"), QStringLiteral("请先打开一个 .rop 文件。"));
        return;
    }
    m_gadgetSearch->clear();
    m_tabs->setCurrentIndex(2);
    refreshGadgetList();
}

void RopToolView::activateMarketTab()
{
    m_tabs->setCurrentIndex(3); // 触发 currentChanged → openMarketTab
}

void RopToolView::openMarketTab()
{
    m_marketSearch->clear();
    m_marketLoading = true;
    refreshMarketList();
    // 拉最新列表，成功后标记已读
    auto conn = std::make_shared<QMetaObject::Connection>();
    *conn = connect(m_plugin->market(), &MarketClient::listFinished, this,
                    [this, conn](const MarketListResult &r) {
                        disconnect(*conn);
                        m_marketLoading = false;
                        if (r.ok) {
                            m_marketItems = r.items;
                            m_marketError.clear();
                            m_plugin->settings()->marketLastSeen = QDateTime::currentMSecsSinceEpoch();
                            m_plugin->settings()->save();
                            updateMarketBadge(0);
                        } else {
                            m_marketItems.clear();
                            m_marketError = r.error;
                        }
                        refreshMarketList();
                    });
    m_plugin->market()->fetchList();
}

void RopToolView::showAboutDialog()
{
    QDialog dialog(this);
    dialog.setWindowTitle(t("aboutTitle"));
    auto *layout = new QVBoxLayout(&dialog);
    auto *title = new QLabel(QStringLiteral("<h2>RopIDE for Kate</h2>"), &dialog);
    title->setAlignment(Qt::AlignCenter);
    auto *subtitle = new QLabel(QStringLiteral("打开一个 .rop 文件以继续。"), &dialog);
    subtitle->setAlignment(Qt::AlignCenter);
    layout->addWidget(title);
    layout->addWidget(subtitle);

    auto *buttons = new QHBoxLayout();
    auto *btnNew = new QPushButton(QStringLiteral("新建一个ROP文件"), &dialog);
    auto *btnOpen = new QPushButton(QStringLiteral("打开ROP文件"), &dialog);
    auto *btnMarket = new QPushButton(t("market"), &dialog);
    buttons->addWidget(btnNew);
    buttons->addWidget(btnOpen);
    buttons->addWidget(btnMarket);
    layout->addLayout(buttons);

    connect(btnNew, &QPushButton::clicked, this, [this, &dialog]() {
        dialog.accept();
        newRopFile();
    });
    connect(btnOpen, &QPushButton::clicked, this, [this, &dialog]() {
        dialog.accept();
        openRopFile();
    });
    connect(btnMarket, &QPushButton::clicked, this, [this, &dialog]() {
        dialog.accept();
        activateMarketTab();
    });

    auto *copy1 = new QLabel(QStringLiteral(
        "<a href=\"https://github.com/Yaing-Yan/ropide-kate-plugin\">RopIDE for Kate</a> @Yaing-Yan（移植自 ropide-vscode-plugin）"),
        &dialog);
    auto *copy2 = new QLabel(QStringLiteral(
        "Copyright © 2026 <a href=\"https://github.com/WulanOVO/rop-ide\">RopIDE</a> @wlyibo"),
        &dialog);
    auto *copy3 = new QLabel(QStringLiteral(
        "<a href=\"https://ropide.pages.dev/\">RopIDE网页版</a> · <a href=\"https://rop-ide2.pages.dev/\">ROP IDE 2nd</a>"),
        &dialog);
    for (QLabel *l : {copy1, copy2, copy3}) {
        l->setOpenExternalLinks(true);
        l->setAlignment(Qt::AlignCenter);
        layout->addWidget(l);
    }
    dialog.exec();
}

void RopToolView::newRopFile()
{
    KTextEditor::Application *app = KTextEditor::Editor::instance()->application();
    const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("新建 .rop 文件"),
                                                      QStringLiteral("untitled.rop"),
                                                      QStringLiteral("Rop File (*.rop)"));
    if (path.isEmpty()) {
        return;
    }

    QRegularExpressionValidator hexValidator(QRegularExpression(QStringLiteral("[0-9A-Fa-f]{0,5}")));
    bool ok = false;
    QString left = QInputDialog::getText(this, QStringLiteral("新建 .rop 文件"),
                                         QStringLiteral("左侧起始地址（例如 E9E0）"), QLineEdit::Normal,
                                         QLatin1String(Rop::DEFAULT_LEFT_ADDRESS), &ok);
    if (!ok) {
        return;
    }
    left = left.trimmed().toUpper();
    left.remove(QRegularExpression(QStringLiteral("^0X"), QRegularExpression::CaseInsensitiveOption));
    while (!left.isEmpty() && !isHexAddrInput(left)) {
        QMessageBox::warning(this, QStringLiteral("RopIDE"), QStringLiteral("地址必须是十六进制（最多 5 位），例如 E9E0"));
        left = QInputDialog::getText(this, QStringLiteral("新建 .rop 文件"),
                                     QStringLiteral("左侧起始地址（例如 E9E0）"), QLineEdit::Normal, left, &ok);
        if (!ok) {
            return;
        }
        left = left.trimmed().toUpper();
    }
    if (left.isEmpty()) {
        left = QStringLiteral("0");
    }

    QString right = QInputDialog::getText(this, QStringLiteral("新建 .rop 文件"),
                                          QStringLiteral("右侧起始地址（例如 D710）"), QLineEdit::Normal,
                                          QLatin1String(Rop::DEFAULT_RIGHT_ADDRESS), &ok);
    if (!ok) {
        return;
    }
    right = right.trimmed().toUpper();
    right.remove(QRegularExpression(QStringLiteral("^0X"), QRegularExpression::CaseInsensitiveOption));
    if (right.isEmpty()) {
        right = QStringLiteral("0");
    }

    const QStringList sources = {
        QStringLiteral("CASIO fx-991 CN X VerF（内置预设）"),
        QStringLiteral("CASIO fx-991 CN X VerC（内置预设）"),
        QStringLiteral("导入 gadgets.json…"),
        QStringLiteral("空 gadgets"),
    };
    const QString source = QInputDialog::getItem(this, QStringLiteral("Gadgets"), QStringLiteral("选择 gadgets 来源"),
                                                 sources, 0, false, &ok);
    if (!ok) {
        return;
    }

    QVector<RopGadget> gadgets;
    if (source == sources.at(0)) {
        gadgets = loadPreset(QStringLiteral("verf"));
    } else if (source == sources.at(1)) {
        gadgets = loadPreset(QStringLiteral("verc"));
    } else if (source == sources.at(2)) {
        const QString gpath = QFileDialog::getOpenFileName(this, QStringLiteral("选择 gadgets.json"), QString(),
                                                           QStringLiteral("Gadgets JSON (*.json)"));
        if (gpath.isEmpty()) {
            return;
        }
        QFile gf(gpath);
        if (!gf.open(QIODevice::ReadOnly)) {
            QMessageBox::warning(this, QStringLiteral("RopIDE"), QStringLiteral("读取 gadgets.json 失败：%1").arg(gf.errorString()));
            return;
        }
        const auto r = parseGadgetsJson(QString::fromUtf8(gf.readAll()));
        if (!r.ok) {
            QMessageBox::warning(this, QStringLiteral("RopIDE"), QStringLiteral("导入 gadgets.json 失败：%1").arg(r.error));
            return;
        }
        gadgets = r.gadgets;
    }

    const QString fileName = QFileInfo(path).fileName();
    RopDocumentData data;
    data.input = QStringLiteral("// %1\n").arg(fileName);
    data.gadgets = gadgets;
    data.leftStartAddress = left;
    data.rightStartAddress = right;
    data.ideVersion = IDE_VERSION;

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QMessageBox::warning(this, QStringLiteral("RopIDE"), QStringLiteral("创建 .rop 文件失败：%1").arg(f.errorString()));
        return;
    }
    f.write(serializeRopDocument(data).toUtf8());
    f.close();
    if (app) {
        app->openUrl(QUrl::fromLocalFile(path));
    }
}

void RopToolView::openRopFile()
{
    KTextEditor::Application *app = KTextEditor::Editor::instance()->application();
    const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("打开 .rop 文件"), QString(),
                                                      QStringLiteral("Rop File (*.rop)"));
    if (path.isEmpty()) {
        return;
    }
    if (app) {
        app->openUrl(QUrl::fromLocalFile(path));
    }
}

} // namespace Rop
