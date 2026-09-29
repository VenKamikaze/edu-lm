// SPDX-License-Identifier: GPL-2.0-or-later
#include "QuizGate.h"

#include "core/QuestionBank.h"

#include <QDataStream>
#include <QDebug>
#include <QLocalSocket>

namespace EduLM {

namespace {

const int MaxYear = 12;

QStringList configProblems(const GateConfig &config)
{
    QStringList problems = config.rules.validate();

    QStringList yearErrors;
    for (const auto &yearGroup : parseYearGroups(config.policy.yearGroups, &yearErrors)) {
        if (yearGroup.second > MaxYear)
            yearErrors << QStringLiteral("YearGroups year %1 for '%2' must be between 1 and %3")
                              .arg(yearGroup.second).arg(yearGroup.first).arg(MaxYear);
    }
    problems += yearErrors;

    if (config.policy.defaultYear < 1 || config.policy.defaultYear > MaxYear)
        problems << QStringLiteral("DefaultYear %1 must be between 1 and %2").arg(config.policy.defaultYear).arg(MaxYear);
    return problems;
}

QString typeName(QuestionType type)
{
    switch (type) {
    case QuestionType::MultipleChoice: return QStringLiteral("multiple_choice");
    case QuestionType::TrueFalse: return QStringLiteral("true_false");
    case QuestionType::Numeric: return QStringLiteral("numeric");
    case QuestionType::ShortText: return QStringLiteral("short_text");
    case QuestionType::Generator: break;
    }
    return QStringLiteral("generator");
}

// Empty for refusals the theme should not hear about (stale ids, no quiz running).
QString refusalReason(QuizSession::Refusal refusal)
{
    switch (refusal) {
    case QuizSession::Refusal::NoSwapsLeft: return QStringLiteral("no-swaps-left");
    case QuizSession::Refusal::AlreadyAttempted: return QStringLiteral("already-attempted");
    case QuizSession::Refusal::NoUnusedEntry: return QStringLiteral("no-more-questions");
    default: return QString();
    }
}

}

// Best effort: copies of the password held elsewhere (SDDM's own) are not wiped.
void QuizGate::PendingLogin::wipe()
{
    password.fill(QChar(0));
    password.clear();
    user.clear();
    proceed = nullptr;
    socket = nullptr;
}

QuizGate::QuizGate(PasswordVerifier &verifier, ConfigProvider config, GroupLookup groups,
                   CooldownTracker &cooldown, QRandomGenerator &rng, QObject *parent)
    : QObject(parent)
    , m_verifier(verifier)
    , m_config(std::move(config))
    , m_groups(std::move(groups))
    , m_cooldown(cooldown)
    , m_rng(rng)
{
    connect(&m_verifier, &PasswordVerifier::verified, this, &QuizGate::onVerified);
}

QuizGate::~QuizGate()
{
    m_pending.wipe();
}

template<typename Payload>
void QuizGate::sendTo(QLocalSocket *socket, EduDaemonMessage id, const Payload &payload)
{
    if (socket)
        socket->write(encodeMessage(id, payload));
}

void QuizGate::sendError(const QString &message)
{
    sendTo(m_quizSocket, EduDaemonMessage::QuizError, QuizErrorMsg{message});
}

void QuizGate::refuseLogin(const QString &reason)
{
    sendTo(m_quizSocket, EduDaemonMessage::QuizLoginRefused, QuizLoginRefusedMsg{reason});
}

QuizStatusMsg QuizGate::statusFor(const QString &user, const GateConfig &config, bool required)
{
    QuizStatusMsg status;
    status.user = user;
    status.required = required;
    status.studentPasswords = config.studentPasswords;
    status.cooldownSeconds = m_cooldown.secondsLeft(user);
    status.questionCount = config.rules.questionCount;
    status.requiredCorrect = config.rules.requiredCorrect;
    return status;
}

void QuizGate::refuseReservedUser()
{
    refuseLogin(QStringLiteral("refused"));
}

bool QuizGate::needsQuiz(const QString &user) const
{
    return decideQuiz(user, m_groups(user), m_config().policy).quizRequired;
}

void QuizGate::reportConfigProblems(QLocalSocket *loginSocket, const QString &user, const QStringList &problems)
{
    qCritical().noquote() << "Edu-LM: the quiz configuration is unusable, refusing login of" << user
                          << "-" << problems.join(QStringLiteral("; "));
    const QString message = QStringLiteral("Edu-LM is misconfigured, ask a parent to check it: ")
                            + problems.join(QStringLiteral("; "));
    sendError(message);
    failLogin(loginSocket, message);
}

void QuizGate::failLogin(QLocalSocket *loginSocket, const QString &information)
{
    if (!loginSocket)
        return;
    if (!information.isEmpty())
        emit informationMessage(loginSocket, information);
    emit loginFailed(loginSocket);
}

bool QuizGate::intercept(QLocalSocket *socket, const QString &user, const QString &password, Continuation proceed)
{
    const GateConfig config = m_config();
    if (!decideQuiz(user, m_groups(user), config.policy).quizRequired) {
        // the greeter learns "no quiz" here, so it can read a later LoginFailed as a wrong password
        if (m_quizSocket)
            sendTo(m_quizSocket, EduDaemonMessage::QuizStatus, statusFor(user, config, false));
        return false;
    }

    const QStringList problems = configProblems(config);
    if (!problems.isEmpty()) {
        reportConfigProblems(socket, user, problems);
        return true;
    }

    if (!m_quizSocket) {
        qInfo() << "Edu-LM: refusing login of" << user << "- the greeter does not support the quiz";
        failLogin(socket, QStringLiteral("This account needs the Edu-LM login screen"));
        return true;
    }

    if (const int seconds = m_cooldown.secondsLeft(user)) {
        sendTo(m_quizSocket, EduDaemonMessage::QuizCooldown, QuizCooldownMsg{seconds});
        failLogin(socket);
        return true;
    }

    if (busy()) {
        qInfo() << "Edu-LM: refusing login of" << user << "- a verification or quiz is already in progress";
        refuseLogin(QStringLiteral("busy"));
        failLogin(socket);
        return true;
    }

    m_pending.socket = socket;
    m_pending.user = user;
    m_pending.password = password;
    m_pending.proceed = std::move(proceed);
    m_hasPending = true;
    connect(socket, &QLocalSocket::disconnected, this, &QuizGate::onLoginSocketDisconnected, Qt::UniqueConnection);

    m_verifying = true;
    m_verifier.verify(user, password);
    return true;
}

void QuizGate::onVerified(bool ok)
{
    m_verifying = false;
    if (!m_hasPending) // discarded while the check was running
        return;

    QLocalSocket *loginSocket = m_pending.socket;
    if (!ok) {
        m_hasPending = false;
        m_pending.wipe();
        refuseLogin(QStringLiteral("wrong-password"));
        failLogin(loginSocket);
        return;
    }

    startQuiz(m_config());
}

// Re-reads the question packs every time so edits by a parent apply to the next attempt.
void QuizGate::startQuiz(const GateConfig &config)
{
    QLocalSocket *loginSocket = m_pending.socket;
    const QString user = m_pending.user;

    auto reject = [&](const QStringList &problems) {
        reportConfigProblems(loginSocket, user, problems);
        m_hasPending = false;
        m_pending.wipe();
    };

    const QStringList problems = configProblems(config);
    if (!problems.isEmpty())
        return reject(problems);

    if (!m_quizSocket) {
        failLogin(loginSocket, QStringLiteral("This account needs the Edu-LM login screen"));
        m_hasPending = false;
        m_pending.wipe();
        return;
    }

    QuestionBank bank;
    bank.load(config.questionDirs);
    for (const LoadIssue &issue : bank.errors())
        qWarning().noquote() << "Edu-LM: question pack problem:" << issue.toString();

    const QuizDecision decision = decideQuiz(user, m_groups(user), config.policy);
    auto quiz = std::make_unique<QuizSession>(config.rules, bank.filter(decision.years, config.subjects), m_rng);
    if (quiz->state() == QuizSession::State::Invalid)
        return reject(quiz->errors());

    m_quiz = std::move(quiz);
    m_answerAttempts = 0;
    m_cooldownSeconds = config.cooldownSeconds;
    qInfo() << "Edu-LM: quiz started for" << user;
    sendTo(m_quizSocket, EduDaemonMessage::QuizStarted,
           QuizStartedMsg{user, config.rules.questionCount, config.rules.requiredCorrect, config.rules.maxSwaps});
    sendQuestion();
}

void QuizGate::sendQuestion()
{
    const Question *question = m_quiz->current();
    QuizQuestionMsg msg;
    msg.id = question->id;
    msg.index = m_quiz->currentIndex();
    msg.count = m_quiz->rules().questionCount;
    msg.prompt = question->prompt;
    msg.type = typeName(question->type);
    msg.choices = question->choices;
    msg.swapsLeft = m_quiz->swapsLeft();
    msg.attemptsLeft = m_quiz->attemptsLeft();
    sendTo(m_quizSocket, EduDaemonMessage::QuizQuestion, msg);
    emit questionSent(*question);
}

bool QuizGate::handleMessage(QLocalSocket *socket, quint32 message, QDataStream &in)
{
    switch (static_cast<EduGreeterMessage>(message)) {
    case EduGreeterMessage::QuizHello:
        onHello(socket);
        return true;
    case EduGreeterMessage::QuizQuery: {
        QuizQueryMsg msg;
        in >> msg;
        if (in.status() == QDataStream::Ok)
            onQuery(socket, msg.user);
        break;
    }
    case EduGreeterMessage::QuizAnswer: {
        QuizAnswerMsg msg;
        in >> msg;
        if (in.status() == QDataStream::Ok)
            onAnswer(socket, msg);
        break;
    }
    case EduGreeterMessage::QuizSwap: {
        QuizSwapMsg msg;
        in >> msg;
        if (in.status() == QDataStream::Ok)
            onSwap(socket, msg);
        break;
    }
    case EduGreeterMessage::QuizAbandon:
        onAbandon(socket);
        return true;
    default:
        return false;
    }

    if (in.status() != QDataStream::Ok)
        qWarning() << "Edu-LM: dropped a truncated quiz message" << message;
    return true;
}

void QuizGate::onHello(QLocalSocket *socket)
{
    if (socket == m_quizSocket)
        return;

    // a new quiz connection replaces the old one, and a quiz running on the old one is over
    if (m_quiz)
        abandon();
    m_quizSocket = socket;
    connect(socket, &QLocalSocket::disconnected, this, &QuizGate::onQuizSocketDisconnected, Qt::UniqueConnection);
    socket->write(encodeMessage(EduDaemonMessage::QuizReady));
}

void QuizGate::onQuery(QLocalSocket *socket, const QString &user)
{
    const GateConfig config = m_config();
    const bool required = decideQuiz(user, m_groups(user), config.policy).quizRequired;
    sendTo(socket, EduDaemonMessage::QuizStatus, statusFor(user, config, required));
}

bool QuizGate::fromQuizSocket(QLocalSocket *socket, const char *what) const
{
    if (m_quiz && socket == m_quizSocket)
        return true;
    qWarning() << "Edu-LM: ignoring" << what << "- no quiz is running on that connection";
    return false;
}

void QuizGate::onAnswer(QLocalSocket *socket, const QuizAnswerMsg &msg)
{
    if (!fromQuizSocket(socket, "QuizAnswer"))
        return;

    const QuizSession::AnswerResult result = m_quiz->answer(msg.questionId, msg.answer);
    if (!result.accepted()) {
        qWarning() << "Edu-LM: ignoring QuizAnswer for stale question" << msg.questionId;
        return;
    }
    ++m_answerAttempts;

    sendTo(m_quizSocket, EduDaemonMessage::QuizFeedback,
           QuizFeedbackMsg{msg.questionId, result.correct, result.attemptsLeft, result.questionFinished});

    switch (m_quiz->state()) {
    case QuizSession::State::Passed:
        finishPassed();
        break;
    case QuizSession::State::Failed:
        finishFailed(true);
        break;
    default:
        if (result.questionFinished)
            sendQuestion();
    }
}

void QuizGate::onSwap(QLocalSocket *socket, const QuizSwapMsg &msg)
{
    if (!fromQuizSocket(socket, "QuizSwap"))
        return;

    const QuizSession::SwapResult result = m_quiz->swap(msg.questionId);
    if (result.accepted()) {
        sendQuestion();
        return;
    }

    const QString reason = refusalReason(result.refusal);
    if (reason.isEmpty())
        qWarning() << "Edu-LM: ignoring QuizSwap for stale question" << msg.questionId;
    else
        sendTo(m_quizSocket, EduDaemonMessage::QuizSwapRefused, QuizSwapRefusedMsg{reason});
}

void QuizGate::onAbandon(QLocalSocket *socket)
{
    if (socket != m_quizSocket) {
        qWarning() << "Edu-LM: ignoring QuizAbandon from a connection that is not the quiz connection";
        return;
    }
    abandon();
}

void QuizGate::finishPassed()
{
    qInfo() << "Edu-LM: quiz passed by" << m_pending.user << "with" << m_quiz->correctCount() << "correct";

    QuizResultMsg result;
    result.passed = true;
    result.correct = m_quiz->correctCount();
    result.required = m_quiz->rules().requiredCorrect;
    result.asked = m_quiz->currentIndex();
    sendTo(m_quizSocket, EduDaemonMessage::QuizResult, result);

    PendingLogin login = std::move(m_pending);
    m_pending = PendingLogin();
    m_hasPending = false;
    m_quiz.reset();

    if (login.socket && login.proceed)
        login.proceed(login.password);
    login.wipe();
}

void QuizGate::finishFailed(bool sendResult)
{
    const QString user = m_pending.user;
    qInfo() << "Edu-LM: quiz failed by" << user << "with" << m_quiz->correctCount() << "correct";
    m_cooldown.recordFailure(user, m_cooldownSeconds);

    if (sendResult) {
        QuizResultMsg result;
        result.passed = false;
        result.correct = m_quiz->correctCount();
        result.required = m_quiz->rules().requiredCorrect;
        result.asked = m_quiz->currentIndex();
        for (const ReviewItem &item : m_quiz->review())
            result.review.append({item.prompt, item.choices, item.yourAnswer, item.correctAnswer, item.explanation});
        result.cooldownSeconds = m_cooldown.secondsLeft(user);
        sendTo(m_quizSocket, EduDaemonMessage::QuizResult, result);
    }

    QLocalSocket *loginSocket = m_pending.socket;
    discard();
    failLogin(loginSocket);
}

// Answering counts as an attempt: leaving afterwards must not dodge the cooldown.
void QuizGate::abandon()
{
    if (m_quiz && m_answerAttempts > 0) {
        qInfo() << "Edu-LM: quiz abandoned by" << m_pending.user << "after answering, counted as failed";
        finishFailed(false);
        return;
    }
    discard();
}

void QuizGate::discard()
{
    m_quiz.reset();
    m_answerAttempts = 0;
    m_hasPending = false;
    m_pending.wipe();
}

void QuizGate::onLoginSocketDisconnected()
{
    if (!m_hasPending || sender() != m_pending.socket)
        return;
    m_pending.socket = nullptr; // nobody left to tell
    abandon();
}

void QuizGate::onQuizSocketDisconnected()
{
    if (sender() != m_quizSocket)
        return;
    m_quizSocket = nullptr;
    if (m_quiz)
        abandon();
}

}
