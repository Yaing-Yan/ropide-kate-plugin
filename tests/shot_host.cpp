/*
 * SPDX-FileCopyrightText: 2026 Yaing-Yan
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * 截图宿主：在无头环境（QT_QPA_PLATFORM=offscreen）里把 RopToolView 的各个标签页
 * 逐个渲染成 PNG，用于 README 配图与人工检查。与 tests/test_host.cpp 的区别是：
 * 这里不做断言，只负责“把界面画出来”，并且会额外渲染查找条、补全浮层等交互态。
 *
 * 用法：QT_QPA_PLATFORM=offscreen ./ropide-shot-host <plugin.so> [outdir]
 */
#include <QApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QTabWidget>
#include <QTextCursor>
#include <QTimer>
#include <QUrl>

#include <KPluginFactory>
#include <KPluginMetaData>
#include <KTextEditor/Document>
#include <KTextEditor/Editor>

#include <iostream>

#include "compiler.h"
#include "rop.h"
#include "ropcodeeditor.h"
#include "ropideplugin.h"
#include "roptoolview.h"
#include "settings.h"

namespace
{

int g_failures = 0;

/** 跑一段事件循环（网络请求、延迟写回、重绘都需要）。 */
void pump(int ms)
{
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
}

void writeTextFile(const QString &path, const QString &text)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        std::cerr << "WARN: cannot write " << path.toStdString() << std::endl;
        return;
    }
    f.write(text.toUtf8());
}

void saveShot(QWidget *w, const QString &path, const QString &what)
{
    const QPixmap pm = w->grab();
    if (pm.isNull() || !pm.save(path)) {
        std::cerr << "FAIL: shot not saved: " << path.toStdString() << std::endl;
        ++g_failures;
        return;
    }
    std::cout << "OK: " << what.toStdString() << " -> " << path.toStdString()
              << " (" << pm.width() << 'x' << pm.height() << ')' << std::endl;
}

/** 演示用 DSL 源码（覆盖注释 / 常量 / 锚点 / gadget / 数值块 / 裸十六进制）。 */
QString demoInput()
{
    return QStringLiteral(
        "// RopIDE for Kate 演示程序 —— CASIO fx-991 CN X\n"
        "$entry = E9E0;                    // 程序入口（左侧起始地址）\n"
        "$backup = D710;                   // 备份区\n"
        "\n"
        "<-launcher>                       // launcher：从 D180 跳进来\n"
        "#pop-er0;                         // 赋值 ER0\n"
        "[$entry]\n"
        "#-mov-sp-er0;                     // 不允许出现 00 字节的 gadget\n"
        "<loop>\n"
        "#add-er0-1;\n"
        "[$loop - 4]\n"
        "12 34 ab cd\n"
        "#exit;                            // POP PC 收尾\n");
}

/** 演示用 gadgets（名字/地址/描述/标签都进 .rop 的 gadgets 字段）。 */
QString demoRopJson()
{
    struct G {
        const char *name;
        const char *addr;
        const char *desc;
        const char *tag;
    };
    const QVector<G> gs = {
        { "pop-er0", "121A8", "POP {ER0}\n赋值 ER0", "ER0" },
        { "mov-sp-er0", "12144", "MOV ER0, SP\n把 ER0 写入 SP", "SP" },
        { "add-er0-1", "123F0", "ADD #1, ER0\nER0 + 1", "ER0" },
        { "exit", "11C3C", "POP PC\n结束并返回", "EXIT" },
        { "pop-r0-r15", "12D34", "POP {R0-R15, PC}\n恢复全部寄存器", "STACK" },
        { "str-er0", "12B2C", "MOV.L ER0, @ER1\n写入内存", "MEM" },
    };

    QJsonArray arr;
    for (const G &g : gs) {
        QJsonObject tag;
        tag.insert(QStringLiteral("name"), QString::fromUtf8(g.tag));
        tag.insert(QStringLiteral("color"), QStringLiteral("#3794ff"));
        QJsonArray tags;
        tags.append(tag);

        QJsonObject o;
        o.insert(QStringLiteral("name"), QString::fromUtf8(g.name));
        o.insert(QStringLiteral("addr"), QString::fromUtf8(g.addr));
        o.insert(QStringLiteral("desc"), QString::fromUtf8(g.desc));
        o.insert(QStringLiteral("tags"), tags);
        arr.append(o);
    }

    QJsonObject root;
    root.insert(QStringLiteral("input"), demoInput());
    root.insert(QStringLiteral("gadgets"), arr);
    root.insert(QStringLiteral("leftStartAddress"), QStringLiteral("E9E0"));
    root.insert(QStringLiteral("rightStartAddress"), QStringLiteral("D710"));
    root.insert(QStringLiteral("ideVersion"), 100);
    return QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact));
}

