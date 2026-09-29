// SPDX-License-Identifier: GPL-2.0-or-later
#include "GateFactory.h"

#include "AuthVerifier.h"
#include "QuizGate.h"
#include "EduConfiguration.h"

#include <QRandomGenerator>

namespace EduLM {

GateConfig loadGateConfig()
{
    const EduConfig config; // loads on construction
    GateConfig snapshot;
    snapshot.policy = quizPolicyConfig(config);
    snapshot.rules = quizRules(config);
    snapshot.studentPasswords = config.Quiz.StudentPasswords.get();
    snapshot.cooldownSeconds = config.Quiz.FailCooldownSeconds.get();
    snapshot.questionDirs = config.Content.QuestionDirs.get();
    snapshot.subjects = config.Content.Subjects.get();
    return snapshot;
}

QuizGate *makeQuizGate(QObject *parent)
{
    // Displays are recreated after every session; the cooldown has to outlive them.
    // Held in memory only: a daemon restart or reboot clears all cooldowns.
    static CooldownTracker cooldown;

    auto *verifier = new AuthVerifier(parent);
    return new QuizGate(*verifier, loadGateConfig, userGroups, cooldown, *QRandomGenerator::global(), parent);
}

}
