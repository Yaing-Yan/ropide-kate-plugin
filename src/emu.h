/*
 * SPDX-FileCopyrightText: 2026 Yaing-Yan
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * CasioEmuMsvc MCP 客户端（极简，仅覆写内存）。
 * 移植自 ropide-vscode-plugin 的 src/emu.ts。
 *
 * CasioEmuMsvc 的 McpPlugin 在模拟器进程内启动 Streamable HTTP JSON-RPC：
 *   - GET  http://127.0.0.1:3001/health
 *   - POST http://127.0.0.1:3001/mcp     （tools/call）
 *   - write_memory 工具: arguments = { address: int, bytes: [int...] }
 */
#pragma once

#include <QObject>
#include <QString>

class QNetworkAccessManager;

namespace Rop
{

class EmuClient : public QObject
{
    Q_OBJECT

public:
    explicit EmuClient(QObject *parent = nullptr);

    /** 向模拟器内存写入字节（仅覆写，不做其它操作）。bytes 为原始字节。 */
    void writeMemory(int address, const QByteArray &bytes, int port);

    /** 解析 hex 字符串（容忍空格 / 0x 前缀 / 分隔符），非法返回空 QByteArray。 */
    static QByteArray parseHexBytes(const QString &s);

Q_SIGNALS:
    void writeFinished(bool ok, const QString &code, const QString &error);

private:
    void doWrite(int address, const QByteArray &bytes, int port);

    QNetworkAccessManager *m_nam = nullptr;
};

} // namespace Rop
