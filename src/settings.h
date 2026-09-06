/*
 * SPDX-FileCopyrightText: 2026 Yaing-Yan
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * 全局设置（对应原插件的 ropide.* 配置项），存于 KConfig 文件 ropidekaterc。
 */
#pragma once

#include <QObject>
#include <QString>

class KConfig;

namespace Rop
{

class Settings : public QObject
{
    Q_OBJECT

public:
    explicit Settings(QObject *parent = nullptr);

    // 界面语言（zh-CN / en）
    QString language = QStringLiteral("zh-CN");
    bool showWelcomeOnStartup = true;
    // 【实验性】gadgets 面板展示反汇编（需要 _disas 文件）
    bool showGadgetDisasm = false;
    // 【实验性】悬停提示中展示反汇编
    bool showGadgetHoverDisasm = false;
    // CasioEmuMsvc McpPlugin 的 MCP 端口
    int casioemuMcpPort = 3001;

    // 覆写工具栏记忆项（原插件的 globalState）
    QString injectAddress;
    QString launcher;
    QString launcherAddr = QStringLiteral("D180");

    // 程序广场未读：最后查看时间（epoch 毫秒）
    qint64 marketLastSeen = 0;

    void load();
    void save();
    /** 保存并广播变更。 */
    void markChanged();

Q_SIGNALS:
    void changed();
};

} // namespace Rop
