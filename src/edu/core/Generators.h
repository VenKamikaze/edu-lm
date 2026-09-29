// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "Question.h"

#include <QRandomGenerator>

namespace EduLM {

bool isKnownGenerator(const QString &name);

// Checks the params of a generator entry and fills in defaults for missing ones.
// Returns an error message, or an empty string if the params are fine.
QString resolveGeneratorParams(const QString &generator, QMap<QString, IntRange> *params);

// Draws a fresh numeric question from a generator entry (params already resolved).
// The instance id is "<entry id>#<operands>".
Question instantiateGenerator(const Question &entry, QRandomGenerator &rng);

}
