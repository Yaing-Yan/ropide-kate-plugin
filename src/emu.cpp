/*
 * SPDX-FileCopyrightText: 2026 Yaing-Yan
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "emu.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

namespace Rop
{

namespace
{
const char MCP_HOST[] = "127.0.0.1";
const char NOT_RUNNING[] =
    "找不到正在运行的CasioEmuMsvc，或者进程不支持MCP";
}

EmuClient::EmuClient(QObject *parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
{
}

QByteArray EmuClient::parseHexBytes(const QString &s)
{
    QString cleaned = s;
    cleaned.remove(QLatin1String("0x"), Qt::CaseInsensitive);
    QString hexOnly;
    for (const QChar c : cleaned) {
        if ((c >= QLatin1Char('0') && c <= QLatin1Char('9'))
            || (c >= QLatin1Char('a') && c <= QLatin1Char('f'))
            || (c >= QLatin1Char('A') && c <= QLatin1Char('F'))) {
            hexOnly.append(c);
        }
    }
    if (hexOnly.isEmpty() || hexOnly.size() % 2 != 0) {
        return {};
    }
    QByteArray bytes;
    bytes.reserve(hexOnly.size() / 2);
    for (int i = 0; i < hexOnly.size(); i += 2) {
        bytes.append((char)(hexOnly.mid(i, 2).toInt(nullptr, 16) & 0xff));
    }
    return bytes;
}

void EmuClient::writeMemory(int address, const QByteArray &bytes, int port)
{
    // 先探活（与 JS 版一致：health 2s 超时，写 10s 超时）
    QNetworkRequest req{QUrl(QStringLiteral("http://%1:%2/health").arg(QString::fromLatin1(MCP_HOST)).arg(port))};
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply *reply = m_nam->get(req);
    req.setTransferTimeout(2000);
    connect(reply, &QNetworkReply::finished, this, [this, reply, address, bytes, port]() {
        reply->deleteLater();
        bool healthy = false;
        if (reply->error() == QNetworkReply::NoError) {
            const QJsonObject o = QJsonDocument::fromJson(reply->readAll()).object();
            healthy = o.value(QLatin1String("status")).toString() == QLatin1String("ok");
        }
        if (!healthy) {
            Q_EMIT writeFinished(false, QStringLiteral("not-running"), QString::fromLatin1(NOT_RUNNING));
            return;
        }
        doWrite(address, bytes, port);
    });
}

void EmuClient::doWrite(int address, const QByteArray &bytes, int port)
{
    QJsonArray byteArr;
    for (unsigned char b : bytes) {
        byteArr.append((int)b);
    }
    QJsonObject args;
    args.insert(QLatin1String("address"), address);
    args.insert(QLatin1String("bytes"), byteArr);
    QJsonObject params;
    params.insert(QLatin1String("name"), QStringLiteral("write_memory"));
    params.insert(QLatin1String("arguments"), args);
    QJsonObject body;
    body.insert(QLatin1String("jsonrpc"), QStringLiteral("2.0"));
    body.insert(QLatin1String("id"), 1);
    body.insert(QLatin1String("method"), QStringLiteral("tools/call"));
    body.insert(QLatin1String("params"), params);

    QNetworkRequest req{QUrl(QStringLiteral("http://%1:%2/mcp").arg(QString::fromLatin1(MCP_HOST)).arg(port))};
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    req.setRawHeader(QByteArrayLiteral("Accept"), QByteArrayLiteral("application/json, text/event-stream"));
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply *reply = m_nam->post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
    req.setTransferTimeout(10000);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            Q_EMIT writeFinished(false, QStringLiteral("http"),
                                 status ? QStringLiteral("MCP 返回 HTTP %1").arg(status)
                                        : QStringLiteral("MCP 连接失败：%1").arg(reply->errorString()));
            return;
        }
        const QJsonObject o = QJsonDocument::fromJson(reply->readAll()).object();
        const QJsonValue err = o.value(QLatin1String("error"));
        if (err.isObject()) {
            Q_EMIT writeFinished(false, QStringLiteral("mcp"),
                                 err.toObject().value(QLatin1String("message")).toString(QStringLiteral("MCP 错误")));
            return;
        }
        const QJsonObject result = o.value(QLatin1String("result")).toObject();
        if (result.value(QLatin1String("isError")).toBool()) {
            const QJsonArray content = result.value(QLatin1String("content")).toArray();
            const QString text = content.isEmpty() ? QString()
                                                   : content.at(0).toObject().value(QLatin1String("text")).toString();
            Q_EMIT writeFinished(false, QStringLiteral("mcp"), text.isEmpty() ? QStringLiteral("写入失败") : text);
            return;
        }
        Q_EMIT writeFinished(true, QString(), QString());
    });
}

} // namespace Rop
