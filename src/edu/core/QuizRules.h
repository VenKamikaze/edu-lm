// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <QStringList>

namespace EduLM {

struct QuizRules {
    int questionCount = 5;
    int requiredCorrect = 4;
    int maxSwaps = 3; // shared by the whole attempt
    int answerAttempts = 1;
    bool showCorrectAnswers = true;

    // Human-readable problems; empty when the rules are usable.
    QStringList validate() const;
};

}
