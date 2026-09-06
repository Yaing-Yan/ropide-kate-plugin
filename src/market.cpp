/*
 * SPDX-FileCopyrightText: 2026 Yaing-Yan
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "market.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
#include <QUrlQuery>

namespace Rop
{

static const char MARKET_BASE_URL[] = "https://ropide.pages.dev";

MarketClient::MarketClient(QObject *parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
{
}

qint64 MarketClient::itemTime(const MarketItem &it)
{
    return it.timestamp;
}

bool MarketClient::parseItems(const QByteArray &json, QVector<MarketItem> &items)
{
    const QJsonDocument doc = QJsonDocument::fromJson(json);
    if (!doc.isArray()) {
        return false;
    }
    for (const QJsonValue &v : doc.array()) {
        if (!v.isObject()) {
            continue;
        }
        const QJsonObject o = v.toObject();
        MarketItem it;
        it.id = o.value(QLatin1String("id")).toVariant().toString();
        it.name = o.value(QLatin1String("name")).toString(QString());
        it.author = o.value(QLatin1String("author")).toString(QString());
        it.model = o.value(QLatin1String("model")).toString(QString());
        it.description = o.value(QLatin1String("description")).toString(QString());
        it.featured = o.value(QLatin1String("featured")).toVariant().toBool();
        const QJsonValue ts = o.value(QLatin1String("timestamp"));
        if (ts.isDouble()) {
            it.timestamp = (qint64)ts.toDouble();
        } else if (ts.isString()) {
            bool numOk = false;
            const double n = ts.toString().toDouble(&numOk);
            if (numOk) {
                it.timestamp = (qint64)n;
            } else {
                const QDateTime d = QDateTime::fromString(ts.toString(), Qt::ISODate);
                it.timestamp = d.isValid() ? d.toMSecsSinceEpoch() : 0;
            }
        }
        items.push_back(it);
    }
    return true;
}

void MarketClient::fetchList()
{
    QNetworkRequest req{QUrl(QString::fromLatin1(MARKET_BASE_URL) + QStringLiteral("/api/market"))};
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply *reply = m_nam->get(req);
    req.setTransferTimeout(15000);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        MarketListResult r;
        if (reply->error() != QNetworkReply::NoError) {
            r.error = QStringLiteral("HTTP %1 %2")
                          .arg(reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt())
                          .arg(reply->errorString());
            Q_EMIT listFinished(r);
            return;
        }
        const QByteArray body = reply->readAll();
        if (!parseItems(body, r.items)) {
            r.error = QStringLiteral("返回数据不是列表");
            Q_EMIT listFinished(r);
            return;
        }
        r.ok = true;
        Q_EMIT listFinished(r);
    });
}

void MarketClient::fetchItem(const QString &id)
{
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("id"), id);
    QUrl url = QUrl(QString::fromLatin1(MARKET_BASE_URL) + QStringLiteral("/api/market"));
    url.setQuery(query);
    QNetworkRequest req{url};
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply *reply = m_nam->get(req);
    req.setTransferTimeout(15000);
    connect(reply, &QNetworkReply::finished, this, [this, reply, id]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            Q_EMIT itemFinished(id, false, QString(),
                                QStringLiteral("HTTP %1 %2")
                                    .arg(reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt())
                                    .arg(reply->errorString()));
            return;
        }
        const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        const QString data = doc.object().value(QLatin1String("data")).toString(QString());
        if (data.isEmpty()) {
            Q_EMIT itemFinished(id, false, QString(), QStringLiteral("返回数据为空"));
            return;
        }
        Q_EMIT itemFinished(id, true, data, QString());
    });
}

void MarketClient::fetchChallenge()
{
    QUrl url = QUrl(QString::fromLatin1(MARKET_BASE_URL) + QStringLiteral("/api/market"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("challenge"), QStringLiteral("true"));
    url.setQuery(query);
    QNetworkRequest req{url};
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply *reply = m_nam->get(req);
    req.setTransferTimeout(15000);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        MarketChallengeResult r;
        if (reply->error() != QNetworkReply::NoError) {
            r.error = QStringLiteral("HTTP %1 %2")
                          .arg(reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt())
                          .arg(reply->errorString());
            Q_EMIT challengeFinished(r);
            return;
        }
        const QJsonObject o = QJsonDocument::fromJson(reply->readAll()).object();
        const QJsonValue token = o.value(QLatin1String("token"));
        const QJsonValue offset = o.value(QLatin1String("offset"));
        if (!token.isString() || !offset.isDouble()) {
            r.error = QStringLiteral("验证题目格式错误");
            Q_EMIT challengeFinished(r);
            return;
        }
        r.ok = true;
        r.token = token.toString();
        r.offset = (qint64)offset.toDouble();
        Q_EMIT challengeFinished(r);
    });
}

void MarketClient::publish(const QString &name, const QString &author, const QString &model,
                           const QString &description, const QString &ropDataJson,
                           const QString &challengeToken, const QString &challengeAnswer)
{
    QJsonObject payload;
    payload.insert(QLatin1String("name"), name);
    payload.insert(QLatin1String("author"), author);
    payload.insert(QLatin1String("model"), model);
    payload.insert(QLatin1String("description"), description);
    payload.insert(QLatin1String("data"), ropDataJson);
    payload.insert(QLatin1String("timestamp"), (double)QDateTime::currentMSecsSinceEpoch());
    payload.insert(QLatin1String("challengeToken"), challengeToken);
    payload.insert(QLatin1String("challengeAnswer"), challengeAnswer);

    QNetworkRequest req{QUrl(QString::fromLatin1(MARKET_BASE_URL) + QStringLiteral("/api/market"))};
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply *reply = m_nam->post(req, QJsonDocument(payload).toJson(QJsonDocument::Compact));
    req.setTransferTimeout(30000);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        MarketPublishResult r;
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() == QNetworkReply::NoError) {
            r.ok = true;
        } else if (status == 403) {
            r.code = QStringLiteral("wrong");
            r.error = QStringLiteral("字节错误");
        } else if (status == 410) {
            r.code = QStringLiteral("expired");
            r.error = QStringLiteral("题目已过期");
        } else {
            r.code = QStringLiteral("error");
            r.error = QStringLiteral("HTTP %1 %2").arg(status).arg(reply->errorString());
        }
        Q_EMIT publishFinished(r);
    });
}

} // namespace Rop
