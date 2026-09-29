// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <QString>
#include <QStringList>

namespace EduLM {

// When a running attempt may stop before all questions are asked.
enum class EndEarly { No, Pass, Fail, Both, Invalid };

// Case-insensitive and trimmed; anything unrecognised gives EndEarly::Invalid.
EndEarly parseEndEarly(const QString &text);

struct QuizRules {
    int questionCount = 5;
    int requiredCorrect = 4;
    int maxSwaps = 3; // shared by the whole attempt
    int answerAttempts = 1;
    bool showCorrectAnswers = true;
    EndEarly endEarly = EndEarly::No;

    // Human-readable problems; empty when the rules are usable.
    QStringList validate() const;
};

}
