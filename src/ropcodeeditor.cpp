/*
 * SPDX-FileCopyrightText: 2026 Yaing-Yan
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "ropcodeeditor.h"

#include <QFontDatabase>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QListWidget>
#include <QMouseEvent>
#include <QPainter>
#include <QRegularExpression>
#include <QScrollBar>
#include <QSyntaxHighlighter>
#include <QTextBlock>
#include <QToolTip>

#include <algorithm>

namespace Rop
{

namespace
{

bool isHexDigit(QChar c)
{
    return (c >= QLatin1Char('0') && c <= QLatin1Char('9'))
        || (c >= QLatin1Char('a') && c <= QLatin1Char('f'))
        || (c >= QLatin1Char('A') && c <= QLatin1Char('F'));
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

// 亮/暗两套配色，移植自 media/editor.css 的 --s-* 变量
struct SyntaxColors {
    QColor comment, constName, constEqual, constValue;
    QColor gadget, gadgetClosedBg, value, valueClosedBg;
    QColor anchor, anchorClosedBg, other, warnUnderline;
    bool dark = false;

    static SyntaxColors fromPalette(const QPalette &pal)
    {
        SyntaxColors c;
        c.dark = pal.color(QPalette::Window).lightness() < 128;
        if (c.dark) {
            c.comment = QColor(0x85, 0x85, 0x85);
            c.constName = QColor(0x22, 0xb8, 0xcf);
            c.constEqual = QColor(0x86, 0x8e, 0x96);
            c.constValue = QColor(0x82, 0xc9, 0x1e);
            c.gadget = QColor(0x4d, 0xab, 0xf7);
            c.gadgetClosedBg = QColor(0x10, 0x1f, 0x33);
            c.value = QColor(0xff, 0xa9, 0x4d);
            c.valueClosedBg = QColor(0x21, 0x1a, 0x10);
            c.anchor = QColor(0x63, 0xe6, 0xbe);
            c.anchorClosedBg = QColor(0x10, 0x24, 0x1d);
            c.other = QColor(0x8a, 0x8a, 0x8a);
            c.warnUnderline = QColor(0xff, 0xc0, 0x78);
        } else {
            c.comment = QColor(0x88, 0x88, 0x88);
            c.constName = QColor(0x0b, 0x72, 0x85);
            c.constEqual = QColor(0x34, 0x3a, 0x40);
            c.constValue = QColor(0x5c, 0x94, 0x0d);
            c.gadget = QColor(0x18, 0x64, 0xab);
            c.gadgetClosedBg = QColor(0xe7, 0xf5, 0xff);
            c.value = QColor(0xe6, 0x77, 0x00);
            c.valueClosedBg = QColor(0xff, 0xf9, 0xdb);
            c.anchor = QColor(0x08, 0x7f, 0x5b);
            c.anchorClosedBg = QColor(0xe6, 0xfc, 0xf5);
            c.other = QColor(0x77, 0x77, 0x77);
            c.warnUnderline = QColor(0xd2, 0x92, 0x00);
        }
        return c;
    }
};

} // namespace

/* ---------------- 语法高亮器：编译器 span → QTextCharFormat ---------------- */

class RopHighlighter : public QSyntaxHighlighter
{
public:
    explicit RopHighlighter(RopCodeEditor *editor)
        : QSyntaxHighlighter(editor)
        , m_editor(editor)
    {
    }

