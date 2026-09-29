// SPDX-License-Identifier: GPL-2.0-or-later
#include "QuizClient.h"

#include <QDataStream>
#include <QLocalSocket>
#include <QVariantMap>

namespace EduLM {

namespace {

const int DefaultFeedbackMs = 1000;
const int DefaultSlowCheckingMs = 10000;
const int MaxInboxBytes = 1024 * 1024;
const int CooldownTickMs = 200;

}

QuizClient::QuizClient(const QString &socketName, LoginSender sendLogin, QObject *parent)
    : QObject(parent)
    , m_socketName(socketName)
    , m_sendLogin(std::move(sendLogin))
    , m_socket(new QLocalSocket(this))
{
    m_feedbackTimer.setSingleShot(true);
    m_feedbackTimer.setInterval(DefaultFeedbackMs);
    m_cooldownTimer.setInterval(CooldownTickMs);
    m_slowTimer.setSingleShot(true);
    m_slowTimer.setInterval(DefaultSlowCheckingMs);
    connect(&m_slowTimer, &QTimer::timeout, this, &QuizClient::onSlowChecking);

    connect(&m_feedbackTimer, &QTimer::timeout, this, &QuizClient::onFeedbackTimeout);
    connect(&m_cooldownTimer, &QTimer::timeout, this, &QuizClient::onCooldownTick);

    connect(m_socket, &QLocalSocket::connected, this, &QuizClient::onConnected);
    connect(m_socket, &QLocalSocket::readyRead, this, &QuizClient::onReadyRead);
    connect(m_socket, &QLocalSocket::disconnected, this, &QuizClient::onSocketLost);
    connect(m_socket, &QLocalSocket::errorOccurred, this, &QuizClient::onSocketLost);
}

QVariantList QuizClient::resultReview() const
{
    QVariantList list;
    for (const QuizReviewItem &item : m_result.review) {
        QVariantMap map;
        map.insert(QStringLiteral("prompt"), item.prompt);
        map.insert(QStringLiteral("choices"), item.choices);
        map.insert(QStringLiteral("givenAnswer"), item.givenAnswer);
        map.insert(QStringLiteral("correctAnswer"), item.correctAnswer);
        map.insert(QStringLiteral("explanation"), item.explanation);
        list.append(map);
    }
    return list;
}

// ---- connection ----

bool QuizClient::socketReady() const
{
    return m_socket->state() == QLocalSocket::ConnectedState;
}

void QuizClient::connectToDaemon()
{
    if (m_socket->state() != QLocalSocket::UnconnectedState)
        return;
    m_lost = false;
    // connected() can fire inside connectToServer()
    setConnection(true, false);
    m_socket->connectToServer(m_socketName);
}

void QuizClient::enable()
{
    m_enabled = true;
    connectToDaemon();
}

void QuizClient::onConnected()
{
    m_lost = false;
    if (m_lostErrorShown) {
        m_lostErrorShown = false;
        setError(QString());
    }
    m_socket->write(encodeMessage(EduGreeterMessage::QuizHello));
    if (!m_outbox.isEmpty()) {
        m_socket->write(m_outbox);
        m_outbox.clear();
    }
}

void QuizClient::send(const QByteArray &bytes)
{
    if (socketReady()) {
        m_socket->write(bytes);
        return;
    }
    // written on the connection that is being opened; dropped if it never comes up. Appended
    // first because connectToDaemon() may flush the outbox at once.
    m_outbox += bytes;
    connectToDaemon();
}

void QuizClient::setConnection(bool connecting, bool ready)
{
    if (m_connecting == connecting && m_ready == ready)
        return;
    m_connecting = connecting;
    m_ready = ready;
    emit connectionChanged();
}

void QuizClient::onSocketLost()
{
    m_outbox.clear();
    m_inbox.clear();
    setConnection(false, false);
    if (!m_enabled || m_lost)
        return;
    m_lost = true;
    m_lostErrorShown = true;
    setError(tr("Lost the connection to the quiz. Please ask a parent for help."));

    if (m_state == Checking || m_state == Question || m_state == Feedback) {
        m_feedbackTimer.stop();
        m_loginFailed = false;
        clearQuiz();
        setState(Idle);
    }
}

void QuizClient::onReadyRead()
{
    m_inbox += m_socket->readAll();
    if (m_inbox.size() > MaxInboxBytes) {
        qWarning() << "Edu-LM: the quiz connection sent too much, closing it";
        m_socket->abort();
        return;
    }
    processInput();
}

// A message can arrive in pieces and several can arrive at once.
void QuizClient::processInput()
{
    while (!m_inbox.isEmpty()) {
        Incoming msg;
        bool known = false;
        bool complete = false;
        bool corrupt = false;
        qint64 consumed = 0;
        {
            QDataStream in(&m_inbox, QIODevice::ReadOnly);
            in.startTransaction();
            known = decode(in, &msg);
            const QDataStream::Status status = in.status();
            complete = in.commitTransaction();
            corrupt = !complete && status != QDataStream::ReadPastEnd;
            consumed = in.device()->pos();
        }
        if (!complete && !corrupt)
            return; // wait for the rest
        if (corrupt || !known) {
            qWarning() << "Edu-LM: unreadable message from the quiz connection, closing it";
            m_socket->abort(); // drops the buffers and reports the loss
            return;
        }
        m_inbox.remove(0, consumed);
        handle(msg);
    }
}

bool QuizClient::decode(QDataStream &in, Incoming *msg)
{
    in >> msg->id;
    if (in.status() != QDataStream::Ok)
        return false;

    switch (static_cast<EduDaemonMessage>(msg->id)) {
    case EduDaemonMessage::QuizStatus: in >> msg->status; break;
    case EduDaemonMessage::QuizStarted: in >> msg->started; break;
    case EduDaemonMessage::QuizQuestion: in >> msg->question; break;
    case EduDaemonMessage::QuizFeedback: in >> msg->feedback; break;
    case EduDaemonMessage::QuizSwapRefused: in >> msg->refused; break;
    case EduDaemonMessage::QuizResult: in >> msg->result; break;
    case EduDaemonMessage::QuizCooldown: in >> msg->cooldown; break;
    case EduDaemonMessage::QuizError: in >> msg->error; break;
    case EduDaemonMessage::QuizReady: break;
    case EduDaemonMessage::QuizLoginRefused: in >> msg->loginRefused; break;
    default: return false;
    }
    return true;
}

// ---- daemon messages ----

void QuizClient::handle(const Incoming &msg)
{
    switch (static_cast<EduDaemonMessage>(msg.id)) {
    case EduDaemonMessage::QuizReady:
        setConnection(false, true);
        break;

    case EduDaemonMessage::QuizStatus:
        applyStatus(msg.status);
        // a login that needs no quiz gets its verdict this way
        if (m_state == Checking && msg.status.user == m_user && !msg.status.required) {
            m_verdict = Verdict::NotQuiz;
            maybeResolveLoginFailure();
        }
        break;

    case EduDaemonMessage::QuizLoginRefused:
        if (m_state == Checking) {
            m_verdict = msg.loginRefused.reason == QLatin1String("wrong-password") ? Verdict::WrongPassword : Verdict::Busy;
            maybeResolveLoginFailure();
        }
        break;

    case EduDaemonMessage::QuizStarted:
        if (m_state != Checking) {
            // a quiz for a login that was given up on: end it rather than show it
            send(encodeMessage(EduGreeterMessage::QuizAbandon));
        } else {
            m_questionCount = msg.started.count;
            m_requiredCorrect = msg.started.required;
            emit questionChanged();
        }
        break;

    case EduDaemonMessage::QuizQuestion:
        if (m_state == Feedback && m_feedbackTimer.isActive()) {
            m_pendingQuestion = msg.question;
            m_hasPendingQuestion = true;
        } else if (m_state == Checking || m_state == Question || m_state == Feedback) {
            applyQuestion(msg.question);
        }
        break;

    case EduDaemonMessage::QuizFeedback:
        if (m_state != Question)
            break;
        m_feedback = msg.feedback;
        m_question.attemptsLeft = msg.feedback.attemptsLeft;
        setInFlight(false);
        emit feedbackChanged();
        emit questionChanged();
        setState(Feedback);
        m_feedbackTimer.start();
        break;

    case EduDaemonMessage::QuizSwapRefused:
        if (m_state != Question)
            break;
        setInFlight(false);
        m_swapRefusedReason = msg.refused.reason;
        emit questionChanged();
        emit swapRefused(msg.refused.reason);
        break;

    case EduDaemonMessage::QuizResult:
        if (m_state == Feedback && m_feedbackTimer.isActive()) {
            m_pendingResult = msg.result;
            m_hasPendingResult = true;
        } else if (m_state == Checking || m_state == Question || m_state == Feedback) {
            applyResult(msg.result);
        }
        break;

    case EduDaemonMessage::QuizCooldown:
        startCooldown(msg.cooldown.seconds);
        if (m_state == Checking || m_state == Idle) {
            // this is what the LoginFailed that goes with it means
            m_loginFailed = false;
            setNotice(NoNotice);
            setState(Cooldown);
        }
        break;

    case EduDaemonMessage::QuizError:
        m_feedbackTimer.stop();
        m_loginFailed = false;
        clearQuiz();
        setError(msg.error.message);
        setState(Idle);
        break;

    default:
        break;
    }
}

void QuizClient::applyStatus(const QuizStatusMsg &status)
{
    if (status.user != m_user)
        return; // the answer to a question the theme has moved on from

    m_statusKnown = true;
    m_quizRequired = status.required;
    m_studentPasswords = status.studentPasswords;
    m_questionCount = status.questionCount;
    m_requiredCorrect = status.requiredCorrect;
    emit statusChanged();
    emit questionChanged();

    if (m_state != Idle)
        return;
    if (status.cooldownSeconds > 0) {
        startCooldown(status.cooldownSeconds);
        setState(Cooldown);
    } else {
        stopCooldown();
    }
}

void QuizClient::applyQuestion(const QuizQuestionMsg &question)
{
    m_hasPendingQuestion = false;
    m_loginFailed = false; // a running quiz means the login did not fail
    m_question = question;
    m_swapRefusedReason.clear();
    setInFlight(false);
    emit questionChanged();
    setState(Question);
}

void QuizClient::applyResult(const QuizResultMsg &result)
{
    m_hasPendingResult = false;
    m_result = result;
    setInFlight(false);
    emit resultChanged();

    if (result.passed) {
        // the daemon's LoginFailed after a pass means the session could not start
        if (m_loginFailed)
            resolveLoginFailure();
        else
            setState(Result);
        return;
    }

    m_loginFailed = false; // explained by this result
    startCooldown(result.cooldownSeconds);
    setState(Result);
}

// ---- login results ----

void QuizClient::onLoginSucceeded()
{
    m_feedbackTimer.stop();
    m_loginFailed = false;
    m_verdict = Verdict::None;
    setInFlight(false);
    setNotice(NoNotice);
    setState(LoggedIn);
}

// LoginFailed arrives on the login socket, the daemon's verdict on the quiz socket, in either
// order. Only a wrong password is a bare LoginFailed, and even that has a verdict, so a failure is
// interpreted once the verdict is in (or straight away if there is no quiz connection to wait for).
// While a quiz is running the failure is held until the QuizResult or the loss of the connection.
void QuizClient::onLoginFailed()
{
    switch (m_state) {
    case Idle:
    case Cooldown:
    case LoggedIn:
        return; // already explained, or nothing was being attempted
    case Result:
        if (m_result.passed)
            resolveLoginFailure(); // the quiz was passed but the session did not start
        return;
    case Checking:
        m_loginFailed = true;
        // no verdict comes if the quiz connection was not ready when the login went out
        if (!m_verdictExpected)
            resolveLoginFailure();
        else
            maybeResolveLoginFailure();
        return;
    case Question:
    case Feedback:
        m_loginFailed = true;
        return;
    }
}

void QuizClient::maybeResolveLoginFailure()
{
    if (m_state == Checking && m_loginFailed && m_verdict != Verdict::None)
        resolveLoginFailure();
}

void QuizClient::resolveLoginFailure()
{
    const bool wasChecking = m_state == Checking;
    const Verdict verdict = m_verdict;
    m_loginFailed = false;
    m_verdict = Verdict::None;
    m_feedbackTimer.stop();
    clearQuiz();

    // With no ready quiz connection the daemon refuses quiz users before checking anything
    const bool quizDown = m_enabled && !m_verdictExpected && !(m_statusKnown && !m_quizRequired);
    const bool wrongPassword = verdict == Verdict::WrongPassword || verdict == Verdict::NotQuiz
                               || (verdict == Verdict::None && !quizDown);
    setNotice(wasChecking && wrongPassword ? WrongPassword : LoginFailed);
    // the daemon's "needs the Edu-LM login screen" is wrong here: this is that screen
    if (wasChecking && quizDown) {
        setInfo(tr("The quiz isn't ready yet. Please try again in a moment."));
        if (m_enabled)
            connectToDaemon(); // safe now: this login is over
    }
    setState(Idle);
}

void QuizClient::onInformationMessage(const QString &message)
{
    setInfo(message);
}

void QuizClient::onFeedbackTimeout()
{
    if (m_hasPendingResult) {
        applyResult(m_pendingResult);
    } else if (m_hasPendingQuestion) {
        applyQuestion(m_pendingQuestion);
    } else if (m_feedback.questionFinished) {
        // the next question or the result follows, and is shown when it arrives
    } else {
        setState(Question); // same question, another try
    }
}

// ---- theme actions ----

void QuizClient::login(const QString &user, const QString &password, int sessionIndex)
{
    // never held back and replayed later: the password goes out now or not at all
    if ((m_state != Idle && m_state != Cooldown) || m_connecting)
        return;

    stopCooldown();
    setUser(user);
    m_result = QuizResultMsg();
    emit resultChanged();
    setNotice(NoNotice);
    setInfo(QString());
    clearConfigError();
    m_loginFailed = false;
    m_verdict = Verdict::None;
    m_answerStarted = false;
    emit questionChanged();
    // sent as things stand, with no reconnect: a reconnect could make QuizReady overtake the LoginFailed
    m_verdictExpected = m_enabled && m_ready;
    setState(Checking);
    m_sendLogin(user, password, sessionIndex);
}

void QuizClient::query(const QString &user)
{
    if (user.isEmpty() || (m_state != Idle && m_state != Cooldown))
        return;
    if (m_state == Cooldown) {
        stopCooldown();
        setState(Idle);
    }

    setUser(user);
    setNotice(NoNotice);
    setInfo(QString());
    clearConfigError();
    if (m_enabled)
        send(encodeMessage(EduGreeterMessage::QuizQuery, QuizQueryMsg{user}));
}

void QuizClient::answer(const QString &text)
{
    if (m_state != Question || m_inFlight)
        return;
    setInFlight(true);
    m_answerStarted = true;
    m_swapRefusedReason.clear();
    emit questionChanged();
    send(encodeMessage(EduGreeterMessage::QuizAnswer, QuizAnswerMsg{m_question.id, text}));
}

void QuizClient::swap()
{
    if (m_state != Question || m_inFlight)
        return;
    setInFlight(true);
    m_swapRefusedReason.clear();
    emit questionChanged();
    send(encodeMessage(EduGreeterMessage::QuizSwap, QuizSwapMsg{m_question.id}));
}

// The daemon counts leaving after an answer as a failure but sends no result for it, so ask
// again: the status carries the cooldown.
void QuizClient::abandon()
{
    if (m_state != Question && m_state != Feedback)
        return;
    send(encodeMessage(EduGreeterMessage::QuizAbandon));
    m_feedbackTimer.stop();
    m_loginFailed = false;
    clearQuiz();
    setState(Idle);
    query(m_user);
}

void QuizClient::continueFromResult()
{
    if (m_state != Result || m_result.passed)
        return;
    setState(remainingCooldown() > 0 ? Cooldown : Idle);
}

void QuizClient::dismiss()
{
    if (m_state != Idle && m_state != Result && m_state != Cooldown)
        return;
    stopCooldown();
    m_loginFailed = false;
    m_result = QuizResultMsg();
    emit resultChanged();
    m_lostErrorShown = false;
    setNotice(NoNotice);
    setError(QString());
    setInfo(QString());
    setState(Idle);
    // give a lost connection another go
    if (m_enabled)
        connectToDaemon();
}

// ---- state ----

void QuizClient::onSlowChecking()
{
    if (m_state != Checking)
        return;
    m_slowChecking = true;
    emit slowCheckingChanged();
}

// The daemon may have a login pending (verifying or in a quiz); abandoning discards it.
void QuizClient::cancelChecking()
{
    if (m_state != Checking)
        return;
    if (m_verdictExpected)
        send(encodeMessage(EduGreeterMessage::QuizAbandon));
    m_loginFailed = false;
    m_verdict = Verdict::None;
    setState(Idle);
}

void QuizClient::setState(State state)
{
    if (m_state == state)
        return;
    m_state = state;
    if (state == Checking) {
        m_slowTimer.start();
    } else {
        m_slowTimer.stop();
        if (m_slowChecking) {
            m_slowChecking = false;
            emit slowCheckingChanged();
        }
    }
    emit stateChanged();
}

void QuizClient::setInFlight(bool value)
{
    if (m_inFlight == value)
        return;
    m_inFlight = value;
    emit waitingChanged();
}

void QuizClient::setNotice(Notice notice)
{
    if (m_notice == notice)
        return;
    m_notice = notice;
    emit messagesChanged();
}

void QuizClient::setError(const QString &message)
{
    if (m_errorMessage == message)
        return;
    m_errorMessage = message;
    emit messagesChanged();
}

void QuizClient::setInfo(const QString &message)
{
    if (m_infoMessage == message)
        return;
    m_infoMessage = message;
    emit messagesChanged();
}

void QuizClient::setUser(const QString &user)
{
    if (m_user == user)
        return;
    m_user = user;
    m_statusKnown = false;
    m_quizRequired = false;
    m_studentPasswords = true;
    emit userChanged();
    emit statusChanged();
}

// A configuration error belongs to the last login; the lost-connection one stays until reconnected.
void QuizClient::clearConfigError()
{
    if (!m_lostErrorShown)
        setError(QString());
}

void QuizClient::clearQuiz()
{
    m_hasPendingQuestion = false;
    m_hasPendingResult = false;
    m_question = QuizQuestionMsg();
    m_swapRefusedReason.clear();
    setInFlight(false);
    emit questionChanged();
}

// ---- cooldown ----

int QuizClient::remainingCooldown() const
{
    if (!m_cooldownActive)
        return 0;
    const qint64 left = m_cooldownTotalMs - m_cooldownClock.elapsed();
    return left <= 0 ? 0 : static_cast<int>((left + 999) / 1000);
}

void QuizClient::startCooldown(int seconds)
{
    if (seconds <= 0) {
        stopCooldown();
        return;
    }
    m_cooldownTotalMs = qint64(seconds) * 1000;
    m_cooldownClock.start();
    m_cooldownActive = true;
    m_cooldownTimer.start();
    if (m_cooldownSeconds != seconds) {
        m_cooldownSeconds = seconds;
        emit cooldownSecondsChanged();
    }
}

void QuizClient::stopCooldown()
{
    m_cooldownTimer.stop();
    m_cooldownActive = false;
    if (m_cooldownSeconds != 0) {
        m_cooldownSeconds = 0;
        emit cooldownSecondsChanged();
    }
}

void QuizClient::onCooldownTick()
{
    const int left = remainingCooldown();
    if (left != m_cooldownSeconds) {
        m_cooldownSeconds = left;
        emit cooldownSecondsChanged();
    }
    if (left > 0)
        return;
    m_cooldownTimer.stop();
    m_cooldownActive = false;
    if (m_state == Cooldown)
        setState(Idle);
}

}
