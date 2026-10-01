// SPDX-License-Identifier: GPL-2.0-or-later
#include "EduProtocol.h"

namespace EduLM {

QDataStream &operator<<(QDataStream &s, const QuizQueryMsg &m) { return s << m.user; }
QDataStream &operator>>(QDataStream &s, QuizQueryMsg &m) { return s >> m.user; }

QDataStream &operator<<(QDataStream &s, const QuizAnswerMsg &m) { return s << m.questionId << m.answer; }
QDataStream &operator>>(QDataStream &s, QuizAnswerMsg &m) { return s >> m.questionId >> m.answer; }

QDataStream &operator<<(QDataStream &s, const QuizSwapMsg &m) { return s << m.questionId; }
QDataStream &operator>>(QDataStream &s, QuizSwapMsg &m) { return s >> m.questionId; }

QDataStream &operator<<(QDataStream &s, const QuizStatusMsg &m)
{
    return s << m.user << m.required << m.studentPasswords << m.cooldownSeconds << m.questionCount << m.requiredCorrect;
}

QDataStream &operator>>(QDataStream &s, QuizStatusMsg &m)
{
    return s >> m.user >> m.required >> m.studentPasswords >> m.cooldownSeconds >> m.questionCount >> m.requiredCorrect;
}

QDataStream &operator<<(QDataStream &s, const QuizStartedMsg &m)
{
    return s << m.user << m.count << m.required << m.maxSwaps;
}

QDataStream &operator>>(QDataStream &s, QuizStartedMsg &m)
{
    return s >> m.user >> m.count >> m.required >> m.maxSwaps;
}

QDataStream &operator<<(QDataStream &s, const QuizQuestionMsg &m)
{
    return s << m.id << m.index << m.count << m.prompt << m.type << m.choices << m.swapsLeft << m.attemptsLeft;
}

QDataStream &operator>>(QDataStream &s, QuizQuestionMsg &m)
{
    return s >> m.id >> m.index >> m.count >> m.prompt >> m.type >> m.choices >> m.swapsLeft >> m.attemptsLeft;
}

QDataStream &operator<<(QDataStream &s, const QuizFeedbackMsg &m)
{
    return s << m.id << m.correct << m.attemptsLeft << m.questionFinished << m.revealed << m.correctAnswer << m.explanation;
}

QDataStream &operator>>(QDataStream &s, QuizFeedbackMsg &m)
{
    return s >> m.id >> m.correct >> m.attemptsLeft >> m.questionFinished >> m.revealed >> m.correctAnswer >> m.explanation;
}

QDataStream &operator<<(QDataStream &s, const QuizSwapRefusedMsg &m) { return s << m.reason; }
QDataStream &operator>>(QDataStream &s, QuizSwapRefusedMsg &m) { return s >> m.reason; }

QDataStream &operator<<(QDataStream &s, const QuizReviewItem &m)
{
    return s << m.prompt << m.choices << m.givenAnswer << m.correctAnswer << m.explanation;
}

QDataStream &operator>>(QDataStream &s, QuizReviewItem &m)
{
    return s >> m.prompt >> m.choices >> m.givenAnswer >> m.correctAnswer >> m.explanation;
}

QDataStream &operator<<(QDataStream &s, const QuizResultMsg &m)
{
    s << m.passed << m.correct << m.required << m.asked << quint32(m.review.size());
    for (const QuizReviewItem &item : m.review)
        s << item;
    return s << m.cooldownSeconds;
}

QDataStream &operator>>(QDataStream &s, QuizResultMsg &m)
{
    quint32 count = 0;
    s >> m.passed >> m.correct >> m.required >> m.asked >> count;
    m.review.clear();
    // a corrupt count must not make us allocate or loop unboundedly
    for (quint32 i = 0; i < count && s.status() == QDataStream::Ok; ++i) {
        QuizReviewItem item;
        s >> item;
        m.review.append(item);
    }
    return s >> m.cooldownSeconds;
}

QDataStream &operator<<(QDataStream &s, const QuizCooldownMsg &m) { return s << m.seconds; }
QDataStream &operator>>(QDataStream &s, QuizCooldownMsg &m) { return s >> m.seconds; }

QDataStream &operator<<(QDataStream &s, const QuizErrorMsg &m) { return s << m.message; }
QDataStream &operator>>(QDataStream &s, QuizErrorMsg &m) { return s >> m.message; }

QDataStream &operator<<(QDataStream &s, const QuizLoginRefusedMsg &m) { return s << m.reason; }
QDataStream &operator>>(QDataStream &s, QuizLoginRefusedMsg &m) { return s >> m.reason; }

QDataStream &operator<<(QDataStream &s, const QuizVersionMsg &m) { return s << m.version; }
QDataStream &operator>>(QDataStream &s, QuizVersionMsg &m) { return s >> m.version; }

}
