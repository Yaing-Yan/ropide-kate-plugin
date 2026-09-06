/*
 * SPDX-FileCopyrightText: 2026 Yaing-Yan
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * RopIDE DSL 编辑器（移植自 ropide-vscode-plugin 的 media/editor.js Webview）：
 *   - 左侧行号栏 → 每行起始左侧地址；右侧地址栏 → 每行起始右侧地址
 *   - 光标处左/右地址（对应原状态栏 L/R）
 *   - 语法高亮（由编译器的解析结果驱动，与网页版同一套 span）
 *   - gadget / 常量补全（# / $ 触发）
 *   - Tab 对齐注释、Ctrl+/ 注释切换
 *   - gadget / 常量 / 锚点 / 数值块悬停提示
 */
#pragma once

#include <QPlainTextEdit>
#include <QString>
#include <QVector>

class QListWidget;
class QSyntaxHighlighter;

#include "compiler.h"

namespace Rop
{

class RopCodeEditor : public QPlainTextEdit
{
    Q_OBJECT

public:
    explicit RopCodeEditor(QWidget *parent = nullptr);
    ~RopCodeEditor() override;

    /** 每次重新解析后由 RopToolView 调用；result 生命周期由调用方保证。 */
    void setParseData(const CompileResult *result, const QVector<RopGadget> *gadgets);
    void setAddressBases(long long leftBase, long long rightBase);

    /** 编译面板点击字节 → 选中源码对应位置（复刻 selectByte）。 */
    void selectByteToken(int byteIndex);

    /** 程序化写回文本（如程序广场下载、新建文件），不触发 textEditedByUser。 */
    void setPlainTextSilently(const QString &text);

Q_SIGNALS:
    /** 光标处的左/右地址（原插件把它发到 VS Code 状态栏）。 */
    void cursorAddressChanged(const QString &left, const QString &right);
    /** 用户主动编辑（区别于程序填充）。 */
    void textEditedByUser();

protected:
    void resizeEvent(QResizeEvent *e) override;
    void keyPressEvent(QKeyEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void leaveEvent(QEvent *e) override;
    bool eventFilter(QObject *obj, QEvent *e) override;

private:
    friend class RopHighlighter;

    // 地址栏
    QWidget *m_leftArea = nullptr;
    QWidget *m_rightArea = nullptr;
    int leftAreaWidth() const;
    int rightAreaWidth() const;
    void updateAreasWidth();
    void positionAreas();
    void paintAddressArea(QPaintEvent *e, QWidget *area, bool right);
    int byteOffsetAt(int charPos) const;
    static QString hexAddr(long long v);

    // 光标 / 行映射
    void updateCursorInfo();
    QVector<int> computeLineOffsets() const;

    // 补全
    void hideAutocomplete();
    void handleAutocomplete();
    void selectAutocomplete(int index);
    void positionAutocomplete();

    // 编辑辅助
    void insertPlainTextAtSelection(const QString &text);
    void alignComment();
    void toggleComment();

    // 悬停
    void handleHover(const QPoint &viewportPos);

    const CompileResult *m_result = nullptr;
    const QVector<RopGadget> *m_gadgets = nullptr;
    QVector<int> m_sortedByteStarts;
    long long m_leftBase = 0;
    long long m_rightBase = 0;

    QSyntaxHighlighter *m_highlighter = nullptr;
    QListWidget *m_acPopup = nullptr;
    int m_acKind = 0; // 0 = gadget, 1 = constant
    bool m_settingText = false;
};

} // namespace Rop
