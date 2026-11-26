// SPDX-License-Identifier: MIT
#include "calllog.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>

static constexpr int kMaxEntries = 500;

CallLog::CallLog(QObject *parent)
    : QObject(parent)
{
    load();
}

QString CallLog::path() const
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/calls.json");
}

static QString dirName(CallLog::Direction d)
{
    switch (d) {
    case CallLog::Direction::Outgoing: return QStringLiteral("out");
    case CallLog::Direction::Missed: return QStringLiteral("missed");
    case CallLog::Direction::Incoming: break;
    }
    return QStringLiteral("in");
}

void CallLog::load()
{
    QFile f(path());
    if (!f.open(QIODevice::ReadOnly))
        return;
    const QJsonArray arr = QJsonDocument::fromJson(f.readAll()).array();
    for (const QJsonValue &v : arr) {
        const QJsonObject o = v.toObject();
        Entry e;
        e.time = QDateTime::fromString(o.value(QStringLiteral("time")).toString(), Qt::ISODate);
        const QString d = o.value(QStringLiteral("dir")).toString();
        e.direction = d == QLatin1String("out") ? Direction::Outgoing : d == QLatin1String("missed") ? Direction::Missed : Direction::Incoming;
        e.number = o.value(QStringLiteral("number")).toString();
        e.name = o.value(QStringLiteral("name")).toString();
        e.seconds = o.value(QStringLiteral("seconds")).toInt();
        e.elsewhere = o.value(QStringLiteral("elsewhere")).toBool();
        if (e.time.isValid())
            m_entries.append(e);
    }
}

void CallLog::save() const
{
    QJsonArray arr;
    for (const Entry &e : m_entries) {
        QJsonObject o{{QStringLiteral("time"), e.time.toString(Qt::ISODate)},
                      {QStringLiteral("dir"), dirName(e.direction)},
                      {QStringLiteral("number"), e.number},
                      {QStringLiteral("seconds"), e.seconds}};
        if (!e.name.isEmpty())
            o.insert(QStringLiteral("name"), e.name);
        if (e.elsewhere)
            o.insert(QStringLiteral("elsewhere"), true);
        arr.append(o);
    }
    QDir().mkpath(QFileInfo(path()).absolutePath());
    QSaveFile f(path());
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QJsonDocument(arr).toJson(QJsonDocument::Compact));
        f.commit();
    }
}

void CallLog::add(const Entry &e)
{
    if (e.number.isEmpty())
        return;
    m_entries.prepend(e);
    while (m_entries.size() > kMaxEntries)
        m_entries.removeLast();
    save();
    Q_EMIT changed();
}

void CallLog::remove(int index)
{
    if (index < 0 || index >= m_entries.size())
        return;
    m_entries.removeAt(index);
    save();
    Q_EMIT changed();
}

void CallLog::clear()
{
    m_entries.clear();
    save();
    Q_EMIT changed();
}
