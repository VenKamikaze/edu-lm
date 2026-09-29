// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "GateConfig.h"

class QObject;

namespace EduLM {

class QuizGate;

// Reads /etc/edu-lm/edu-lm.conf afresh on every call.
GateConfig loadGateConfig();

// The gate wired to the real config, PAM, groups and the process-wide cooldown tracker.
QuizGate *makeQuizGate(QObject *parent);

}