/**
 * 演示用 _disas 文本（截图专用，非真实固件反汇编）：
 * parseDisas 只认 “4~6 位十六进制地址 + 两个以上空格” 开头的行。
 */
QString demoDisas()
{
    return QStringLiteral(
        "# demo _disas for RopIDE screenshots（演示数据）\n"
        "011C3C    POP     PC\n"
        "012144    MOV     ER0, SP\n"
        "012146    RT\n"
        "0121A8    POP     {ER0}\n"
        "0121AA    POP     PC\n"
        "0123F0    ADD     #1, ER0\n"
        "0123F2    POP     PC\n"
        "012B2C    MOV.L   ER0, @ER1\n"
        "012B2E    RT\n"
        "012D34    POP     {R0-R15, PC}\n"
        "0D7100    MOV     #0, ER0\n"
        "0D7102    MOV.L   ER0, @(4,ER1)\n"
        "0D7104    POP     PC\n");
}

QPushButton *findButtonByText(QWidget *root, const QString &text)
{
    const auto buttons = root->findChildren<QPushButton *>();
    for (QPushButton *b : buttons) {
        if (b->text() == text) {
            return b;
        }
    }
    return nullptr;
}

QLineEdit *findLineEditByPlaceholder(QWidget *root, const QString &ph)
{
    const auto edits = root->findChildren<QLineEdit *>();
    for (QLineEdit *e : edits) {
        if (e->placeholderText() == ph) {
            return e;
        }
    }
    return nullptr;
}

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    // 看门狗：无论如何 2 分钟后退出
    QTimer::singleShot(120000, &app, [&app]() {
        std::cerr << "TIMEOUT" << std::endl;
        app.exit(3);
    });

    const QString pluginPath =
        argc > 1 ? QString::fromLocal8Bit(argv[1]) : QStringLiteral("ropidekate.so");
    const QString outArg = argc > 2 ? QString::fromLocal8Bit(argv[2]) : QStringLiteral("shots");
    QDir().mkpath(outArg);
    const QString outDir = QDir(outArg).absolutePath();

    // 0) 插件元数据 + 工厂（与 test_host 相同的最低校验）
    const KPluginMetaData md(pluginPath);
    if (!md.isValid()) {
        std::cerr << "FAIL: invalid plugin metadata: " << pluginPath.toStdString() << std::endl;
        return 1;
    }
    const KPluginFactory::Result res = KPluginFactory::loadFactory(md);
    if (!res.plugin) {
        std::cerr << "FAIL: cannot load plugin factory: " << res.errorString.toStdString() << std::endl;
        return 1;
    }
    std::cout << "plugin id=" << md.pluginId().toStdString()
              << " version=" << md.version().toStdString() << std::endl;

    // 1) 演示文件：demo.rop + sidecar（记住 _disas 路径）+ demo_disas.txt
    const QString ropPath = outDir + QStringLiteral("/demo.rop");
    writeTextFile(ropPath, demoRopJson());
    const QString disasPath = outDir + QStringLiteral("/demo_disas.txt");
    writeTextFile(disasPath, demoDisas());
    QJsonObject sidecar;
    sidecar.insert(QStringLiteral("disasPath"), disasPath);
    writeTextFile(outDir + QStringLiteral("/demo.ropide.json"),
                  QString::fromUtf8(QJsonDocument(sidecar).toJson(QJsonDocument::Compact)));

    // 2) 插件 + 真实 .rop 文档 + 工具视图
    auto *plugin = new Rop::RopIDEPlugin(nullptr);
    KTextEditor::Editor *keditor = KTextEditor::Editor::instance();
    if (!keditor) {
        std::cerr << "FAIL: no KTextEditor::Editor (katepart)" << std::endl;
        return 1;
    }
    KTextEditor::Document *doc = keditor->createDocument(nullptr);
    doc->openUrl(QUrl::fromLocalFile(ropPath));

    auto *view = new Rop::RopToolView(plugin, nullptr);
    view->resize(1280, 860);
    view->show();
    view->setActiveDocument(doc);
    pump(1000);

    auto *tabs = view->findChild<QTabWidget *>();
    auto *code = view->findChild<Rop::RopCodeEditor *>();
    if (!tabs || !code) {
        std::cerr << "FAIL: tool view internals missing" << std::endl;
        return 1;
    }

    // 3) Editor：默认态
    view->activateEditorTab();
    pump(400);
    saveShot(view, outDir + QStringLiteral("/01-editor.png"), QStringLiteral("Editor 标签页"));

    // 4) Editor：查找条。Qt6 的 QShortcut 没有 activate()，离屏环境也不保证有活动
    //    窗口，因此按控件树找到查找条本体（findInput → row → bar）后直接显示。
    QWidget *findBar = nullptr;
    if (QLineEdit *findInput = findLineEditByPlaceholder(view, QStringLiteral("查找…"))) {
        if (findInput->parentWidget()) {
            findBar = findInput->parentWidget()->parentWidget();
        }
        if (findBar) {
            findBar->setVisible(true);
        }
        findInput->setText(QStringLiteral("loop"));
    }
    pump(400);
    saveShot(view, outDir + QStringLiteral("/02-editor-find.png"), QStringLiteral("Editor + 查找条"));
    if (QPushButton *closeBtn = findButtonByText(view, QStringLiteral("×"))) {
        closeBtn->click(); // 走真实的关闭逻辑
        pump(200);
    }
    if (findBar) {
        findBar->setVisible(false);
    }

    // 5) Editor：gadget 补全浮层（光标停在 "#pop-er0;" 的 "#pop" 之后）
    const int gadgetPosition = code->toPlainText().indexOf(QStringLiteral("#pop-er0;"));
    if (gadgetPosition >= 0) {
        QTextCursor c(code->document());
        c.setPosition(gadgetPosition + 4); // "#pop" 之后
        code->setTextCursor(c);
        code->centerCursor();
        code->setFocus();
        // 发一个不改动文本的按键：keyPressEvent 末尾会调用 handleAutocomplete
        QKeyEvent press(QEvent::KeyPress, Qt::Key_Shift, Qt::NoModifier);
        QApplication::sendEvent(code, &press);
        pump(400);
    }
    {
        QPixmap shot = view->grab();
        const QPoint viewOrigin = view->mapToGlobal(QPoint(0, 0));
        QPainter painter(&shot);
        bool composited = false;
        const auto tops = QApplication::topLevelWidgets();
        for (QWidget *w : tops) {
            if (w == view || !w->isVisible() || w->width() <= 0 || w->height() <= 0) {
                continue;
            }
            const QPixmap ovl = w->grab();
            if (ovl.isNull()) {
                continue;
            }
            painter.drawPixmap(w->mapToGlobal(QPoint(0, 0)) - viewOrigin, ovl);
            composited = true;
        }
        painter.end();
        if (!composited) {
            std::cerr << "WARN: no completion popup composited" << std::endl;
        }
        const QString path = outDir + QStringLiteral("/03-editor-completion.png");
        if (!shot.save(path)) {
            std::cerr << "FAIL: shot not saved: " << path.toStdString() << std::endl;
            ++g_failures;
        } else {
            std::cout << "OK: Editor + 补全浮层 -> " << path.toStdString()
                      << " (" << shot.width() << 'x' << shot.height() << ')' << std::endl;
        }
    }
    {
        QKeyEvent esc(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
        QApplication::sendEvent(code, &esc);
        pump(200);
    }

    // 6) Compile：hexdump
    view->activateCompileTab();
    pump(500);
    saveShot(view, outDir + QStringLiteral("/04-compile.png"), QStringLiteral("Compile 标签页"));

    // 7) Gadgets（默认配置：不展示反汇编）
    view->activateGadgetsTab();
    pump(400);
    saveShot(view, outDir + QStringLiteral("/05-gadgets.png"), QStringLiteral("Gadgets 标签页"));

    // 8) Market：等列表回来（失败也会显示错误项，不阻塞）
    view->activateMarketTab();
    if (QWidget *marketTab = tabs->widget(3)) {
        if (QListWidget *list = marketTab->findChild<QListWidget *>()) {
            for (int i = 0; i < 30 && list->count() <= 1; ++i) {
                pump(500);
            }
        }
    }
    saveShot(view, outDir + QStringLiteral("/06-market.png"), QStringLiteral("程序广场标签页"));

    // 9) Settings
    tabs->setCurrentIndex(5);
    pump(400);
    saveShot(view, outDir + QStringLiteral("/07-settings.png"), QStringLiteral("Settings 标签页"));

    // 10) Disas：需要先切到该页，再打开「展示汇编」开关（setTabVisible 只在该页生效）
    tabs->setCurrentIndex(4);
    plugin->settings()->showGadgetDisasm = true;
    plugin->settings()->markChanged();
    pump(500);
    saveShot(view, outDir + QStringLiteral("/08-disas.png"), QStringLiteral("Disas 标签页"));

    // 11) Gadgets：开启反汇编后的面板
    view->activateGadgetsTab();
    pump(400);
    saveShot(view, outDir + QStringLiteral("/09-gadgets-disasm.png"),
             QStringLiteral("Gadgets + 反汇编"));

    std::cout << (g_failures == 0 ? "DONE: all shots saved" : "DONE with failures")
              << " (" << g_failures << " failed)" << std::endl;
    return g_failures == 0 ? 0 : 1;
}
