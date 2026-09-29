// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>

namespace EduLM {

enum class QuestionType { MultipleChoice, TrueFalse, Numeric, ShortText, Generator };

struct IntRange {
    int lo = 0;
    int hi = 0;
};

struct Question {
    QString id;
    QString pack;
    QString subject;
    QuestionType type = QuestionType::MultipleChoice;
    QList<int> years;
    QString prompt;
    QString curriculum;
    QString explanation;
    int difficulty = 0;

    QStringList choices;
    int answerIndex = -1;
    bool answerBool = false;
    double answerNumber = 0;
    double tolerance = 0;
    QStringList accept;

    QString generator;
    QMap<QString, IntRange> params;
};

}