    void reloadAll()
    {
        rehighlight();
    }

protected:
    void highlightBlock(const QString &text) override
    {
        Q_UNUSED(text);
        if (!m_editor->m_result) {
            return;
        }
        const int line = currentBlock().blockNumber();
        if (line >= m_editor->m_result->highlightLines.size()) {
            return;
        }
        const SyntaxColors colors = SyntaxColors::fromPalette(m_editor->palette());
        const QColor fg = m_editor->palette().color(QPalette::Text);

        int offset = 0;
        for (const RopSpan &span : m_editor->m_result->highlightLines.at(line)) {
            const int len = span.content.size();
            if (len > 0) {
                QTextCharFormat fmt;
                const QStringList types = span.type.split(QLatin1Char(','), Qt::SkipEmptyParts);
                const bool closed = types.contains(QLatin1String("closed"));
                const bool warning = types.contains(QLatin1String("warning"));

                if (types.contains(QLatin1String("comment"))) {
                    fmt.setForeground(colors.comment);
                    fmt.setFontItalic(true);
                } else if (types.contains(QLatin1String("constant"))) {
                    if (types.contains(QLatin1String("name"))) {
                        fmt.setForeground(colors.constName);
                    } else if (types.contains(QLatin1String("equal"))) {
                        fmt.setForeground(colors.constEqual);
                    } else if (types.contains(QLatin1String("value"))) {
                        fmt.setForeground(colors.constValue);
                    }
                } else if (types.contains(QLatin1String("gadget"))) {
                    fmt.setForeground(colors.gadget);
                    if (closed) {
                        fmt.setBackground(colors.gadgetClosedBg);
                    }
                } else if (types.contains(QLatin1String("value"))) {
                    fmt.setForeground(colors.value);
                    if (closed) {
                        fmt.setBackground(colors.valueClosedBg);
                    }
                } else if (types.contains(QLatin1String("anchor"))) {
                    fmt.setForeground(colors.anchor);
                    if (closed) {
                        fmt.setBackground(colors.anchorClosedBg);
                    }
                } else if (types.contains(QLatin1String("hex"))) {
                    fmt.setForeground(fg);
                    fmt.setFontWeight(QFont::DemiBold);
                } else { // other
                    fmt.setForeground(colors.other);
                }
                if (warning) {
                    fmt.setUnderlineStyle(QTextCharFormat::WaveUnderline);
                    fmt.setUnderlineColor(colors.warnUnderline);
                }
                setFormat(offset, len, fmt);
            }
            offset += len;
        }
    }

private:
    RopCodeEditor *m_editor;
};

/* ---------------- 编辑器主体 ---------------- */

RopCodeEditor::RopCodeEditor(QWidget *parent)
    : QPlainTextEdit(parent)
{
    setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    setLineWrapMode(QPlainTextEdit::NoWrap);
    setTabChangesFocus(false);

    m_leftArea = new QWidget(this);
    m_rightArea = new QWidget(this);
    m_leftArea->installEventFilter(this);
    m_rightArea->installEventFilter(this);

    m_highlighter = new RopHighlighter(this);

    connect(this, &QPlainTextEdit::textChanged, this, [this]() {
        updateAreasWidth();
        if (!m_settingText) {
            Q_EMIT textEditedByUser();
        }
        updateCursorInfo();
    });
    connect(this, &QPlainTextEdit::cursorPositionChanged, this, [this]() {
        updateCursorInfo();
        // 光标移动时把地址栏当前行刷新一下
        m_leftArea->update();
        m_rightArea->update();
    });
    connect(this, &QPlainTextEdit::updateRequest, this, [this](const QRect &, int dy) {
        if (dy) {
            m_leftArea->scroll(0, dy);
            m_rightArea->scroll(0, dy);
        } else {
            m_leftArea->update();
            m_rightArea->update();
        }
    });

    viewport()->setMouseTracking(true);

    // 补全浮层（对应原插件的 autocomplete 面板）
    m_acPopup = new QListWidget(this);
    m_acPopup->setWindowFlags(Qt::Popup);
    m_acPopup->setAttribute(Qt::WA_ShowWithoutActivating);
    m_acPopup->setUniformItemSizes(true);
    m_acPopup->setVisible(false);
    m_acPopup->setFont(font());
    connect(m_acPopup, &QListWidget::itemClicked, this, [this](QListWidgetItem *it) {
        selectAutocomplete(m_acPopup->row(it));
        setFocus();
    });

    updateAreasWidth();
}

RopCodeEditor::~RopCodeEditor() = default;

void RopCodeEditor::setParseData(const CompileResult *result, const QVector<RopGadget> *gadgets)
{
    m_result = result;
    m_gadgets = gadgets;
    m_sortedByteStarts.clear();
    if (result) {
        m_sortedByteStarts = result->byteStartPositions;
        std::sort(m_sortedByteStarts.begin(), m_sortedByteStarts.end());
    }
    static_cast<RopHighlighter *>(m_highlighter)->reloadAll();
    m_leftArea->update();
    m_rightArea->update();
    updateCursorInfo();
}

void RopCodeEditor::setAddressBases(long long leftBase, long long rightBase)
{
    m_leftBase = leftBase;
    m_rightBase = rightBase;
    m_leftArea->update();
    m_rightArea->update();
    updateCursorInfo();
}

void RopCodeEditor::setPlainTextSilently(const QString &text)
{
    m_settingText = true;
    if (toPlainText() != text) {
        setPlainText(text);
    }
    m_settingText = false;
}

