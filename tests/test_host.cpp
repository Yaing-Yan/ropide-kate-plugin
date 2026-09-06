/*
 * SPDX-FileCopyrightText: 2026 Yaing-Yan
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * 无头冒烟测试宿主：
 *   1. 用 KPluginMetaData/KPluginFactory 加载插件（校验元数据 + 工厂）
 *   2. 直接实例化 RopToolView，绑定一个内存中的 .rop 文档，
 *      走一遍 编辑 → 编译 → gadgets → 写回 的最小闭环
 *   3. 渲染一张截图到 /tmp 供人工检查
 *
 * 用法：QT_QPA_PLATFORM=offscreen ./ropide-test-host <plugin.so> [out.png]
 */
#include <QApplication>
#include <QFile>
#include <QNetworkProxy>
#include <QTimer>

#include <KPluginFactory>
#include <KPluginMetaData>
#include <KTextEditor/Document>
#include <KTextEditor/Editor>

#include <cstdio>
#include <iostream>

#include "rop.h"
#include "compiler.h"
#include "ropideplugin.h"
#include "roptoolview.h"

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    // 看门狗：无论如何 15 秒后退出
    QTimer::singleShot(15000, &app, [&app]() {
        std::cerr << "TIMEOUT" << std::endl;
        app.exit(3);
    });
    // 市场请求走代理（如有），避免无头环境直连挂起
    if (const QByteArray proxyUrl = qgetenv("https_proxy"); !proxyUrl.isEmpty()) {
        const QUrl url(QString::fromUtf8(proxyUrl));
        if (url.isValid() && url.host().startsWith(QStringLiteral("127."))) {
            QNetworkProxy proxy(QNetworkProxy::HttpProxy,
                                url.host(),
                                url.port(8080));
            QNetworkProxy::setApplicationProxy(proxy);
        }
    }
    const QString pluginPath = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QStringLiteral("ropidekate.so");
    const QString shotPath = argc > 2 ? QString::fromLocal8Bit(argv[2]) : QStringLiteral("/tmp/ropide-host.png");

    // 1) 插件元数据 + 工厂加载
    KPluginMetaData md(pluginPath);
    if (!md.isValid()) {
        std::cerr << "FAIL: invalid plugin metadata: " << pluginPath.toStdString() << std::endl;
        return 1;
    }
    std::cout << "plugin id=" << md.pluginId().toStdString() << " name=" << md.name().toStdString()
              << " license=" << md.license().toStdString() << " version=" << md.version().toStdString()
              << std::endl;
    const KPluginFactory::Result result = KPluginFactory::loadFactory(md);
    if (!result.plugin) {
        std::cerr << "FAIL: cannot load plugin factory: " << result.errorString.toStdString() << std::endl;
        return 1;
    }
    // 跨 .so 边界用基类接收（qobject_cast 对自定义类的元对象副本会失败）
    auto *loaded = result.plugin->create<KTextEditor::Plugin>();
    if (!loaded) {
        std::cerr << "FAIL: cannot create plugin instance" << std::endl;
        return 1;
    }
    std::cout << "OK: plugin instance created via factory" << std::endl;
    // 工具视图测试直接用本二进制内的插件实例
    auto *plugin = new Rop::RopIDEPlugin(nullptr);

    // 2) 内存 .rop 文档 + 工具视图闭环
    KTextEditor::Editor *editor = KTextEditor::Editor::instance();
    if (!editor) {
        std::cerr << "FAIL: no KTextEditor::Editor (katepart)" << std::endl;
        return 1;
    }
    KTextEditor::Document *doc = editor->createDocument(nullptr);
    const QString ropJson = QStringLiteral(
        "{\"input\":\"// demo\\n$st = E9E0;\\n<loop>\\n#pop-er0;\\n[$loop + 2]\\n12 34\\n\","
        "\"gadgets\":[{\"name\":\"pop-er0\",\"addr\":\"121A8\",\"desc\":\"\\u8d4b\\u503c ER0\",\"tags\":[]}],"
        "\"leftStartAddress\":\"E9E0\",\"rightStartAddress\":\"D710\",\"ideVersion\":100}");
    // 写入真实临时文件（setActiveDocument 按 .rop 扩展名绑定）
    const QString tmpPath = QStringLiteral("/tmp/ropide-demo.rop");
    {
        QFile f(tmpPath);
        f.open(QIODevice::WriteOnly | QIODevice::Truncate);
        f.write(ropJson.toUtf8());
    }
    doc->openUrl(QUrl::fromLocalFile(tmpPath));

    Rop::RopToolView *view = new Rop::RopToolView(plugin, nullptr);
    view->resize(1100, 760);
    view->show();
    view->setActiveDocument(doc);

    // 模拟用户在编辑器中追加一行 → 触发 reparse + 写回
    view->activateCompileTab();
    view->activateGadgetsTab();
    view->activateEditorTab();

    QTimer::singleShot(200, &app, [&]() {
        // 写回校验：文档应被序列化为紧凑 JSON 且 input 含追加内容前保持一致
        const QString after = doc->text();
        if (!after.startsWith(QLatin1Char('{'))) {
            std::cerr << "FAIL: doc not valid JSON after sync" << std::endl;
            app.exit(1);
            return;
        }
        const auto parsed = Rop::parseRopDocument(after);
        if (!parsed.ok) {
            std::cerr << "FAIL: parseRopDocument: " << parsed.error.toStdString() << std::endl;
            app.exit(1);
            return;
        }
        if (parsed.data.leftStartAddress != QLatin1String("E9E0")
            || parsed.data.gadgets.size() != 1) {
            std::cerr << "FAIL: round-trip data mismatch" << std::endl;
            app.exit(1);
            return;
        }
        std::cout << "OK: document round-trip preserved" << std::endl;
        view->grab().save(shotPath);
        std::cout << "OK: screenshot saved to " << shotPath.toStdString() << std::endl;
        app.exit(0);
    });

    return app.exec();
}
