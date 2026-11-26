// SPDX-License-Identifier: MIT
// Call history, stored as JSON in the application data directory (the last 500 calls).
#pragma once

#include <QDateTime>
#include <QList>
#include <QObject>
#include <QString>

class CallLog : public QObject
{
    Q_OBJECT
public:
    enum class Direction { Incoming, Outgoing, Missed };
    struct Entry {
        QDateTime time;
        Direction direction = Direction::Incoming;
        QString number;
        QString name;
        int seconds = 0;        // talk time; 0 if not connected
        bool elsewhere = false; // incoming, answered by another phone
    };

    explicit CallLog(QObject *parent = nullptr);

    const QList<Entry> &entries() const { return m_entries; } // newest first
    void add(const Entry &e);
    void remove(int index);
    void clear();

Q_SIGNALS:
    void changed();

private:
    void load();
    void save() const;
    QString path() const;
    QList<Entry> m_entries;
};
