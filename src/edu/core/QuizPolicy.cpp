// SPDX-License-Identifier: GPL-2.0-or-later
#include "QuizPolicy.h"

#include <grp.h>
#include <pwd.h>
#include <unistd.h>

#include <QByteArray>
#include <QVector>

namespace EduLM {

QList<QPair<QString, int>> parseYearGroups(const QStringList &entries, QStringList *errors)
{
    QList<QPair<QString, int>> result;
    for (const QString &entry : entries) {
        const int colon = entry.lastIndexOf(QLatin1Char(':'));
        bool ok = false;
        const QString group = entry.left(colon).trimmed();
        const int year = colon < 0 ? 0 : entry.mid(colon + 1).trimmed().toInt(&ok);
        if (!ok || group.isEmpty() || year < 1) {
            if (errors)
                errors->append(QStringLiteral("bad YearGroups entry '%1' (expected group:year)").arg(entry));
            continue;
        }
        result.append({group, year});
    }
    return result;
}

QuizDecision decideQuiz(const QString &user, const QStringList &userGroups, const QuizPolicyConfig &config)
{
    QuizDecision decision;
    decision.yearLevel = config.defaultYear;

    for (const auto &yearGroup : parseYearGroups(config.yearGroups)) {
        if (userGroups.contains(yearGroup.first)) {
            decision.yearLevel = yearGroup.second;
            break;
        }
    }

    if (!config.enabled || config.exemptUsers.contains(user))
        return decision;

    bool inGroup = false;
    for (const QString &g : config.groups)
        inGroup = inGroup || userGroups.contains(g);
    decision.quizRequired = inGroup || config.users.contains(user);
    return decision;
}

QStringList userGroups(const QString &user)
{
    const QByteArray name = user.toLocal8Bit();
    const passwd *pw = getpwnam(name.constData());
    if (!pw)
        return {};

    int count = 32;
    QVector<gid_t> gids(count);
    while (getgrouplist(name.constData(), pw->pw_gid, gids.data(), &count) < 0)
        gids.resize(count);

    QStringList names;
    for (int i = 0; i < count; ++i) {
        if (const group *gr = getgrgid(gids[i]))
            names.append(QString::fromLocal8Bit(gr->gr_name));
    }
    return names;
}

}
