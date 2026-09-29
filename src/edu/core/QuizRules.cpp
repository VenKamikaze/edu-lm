// SPDX-License-Identifier: GPL-2.0-or-later
#include "QuizRules.h"

namespace EduLM {

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
    return problems;
}

}