QString RopCodeEditor::hexAddr(long long v)
{
    if (v < 0) {
        v = 0;
    }
    return QString::number((ulong)v, 16).toUpper().rightJustified(4, QLatin1Char('0'));
}

int RopCodeEditor::byteOffsetAt(int charPos) const
{
    // countLessThan：统计 < charPos 的字节起点个数
    return (int)(std::lower_bound(m_sortedByteStarts.cbegin(), m_sortedByteStarts.cend(), charPos)
                 - m_sortedByteStarts.cbegin());
}

QVector<int> RopCodeEditor::computeLineOffsets() const
{
    QVector<int> offsets;
    QTextBlock block = document()->firstBlock();
    while (block.isValid()) {
        offsets.push_back(byteOffsetAt(block.position()));
        block = block.next();
    }
    return offsets;
}

int RopCodeEditor::leftAreaWidth() const
{
    const int digits = 5; // 地址最多 5 位（1~5 位 hex，0xFFFFF 上限）
    const int advance = fontMetrics().horizontalAdvance(QLatin1Char('0'));
    return 6 + digits * advance + 6;
}

int RopCodeEditor::rightAreaWidth() const
{
    return leftAreaWidth();
}

void RopCodeEditor::updateAreasWidth()
{
    setViewportMargins(leftAreaWidth(), 0, rightAreaWidth(), 0);
    positionAreas();
}

void RopCodeEditor::positionAreas()
{
    const QRect cr = contentsRect();
    m_leftArea->setGeometry(cr.left(), cr.top(), leftAreaWidth(), cr.height());
    m_rightArea->setGeometry(cr.left() + cr.width() - rightAreaWidth(), cr.top(),
                             rightAreaWidth(), cr.height());
}

void RopCodeEditor::paintAddressArea(QPaintEvent *e, QWidget *area, bool right)
{
    Q_UNUSED(e);
    QPainter painter(area);
    painter.fillRect(area->rect(), palette().window());
    painter.setPen(palette().color(QPalette::PlaceholderText));

    const QFontMetrics fm = fontMetrics();
    const int curLine = textCursor().blockNumber();
    const QVector<int> offsets = computeLineOffsets();

    QTextBlock block = firstVisibleBlock();
    int top = (int)blockBoundingGeometry(block).translated(contentOffset()).top();
    int bottom = top + (int)blockBoundingRect(block).height();
    while (block.isValid() && top <= e->rect().bottom()) {
        if (block.isVisible() && bottom >= e->rect().top()) {
            const int line = block.blockNumber();
            const int off = line < offsets.size() ? offsets.at(line) : 0;
            const long long addr = (right ? m_rightBase : m_leftBase) + off;
            const bool current = line == curLine;
            if (current) {
                painter.fillRect(0, top, area->width(), fm.height(),
                                 palette().color(QPalette::Window).lightness() < 128
                                     ? QColor(255, 255, 255, 26)
                                     : QColor(0, 0, 0, 22));
                painter.setPen(palette().color(QPalette::Text));
            }
            painter.drawText(0, top, area->width() - 4, fm.height(), Qt::AlignRight, hexAddr(addr));
            if (current) {
                painter.setPen(palette().color(QPalette::PlaceholderText));
            }
        }
        block = block.next();
        top = bottom;
        bottom = top + (int)blockBoundingRect(block).height();
    }
}

void RopCodeEditor::updateCursorInfo()
{
    const int pos = textCursor().position();
    const int off = byteOffsetAt(pos);
    Q_EMIT cursorAddressChanged(hexAddr(m_leftBase + off), hexAddr(m_rightBase + off));
}

void RopCodeEditor::selectByteToken(int byteIndex)
{
    if (!m_result || byteIndex < 0 || document()->isEmpty()) {
        return;
    }
    const QVector<int> &map = m_result->charPosInInputMap;
    const int sIdx = byteIndex * 2;
    if (sIdx + 1 >= map.size()) {
        return;
    }
    const int start = map.at(sIdx);
    const int end = map.at(sIdx + 1);
    const int maxPos = document()->characterCount() - 1;
    QTextCursor c = textCursor();
    c.setPosition(qBound(0, start, maxPos));
    c.setPosition(qBound(0, end + 1, maxPos), QTextCursor::KeepAnchor);
    setTextCursor(c);
    ensureCursorVisible();
    setFocus();
}

