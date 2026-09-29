// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "Question.h"
#include "QuizRules.h"

#include <QList>
#include <QMap>
#include <QRandomGenerator>

namespace EduLM {

struct ReviewItem {
    QString prompt;
    QStringList choices;
    QString yourAnswer;
    QString correctAnswer;
    QString explanation;
};

// One quiz attempt. The rng must outlive the session.
class QuizSession
{
public:
    enum class State { InProgress, Passed, Failed, Invalid };
    enum class Refusal { None, NotInProgress, WrongQuestion, NoSwapsLeft, AlreadyAttempted, NoUnusedEntry };

    struct AnswerResult {
        Refusal refusal = Refusal::None;
        bool correct = false;
        int attemptsLeft = 0;
        bool questionFinished = false;

        bool accepted() const { return refusal == Refusal::None; }
    };

    struct SwapResult {
        Refusal refusal = Refusal::None;

        bool accepted() const { return refusal == Refusal::None; }
    };

    // pool: candidate entries already filtered by year and subject.
    QuizSession(const QuizRules &rules, const QList<Question> &pool, QRandomGenerator &rng);

    State state() const { return m_state; }
    const QStringList &errors() const { return m_errors; }
    const QuizRules &rules() const { return m_rules; }

    // Null unless InProgress.
    const Question *current() const;
    int currentIndex() const { return m_index; }
    int correctCount() const { return m_correct; }
    int swapsLeft() const { return m_rules.maxSwaps - m_swapsUsed; }
    int attemptsLeft() const;

    AnswerResult answer(const QString &questionId, const QString &text);
    SwapResult swap(const QString &questionId);

    // Missed questions; empty unless Failed and showCorrectAnswers is set.
    QList<ReviewItem> review() const;

private:
    struct Slot {
        Question question;
        int attempts = 0;
        bool correct = false;
        QString lastAnswer;
    };

    // Picks a subject with unused entries at random, then an entry from it.
    bool drawQuestion(Question *out);
    Refusal checkCurrent(const QString &questionId) const;
    void finishIfDone();

    QuizRules m_rules;
    QList<Question> m_pool;
    QRandomGenerator &m_rng;
    QMap<QString, QList<int>> m_unused; // subject -> unused pool indexes
    QList<Slot> m_slots;
    QStringList m_errors;
    State m_state = State::InProgress;
    int m_index = 0;
    int m_correct = 0;
    int m_swapsUsed = 0;
};

}
