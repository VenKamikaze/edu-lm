// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "Question.h"

namespace EduLM {

// No-break spaces between thousands groups: "14\u00A0527". Numbers below 10000 have no separator.
QString formatNumber(qint64 value);

// Grammar after trimming: optional "-" (or U+2212), then either plain digits with an optional
// decimal part (".5" is fine), or groups of three digits split by one repeated separator
// (space, no-break space or comma) as in "4 527" and "4,527.5". Anything else fails.
bool parseNumber(const QString &text, double *value);

// Never throws or crashes on garbage input; unparseable answers are simply wrong.
bool isCorrect(const Question &question, const QString &answer);

// The answer key as text for the review screen.
QString correctAnswerText(const Question &question);

// The child's answer as text for the review screen (choice text for multiple choice).
QString displayAnswer(const Question &question, const QString &answer);

}