/* ---------------- 事件处理：Tab 注释 / 补全键 / 查找 ---------------- */

void RopCodeEditor::resizeEvent(QResizeEvent *e)
{
    QPlainTextEdit::resizeEvent(e);
    updateAreasWidth();
}

void RopCodeEditor::keyPressEvent(QKeyEvent *e)
{
    const bool popupVisible = m_acPopup->isVisible() && !m_acPopup->selectedItems().isEmpty()
        || (m_acPopup->isVisible() && m_acPopup->count() > 0);
    Q_UNUSED(popupVisible);
    if (m_acPopup->isVisible()) {
        if (e->key() == Qt::Key_Down) {
            const int next = qMin(m_acPopup->currentRow() + 1, m_acPopup->count() - 1);
            m_acPopup->setCurrentRow(next);
            return;
        }
        if (e->key() == Qt::Key_Up) {
            const int next = qMax(m_acPopup->currentRow() - 1, 0);
            m_acPopup->setCurrentRow(next);
            return;
        }
        if (e->key() == Qt::Key_Enter || e->key() == Qt::Key_Return || e->key() == Qt::Key_Tab) {
            selectAutocomplete(m_acPopup->currentRow());
            return;
        }
        if (e->key() == Qt::Key_Escape) {
            hideAutocomplete();
            return;
        }
    }

    if (e->key() == Qt::Key_Tab && !e->modifiers()) {
        alignComment();
        return;
    }
    if (e->key() == Qt::Key_Slash && (e->modifiers() & Qt::ControlModifier)) {
        toggleComment();
        return;
    }
    QPlainTextEdit::keyPressEvent(e);
    if (!isReadOnly()) {
        handleAutocomplete();
    }
}

bool RopCodeEditor::eventFilter(QObject *obj, QEvent *e)
{
    if (e->type() == QEvent::Paint && (obj == m_leftArea || obj == m_rightArea)) {
        paintAddressArea(static_cast<QPaintEvent *>(e), static_cast<QWidget *>(obj), obj == m_rightArea);
        return true;
    }
    return QPlainTextEdit::eventFilter(obj, e);
}

void RopCodeEditor::mouseMoveEvent(QMouseEvent *e)
{
    QPlainTextEdit::mouseMoveEvent(e);
    handleHover(e->pos());
}

void RopCodeEditor::leaveEvent(QEvent *e)
{
    QPlainTextEdit::leaveEvent(e);
    viewport()->unsetCursor();
}

/* ---------------- Tab 对齐注释 ---------------- */

void RopCodeEditor::insertPlainTextAtSelection(const QString &text)
{
    QTextCursor c = textCursor();
    c.insertText(text);
    setTextCursor(c);
}

void RopCodeEditor::alignComment()
{
    const QString text = toPlainText();
    const int cursorPos = textCursor().position();
    const int lineStart = cursorPos > 0 ? text.lastIndexOf(QLatin1Char('\n'), cursorPos - 1) + 1 : 0;
    const int currentCol = cursorPos - lineStart;

    // 向上找（最多 50 行）第一个 `//` 列号大于当前列的行，对齐到该列
    const QStringList lines = text.left(lineStart).split(QLatin1Char('\n'));
    int targetCol = -1;
    int tried = 0;
    for (int i = lines.size() - 2; i >= 0 && tried < 50; i--, tried++) {
        const int col = lines.at(i).indexOf(QLatin1String("//"));
        if (col >= 0 && col > currentCol) {
            targetCol = col;
            break;
        }
    }
    const QString spaces = targetCol >= 0 ? QString(targetCol - currentCol, QLatin1Char(' '))
                                          : QStringLiteral("  ");
    insertPlainTextAtSelection(spaces);
}

/* ---------------- Ctrl+/ 注释切换 ---------------- */

