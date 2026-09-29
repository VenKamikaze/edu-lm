// SPDX-License-Identifier: GPL-2.0-or-later
#include "QuizRules.h"

namespace EduLM {

EndEarly parseEndEarly(const QString &text)
{
    const QString s = text.trimmed().toLower();
    if (s == QLatin1String("no"))
        return EndEarly::No;
    if (s == QLatin1String("pass"))
        return EndEarly::Pass;
    if (s == QLatin1String("fail"))
        return EndEarly::Fail;
    if (s == QLatin1String("both"))
        return EndEarly::Both;
    return EndEarly::Invalid;
}

QStringList QuizRules::validate() const
{
    QStringList problems;
    if (questionCount < 1)
        problems << QStringLiteral("QuestionCount must be at least 1");
    if (requiredCorrect < 1)
        problems << QStringLiteral("RequiredCorrect must be at least 1");
    if (requiredCorrect > questionCount)
        problems << QStringLiteral("RequiredCorrect (%1) is greater than QuestionCount (%2)").arg(requiredCorrect).arg(questionCount);
    if (maxSwaps < 0)
        problems << QStringLiteral("MaxSwaps must not be negative");
    if (answerAttempts < 1)
        problems << QStringLiteral("AnswerAttempts must be at least 1");
    if (endEarly == EndEarly::Invalid)
        problems << QStringLiteral("EndEarly must be one of no, pass, fail, both");
    return problems;
}

}
