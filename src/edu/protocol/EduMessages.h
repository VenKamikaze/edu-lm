// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <QtGlobal>

namespace EduLM {

// Numbered from "ED" (0x4544) so they never collide with SDDM's own message values.
enum class EduGreeterMessage : quint32 {
    QuizHello = 0x45440000, // marks the connection as quiz-capable
    QuizQuery,
    QuizAnswer,
    QuizSwap,
    QuizAbandon,
    QuizNext, // after a revealing QuizFeedback: carry on with the quiz
};

enum class EduDaemonMessage : quint32 {
    QuizStatus = 0x45440100,
    QuizStarted,
    QuizQuestion,
    QuizFeedback,
    QuizSwapRefused,
    QuizResult,
    QuizCooldown,
    QuizError,
    QuizReady,        // reply to QuizHello: the daemon has registered this connection
    QuizLoginRefused, // a login was refused before any quiz (reason: wrong-password, busy)
};

}
