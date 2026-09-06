/*
 * SPDX-FileCopyrightText: 2026 Yaing-Yan
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * 程序广场（Market）——对接 ropide.pages.dev 的 /api/market。
 * 移植自 ropide-vscode-plugin 的 src/market.ts / marketState.ts。
 */
#pragma once

#include <QObject>
#include <QString>
#include <QVector>

class QNetworkAccessManager;

namespace Rop
{

struct MarketItem {
    QString id;
    QString name;
    QString author;
    QString model;
    QString description;
    bool featured = false;
    qint64 timestamp = 0;
};

struct MarketListResult {
    bool ok = false;
    QVector<MarketItem> items;
    QString error;
};

struct MarketChallengeResult {
    bool ok = false;
    QString token;
    qint64 offset = 0;
    QString error;
};

struct MarketPublishResult {
    bool ok = false;
    QString code; // "wrong" / "expired" / "error"
    QString error;
};

class MarketClient : public QObject
{
    Q_OBJECT

public:
    explicit MarketClient(QObject *parent = nullptr);

    void fetchList();
    void fetchItem(const QString &id);
    void fetchChallenge();
    void publish(const QString &name, const QString &author, const QString &model,
                 const QString &description, const QString &ropDataJson,
                 const QString &challengeToken, const QString &challengeAnswer);

    /** 条目时间戳统一成 epoch 毫秒（兼容 number / 数字字符串 / ISO 字符串）。 */
    static qint64 itemTime(const MarketItem &it);
    /** 解析 /api/market 返回的列表 JSON；非数组返回 false。 */
    static bool parseItems(const QByteArray &json, QVector<MarketItem> &items);

Q_SIGNALS:
    void listFinished(const Rop::MarketListResult &result);
    /** itemFinished 的 ropJson 为服务端返回的 .rop JSON 字符串（解析交给调用方）。 */
    void itemFinished(const QString &id, bool ok, const QString &ropJson, const QString &error);
    void challengeFinished(const Rop::MarketChallengeResult &result);
    void publishFinished(const Rop::MarketPublishResult &result);

private:
    QNetworkAccessManager *m_nam = nullptr;
};

} // namespace Rop