void RopCodeEditor::toggleComment()
{
    QTextCursor c = textCursor();
    const int start = c.selectionStart();
    const int end = c.selectionEnd();
    const QString text = toPlainText();

    const int firstLine = start == 0 ? 0 : text.mid(0, start).count(QLatin1Char('\n'));
    const int lastLine = end == 0 ? 0 : text.mid(0, end).count(QLatin1Char('\n'));

    QStringList lines = text.split(QLatin1Char('\n'));
    if (firstLine >= lines.size() || lastLine >= lines.size()) {
        return;
    }

    bool allCommented = true;
    bool hasNonEmpty = false;
    for (int i = firstLine; i <= lastLine; i++) {
        const QString trimmed = lines.at(i).trimmed();
        if (trimmed.size() > 0) {
            hasNonEmpty = true;
            if (!trimmed.startsWith(QLatin1String("//"))) {
                allCommented = false;
                break;
            }
        }
    }
    if (!hasNonEmpty) {
        return;
    }

    QStringList newLines = lines;
    int firstLineDelta = 0;
    int totalDelta = 0;
    for (int i = firstLine; i <= lastLine; i++) {
        if (lines.at(i).trimmed().size() == 0) {
            continue;
        }
        if (allCommented) {
            const int idx = newLines[i].indexOf(QLatin1String("//"));
            if (idx >= 0) {
                const int removeCount = newLines[i].mid(idx + 2, 1) == QLatin1String(" ") ? 3 : 2;
                newLines[i].remove(idx, removeCount);
                if (i == firstLine) {
                    firstLineDelta = -removeCount;
                }
                totalDelta += -removeCount;
            }
        } else {
            newLines[i] = QStringLiteral("// ") + newLines.at(i);
            if (i == firstLine) {
                firstLineDelta = 3;
            }
            totalDelta += 3;
        }
    }

    const QString newText = newLines.join(QLatin1Char('\n'));
    c.beginEditBlock();
    c.select(QTextCursor::Document);
    c.insertText(newText);
    c.endEditBlock();
    setTextCursor(c);

    // 保持选区不折叠（移植自 editor.js）
    const int newStart = qMax(0, start + firstLineDelta);
    const int newEnd = qMax(newStart, end + totalDelta);
    QTextCursor sel = textCursor();
    sel.setPosition(qMin(newStart, document()->characterCount() - 1));
    sel.setPosition(qMin(newEnd, document()->characterCount() - 1), QTextCursor::KeepAnchor);
    setTextCursor(sel);
}

/* ---------------- 补全 ---------------- */

void RopCodeEditor::hideAutocomplete()
{
    m_acPopup->setVisible(false);
    m_acPopup->clear();
}

void RopCodeEditor::handleAutocomplete()
{
    if (!m_result || isReadOnly()) {
        hideAutocomplete();
        return;
    }
    const int pos = textCursor().position();
    const QString before = toPlainText().left(pos);
    const int lineStartIdx = before.lastIndexOf(QLatin1Char('\n')) + 1;
    const QString lineBefore = before.mid(lineStartIdx);
    if (lineBefore.contains(QLatin1String("//"))) {
        hideAutocomplete();
        return;
    }

    static const QRegularExpression constRe(QStringLiteral("\\$([A-Za-z0-9_-]*)$"));
    static const QRegularExpression gadgetRe(QStringLiteral("#([A-Za-z0-9-]*)$"));

    auto cm = constRe.match(before);
    if (cm.hasMatch()) {
        const QString query = cm.captured(1).toLower();
        struct ConstItem { QString name; int value; };
        QVector<ConstItem> list;
        for (const QString &n : m_result->constantOrder) {
            if (m_result->constants.contains(n) && n.toLower().contains(query)) {
                list.push_back(ConstItem{n, m_result->constants.value(n)});
            }
        }
        std::sort(list.begin(), list.end(), [](const ConstItem &a, const ConstItem &b) {
            return a.name < b.name;
        });
        if (list.isEmpty()) {
            hideAutocomplete();
            return;
        }
        m_acKind = 1;
        m_acPopup->clear();
        for (int i = 0; i < list.size() && i < 50; i++) {
            QListWidgetItem *item =
                new QListWidgetItem(QStringLiteral("%1    0x%2")
                                        .arg(list.at(i).name, hexAddr(list.at(i).value)),
                                    m_acPopup);
            item->setData(Qt::UserRole, list.at(i).name);
        }
        m_acPopup->setCurrentRow(0);
        positionAutocomplete();
        return;
    }

    auto gm = gadgetRe.match(before);
    if (!gm.hasMatch() || m_gadgets->isEmpty()) {
        hideAutocomplete();
        return;
    }
    const QString query = gm.captured(1);
    const bool allow00 = !query.startsWith(QLatin1Char('-'));
    struct GadgetItem { QString name; QString addr; QString desc; };
    QVector<GadgetItem> list;
    for (const RopGadget &g : *m_gadgets) {
        const QString name = allow00 ? g.name : QStringLiteral("-") + g.name;
        if (name.toLower().contains(query.toLower())) {
            list.push_back(GadgetItem{name, g.addr, g.desc});
        }
        if (list.size() >= 50) {
            break;
        }
    }
    if (list.isEmpty()) {
        hideAutocomplete();
        return;
    }
    m_acKind = 0;
    m_acPopup->clear();
    for (const GadgetItem &g : list) {
        QListWidgetItem *item =
            new QListWidgetItem(QStringLiteral("%1    %2    %3")
                                    .arg(g.name, g.addr, g.desc.split(QLatin1Char('\n')).value(0)),
                                m_acPopup);
        item->setData(Qt::UserRole, g.name);
    }
    m_acPopup->setCurrentRow(0);
    positionAutocomplete();
}

