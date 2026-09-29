// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "core/QuizPolicy.h"
#include "core/QuizRules.h"

#include <QStringList>

namespace EduLM {

// Plain copy of the settings the gate needs; re-read for every login.
struct GateConfig {
    QuizPolicyConfig policy; // includes the Enabled switch
    QuizRules rules;
    bool studentPasswords = true;
    int cooldownSeconds = 60;
    QStringList questionDirs;
    QStringList subjects;
};

}
