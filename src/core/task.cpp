// SPDX-License-Identifier: GPL-3.0-or-later
#include "task.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimeZone>
#include <QUuid>

namespace KTasks
{

QDateTime Task::effectiveDue() const
{
    if (!due.isValid()) {
        return {};
    }
    if (allDay) {
        return QDateTime(due.date().addDays(1), QTime(0, 0));
    }
    return due;
}

QDateTime Task::alarmTime(const Alarm &alarm) const
{
    switch (alarm.kind) {
    case Alarm::Absolute:
        return alarm.at;
    case Alarm::RelativeToDue: {
        const QDateTime base = due.isValid() ? due : start;
        return base.isValid() ? base.addSecs(alarm.offsetSecs) : QDateTime();
    }
    case Alarm::RelativeToStart: {
        const QDateTime base = start.isValid() ? start : due;
        return base.isValid() ? base.addSecs(alarm.offsetSecs) : QDateTime();
    }
    }
    return {};
}

QString newUid()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

QString alarmsToJson(const QList<Alarm> &alarms)
{
    QJsonArray arr;
    for (const Alarm &a : alarms) {
        QJsonObject o;
        o[QStringLiteral("k")] = int(a.kind);
        if (a.kind == Alarm::Absolute) {
            o[QStringLiteral("at")] = a.at.toUTC().toString(Qt::ISODate);
        } else {
            o[QStringLiteral("o")] = a.offsetSecs;
        }
        arr.append(o);
    }
    return QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact));
}

QList<Alarm> alarmsFromJson(const QString &json)
{
    QList<Alarm> out;
    const QJsonArray arr = QJsonDocument::fromJson(json.toUtf8()).array();
    for (const QJsonValue &v : arr) {
        const QJsonObject o = v.toObject();
        Alarm a;
        a.kind = Alarm::Kind(o[QStringLiteral("k")].toInt(1));
        if (a.kind == Alarm::Absolute) {
            a.at = QDateTime::fromString(o[QStringLiteral("at")].toString(), Qt::ISODate);
        } else {
            a.offsetSecs = o[QStringLiteral("o")].toInt();
        }
        out.append(a);
    }
    return out;
}

double defaultSortOrder(const QDateTime &created)
{
    static const QDateTime epoch(QDate(2001, 1, 1), QTime(0, 0), QTimeZone::UTC);
    return double(epoch.secsTo(created.isValid() ? created : QDateTime::currentDateTimeUtc()));
}

}