void RopCodeEditor::positionAutocomplete()
{
    const QRect cr = cursorRect();
    QPoint pos = viewport()->mapTo(this, cr.bottomLeft());
    pos.setY(pos.y() + 2);
    pos.setX(qBound(0, pos.x(), qMax(0, width() - m_acPopup->sizeHint().width() - 10)));
    m_acPopup->adjustSize();
    m_acPopup->move(mapToGlobal(pos));
    m_acPopup->setVisible(true);
}

void RopCodeEditor::selectAutocomplete(int index)
{
    QListWidgetItem *item = m_acPopup->item(index);
    if (!item) {
        hideAutocomplete();
        return;
    }
    const QString name = item->data(Qt::UserRole).toString();
    const int pos = textCursor().position();
    const QString before = toPlainText().left(pos);
    QString matched;
    if (m_acKind == 1) {
        auto m = QRegularExpression(QStringLiteral("\\$[A-Za-z0-9_-]*$")).match(before);
        if (m.hasMatch()) {
            matched = m.captured(0);
        }
    } else {
        auto m = QRegularExpression(QStringLiteral("#[A-Za-z0-9-]*$")).match(before);
        if (m.hasMatch()) {
            matched = m.captured(0);
        }
    }
    if (!matched.isNull()) {
        QTextCursor c = textCursor();
        c.setPosition(pos - matched.size());
        c.setPosition(pos, QTextCursor::KeepAnchor);
        c.insertText(m_acKind == 1 ? QLatin1Char('$') + name : QLatin1Char('#') + name + QLatin1Char(';'));
        setTextCursor(c);
    }
    hideAutocomplete();
}

/* ---------------- 悬停提示 ---------------- */

