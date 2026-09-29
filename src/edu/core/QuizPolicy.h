// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <QList>
#include <QPair>
#include <QStringList>

namespace EduLM {

struct QuizPolicyConfig {
    bool enabled = true;
    QStringList groups;
    QStringList users;
    QStringList exemptUsers;
    QStringList yearGroups; // "group:year" entries in config order
    int defaultYear = 4;
};

struct QuizDecision {
    bool quizRequired = false;
    int yearLevel = 0;
};

// Splits "group:year" entries; malformed ones are dropped and described in *errors.
QList<QPair<QString, int>> parseYearGroups(const QStringList &entries, QStringList *errors = nullptr);

QuizDecision decideQuiz(const QString &user, const QStringList &userGroups, const QuizPolicyConfig &config);

// All groups of a system user (primary and supplementary); empty if the user is unknown.
QStringList userGroups(const QString &user);

}
