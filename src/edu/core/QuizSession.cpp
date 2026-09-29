// SPDX-License-Identifier: GPL-2.0-or-later
#include "QuizSession.h"

#include "Answer.h"
#include "Generators.h"

#include <iterator>

namespace EduLM {

QuizSession::QuizSession(const QuizRules &rules, const QList<Question> &pool, QRandomGenerator &rng)
    : m_rules(rules)
    , m_pool(pool)
    , m_rng(rng)
{
    m_errors = rules.validate();
    if (m_errors.isEmpty() && pool.size() < rules.questionCount)
        m_errors << QStringLiteral("question pool has %1 entries but %2 are needed").arg(pool.size()).arg(rules.questionCount);
    if (!m_errors.isEmpty()) {
        m_state = State::Invalid;
        return;
    }

    for (int i = 0; i < pool.size(); ++i)
        m_unused[pool.at(i).subject].append(i);

    for (int i = 0; i < rules.questionCount; ++i) {
        Slot slot;
        drawQuestion(&slot.question);
        m_slots.append(slot);
    }
}

bool QuizSession::drawQuestion(Question *out)
{
    if (m_unused.isEmpty())
        return false;

    auto subject = std::next(m_unused.begin(), m_rng.bounded(static_cast<int>(m_unused.size())));
    QList<int> &indexes = subject.value();
    const int pick = static_cast<int>(m_rng.bounded(static_cast<int>(indexes.size())));
    const Question &entry = m_pool.at(indexes.takeAt(pick));
    if (indexes.isEmpty())
        m_unused.erase(subject);

    *out = entry.type == QuestionType::Generator ? instantiateGenerator(entry, m_rng) : entry;
    return true;
}

const Question *QuizSession::current() const
{
    return m_state == State::InProgress ? &m_slots.at(m_index).question : nullptr;
}

int QuizSession::attemptsLeft() const
{
    return m_state == State::InProgress ? m_rules.answerAttempts - m_slots.at(m_index).attempts : 0;
}

QuizSession::Refusal QuizSession::checkCurrent(const QString &questionId) const
{
    if (m_state != State::InProgress)
        return Refusal::NotInProgress;
    if (questionId != m_slots.at(m_index).question.id)
        return Refusal::WrongQuestion;
    return Refusal::None;
}

QuizSession::AnswerResult QuizSession::answer(const QString &questionId, const QString &text)
{
    AnswerResult result;
    result.refusal = checkCurrent(questionId);
    if (!result.accepted())
        return result;

    Slot &slot = m_slots[m_index];
    slot.attempts++;
    slot.lastAnswer = text;
    result.correct = isCorrect(slot.question, text);
    slot.correct = result.correct;
    result.attemptsLeft = m_rules.answerAttempts - slot.attempts;
    result.questionFinished = result.correct || result.attemptsLeft == 0;

    if (result.questionFinished) {
        if (result.correct)
            m_correct++;
        m_index++;
        finishIfDone();
    }
    return result;
}

void QuizSession::finishIfDone()
{
    const int remaining = m_slots.size() - m_index;
    const bool endOnPass = m_rules.endEarly == EndEarly::Pass || m_rules.endEarly == EndEarly::Both;
    const bool endOnFail = m_rules.endEarly == EndEarly::Fail || m_rules.endEarly == EndEarly::Both;

    if (endOnPass && m_correct >= m_rules.requiredCorrect)
        m_state = State::Passed;
    else if (endOnFail && m_correct + remaining < m_rules.requiredCorrect)
        m_state = State::Failed;
    else if (remaining == 0)
        m_state = m_correct >= m_rules.requiredCorrect ? State::Passed : State::Failed;
}

QuizSession::SwapResult QuizSession::swap(const QString &questionId)
{
    SwapResult result;
    result.refusal = checkCurrent(questionId);
    if (!result.accepted())
        return result;

    if (swapsLeft() <= 0)
        result.refusal = Refusal::NoSwapsLeft;
    else if (m_slots.at(m_index).attempts > 0)
        result.refusal = Refusal::AlreadyAttempted;
    else if (!drawQuestion(&m_slots[m_index].question))
        result.refusal = Refusal::NoUnusedEntry;
    else
        m_swapsUsed++;
    return result;
}

QList<ReviewItem> QuizSession::review() const
{
    QList<ReviewItem> items;
    if (m_state != State::Failed || !m_rules.showCorrectAnswers)
        return items;

    for (const Slot &slot : m_slots) {
        // unreached questions (after an early fail) are not shown
        if (slot.correct || slot.attempts == 0)
            continue;
        items.append({slot.question.prompt, slot.question.choices, displayAnswer(slot.question, slot.lastAnswer),
                      correctAnswerText(slot.question), slot.question.explanation});
    }
    return items;
}

}