void RopCodeEditor::handleHover(const QPoint &viewportPos)
{
    if (!m_result) {
        return;
    }
    const QTextCursor cursor = cursorForPosition(viewportPos);
    const int offset = cursor.position();
    const QString text = toPlainText();
    if (offset < 0 || offset >= text.size()) {
        QToolTip::hideText();
        return;
    }
    const int lineStartIdx = offset > 0 ? text.lastIndexOf(QLatin1Char('\n'), offset - 1) + 1 : 0;
    int nl = text.indexOf(QLatin1Char('\n'), offset);
    const int lineEnd = nl == -1 ? text.size() : nl;
    const QString line = text.mid(lineStartIdx, lineEnd - lineStartIdx);
    const int col = offset - lineStartIdx;

    struct Token {
        QString kind;
        QString name;
        int start = 0;
        int end = 0;
        QString expr;
    };
    Token token;
    bool found = false;

    struct Pat {
        const char *kind;
        const char *re;
        bool strip;
    };
    const Pat pats[] = {
        {"gadget", "#[^;\\s]*", true},
        {"anchor", "<-?[^>\\s]*", true},
        {"const", "\\$[A-Za-z0-9_-]*", true},
    };
    for (const Pat &p : pats) {
        QRegularExpression re(QString::fromLatin1(p.re));
        auto it = re.globalMatch(line);
        while (it.hasNext()) {
            auto m = it.next();
            if (col >= m.capturedStart() && col < m.capturedStart() + m.capturedLength()) {
                token.kind = QString::fromLatin1(p.kind);
                token.start = lineStartIdx + m.capturedStart();
                token.end = lineStartIdx + m.capturedStart() + m.capturedLength();
                QString cap = m.captured(0);
                if (p.kind == QLatin1String("gadget")) {
                    cap.remove(QRegularExpression(QStringLiteral("^#-?")));
                } else if (p.kind == QLatin1String("anchor")) {
                    cap.remove(QRegularExpression(QStringLiteral("^<-?")));
                } else {
                    cap = cap.mid(1);
                }
                token.name = cap;
                found = true;
                break;
            }
        }
        if (found) {
            break;
        }
    }
    if (!found) {
        QRegularExpression vb(QStringLiteral("\\[[^\\]]*\\]?"));
        auto it = vb.globalMatch(line);
        while (it.hasNext()) {
            auto m = it.next();
            if (col >= m.capturedStart() && col < m.capturedStart() + m.capturedLength()) {
                token.kind = QStringLiteral("value");
                token.expr = m.captured(0).mid(1);
                if (token.expr.endsWith(QLatin1Char(']'))) {
                    token.expr.chop(1);
                }
                token.start = lineStartIdx + m.capturedStart();
                token.end = lineStartIdx + m.capturedStart() + m.capturedLength();
                found = true;
                break;
            }
        }
    }
    if (!found) {
        QToolTip::hideText();
        return;
    }

    // 构造提示 HTML（对应 renderHoverHtml）
    QString html;
    if (token.kind == QLatin1String("gadget")) {
        const RopGadget *g = nullptr;
        for (const RopGadget &x : *m_gadgets) {
            if (x.name == token.name) {
                g = &x;
                break;
            }
        }
        if (!g) {
            QToolTip::hideText();
            return;
        }
        html = QStringLiteral("<b>#%1;</b><br/><span>%2</span>").arg(escapeHtml(g->name), escapeHtml(g->addr));
        if (!g->desc.isEmpty()) {
            html += QStringLiteral("<br/>%1").arg(escapeHtml(g->desc));
        }
        for (const RopTag &t : g->tags) {
            html += QStringLiteral(" <i>[%1]</i>").arg(escapeHtml(t.name));
        }
    } else if (token.kind == QLatin1String("const") || token.kind == QLatin1String("anchor")) {
        const int v = m_result->constants.value(token.name, 0);
        const QString side = m_result->anchorSides.value(token.name);
        if (token.kind == QLatin1String("const")) {
            html = QStringLiteral("<b>%1 $%2</b><br/>0x%3")
                       .arg(side.isEmpty() ? QStringLiteral("Constant") : QStringLiteral("Anchor"),
                            escapeHtml(token.name), hexAddr(v));
        } else {
            html = QStringLiteral("<b>Anchor &lt;%1&gt;</b><br/>0x%2 · %3")
                       .arg(escapeHtml(token.name), hexAddr(v), side == QLatin1String("left")
                                                                ? QStringLiteral("left")
                                                                : QStringLiteral("right"));
        }
    } else { // value
        // 与 editor.js 的 evalExpr 一致
        long long value = 0;
        QString symbol = QStringLiteral("+");
        bool ok = true;
        const QStringList parts = token.expr.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        for (const QString &part : parts) {
            if (part.startsWith(QLatin1Char('$'))) {
                const auto itv = m_result->constants.constFind(part.mid(1));
                if (itv == m_result->constants.constEnd()) {
                    ok = false;
                    break;
                }
                value += symbol == QLatin1String("-") ? -itv.value() : itv.value();
                symbol = QString();
            } else if (part == QLatin1String("+") || part == QLatin1String("-")) {
                symbol = part;
            } else if (QRegularExpression(QStringLiteral("^-?[0-9a-fA-F]+$")).match(part).hasMatch()) {
                const long long v = part.toLongLong(nullptr, 16);
                value += symbol == QLatin1String("-") ? -v : v;
                symbol = QString();
            } else {
                ok = false;
                break;
            }
        }
        if (!symbol.isEmpty()) {
            ok = false;
        }
        if (ok && value < 0) {
            value = 0xffff + value + 1;
        }
        value &= 0xffff;
        if (!ok) {
            html = QStringLiteral("<b>Value block</b><br/>%1<br/>%2")
                       .arg(escapeHtml(token.expr.trimmed()), QStringLiteral("—"));
        } else {
            html = QStringLiteral("<b>Value block</b><br/>%1<br/>0x%3 · LE: %2")
                       .arg(escapeHtml(token.expr.trimmed()),
                            QStringLiteral("%1 %2").arg(hexAddr(value).right(2), hexAddr(value).left(2)),
                            hexAddr(value));
        }
    }

    QToolTip::showText(viewport()->mapToGlobal(viewportPos) + QPoint(14, 14), html, this);
}

} // namespace Rop
