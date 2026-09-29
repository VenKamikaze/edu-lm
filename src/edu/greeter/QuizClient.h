// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "protocol/EduProtocol.h"

#include <QByteArray>
#include <QElapsedTimer>
#include <QObject>
#include <QStringList>
#include <QTimer>
#include <QVariantList>

#include <functional>

class QLocalSocket;

namespace EduLM {

// The theme's view of the quiz. It talks to the daemon on a second socket (opened by
// enable()) and follows the login result that GreeterProxy reports on the first one.
// The two sockets have no ordering between them, so a login failure is only interpreted
// once the quiz socket has had a chance to explain it (see onLoginFailed()).
class QuizClient : public QObject
{
    Q_OBJECT
    Q_PROPERTY(State state READ state NOTIFY stateChanged)
    Q_PROPERTY(bool waiting READ waiting NOTIFY waitingChanged)
    // slowChecking: the password check has taken a while; cancelChecking() is the way out
    Q_PROPERTY(bool slowChecking READ slowChecking NOTIFY slowCheckingChanged)
    // connecting: a quiz connection is being opened and the daemon has not said QuizReady yet
    Q_PROPERTY(bool connecting READ connecting NOTIFY connectionChanged)
    Q_PROPERTY(bool ready READ ready NOTIFY connectionChanged)

    Q_PROPERTY(QString user READ user NOTIFY userChanged)
    Q_PROPERTY(bool statusKnown READ statusKnown NOTIFY statusChanged)
    Q_PROPERTY(bool quizRequired READ quizRequired NOTIFY statusChanged)
    Q_PROPERTY(bool studentPasswords READ studentPasswords NOTIFY statusChanged)
    Q_PROPERTY(int cooldownSeconds READ cooldownSeconds NOTIFY cooldownSecondsChanged)

    Q_PROPERTY(int questionCount READ questionCount NOTIFY questionChanged)
    Q_PROPERTY(int requiredCorrect READ requiredCorrect NOTIFY questionChanged)
    Q_PROPERTY(QString questionId READ questionId NOTIFY questionChanged)
    Q_PROPERTY(int questionNumber READ questionNumber NOTIFY questionChanged) // 1-based
    Q_PROPERTY(QString prompt READ prompt NOTIFY questionChanged)
    Q_PROPERTY(QString questionType READ questionType NOTIFY questionChanged)
    Q_PROPERTY(QStringList choices READ choices NOTIFY questionChanged)
    Q_PROPERTY(int swapsLeft READ swapsLeft NOTIFY questionChanged)
    Q_PROPERTY(int attemptsLeft READ attemptsLeft NOTIFY questionChanged)
    Q_PROPERTY(bool answerStarted READ answerStarted NOTIFY questionChanged) // leaving now counts as a try
    Q_PROPERTY(QString swapRefusedReason READ swapRefusedReason NOTIFY questionChanged)

    Q_PROPERTY(bool feedbackCorrect READ feedbackCorrect NOTIFY feedbackChanged)
    Q_PROPERTY(bool feedbackFinished READ feedbackFinished NOTIFY feedbackChanged)

    Q_PROPERTY(bool resultPassed READ resultPassed NOTIFY resultChanged)
    Q_PROPERTY(int resultCorrect READ resultCorrect NOTIFY resultChanged)
    Q_PROPERTY(int resultRequired READ resultRequired NOTIFY resultChanged)
    Q_PROPERTY(int resultAsked READ resultAsked NOTIFY resultChanged)
    Q_PROPERTY(QVariantList resultReview READ resultReview NOTIFY resultChanged)

    Q_PROPERTY(Notice notice READ notice NOTIFY messagesChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY messagesChanged)
    Q_PROPERTY(QString infoMessage READ infoMessage NOTIFY messagesChanged)

public:
    enum State {
        Idle,     // nothing running: the login screen (with a banner if errorMessage is set)
        Checking, // a login was sent and the password is being verified
        Question, // a question is on screen
        Feedback, // "Correct!" / "Not quite" is on screen
        Result,   // the quiz is over: passed (logging in) or failed (review)
        Cooldown, // waiting out a cooldown
        LoggedIn, // the login succeeded
    };
    Q_ENUM(State)

    // Why the last login did not work, for the login screen. Never WrongPassword for a
    // failed quiz or a cooldown refusal.
    enum Notice {
        NoNotice,
        WrongPassword,
        LoginFailed, // the session could not start, or the reason is unknown
    };
    Q_ENUM(Notice)

    // Forwards a login to GreeterProxy (or the test-mode mock).
    using LoginSender = std::function<void(const QString &user, const QString &password, int sessionIndex)>;

    QuizClient(const QString &socketName, LoginSender sendLogin, QObject *parent = nullptr);

    State state() const { return m_state; }
    bool waiting() const { return m_inFlight; }
    bool connecting() const { return m_connecting; }
    bool slowChecking() const { return m_slowChecking; }
    bool ready() const { return m_ready; }
    QString user() const { return m_user; }
    bool statusKnown() const { return m_statusKnown; }
    bool quizRequired() const { return m_quizRequired; }
    bool studentPasswords() const { return m_studentPasswords; }
    int cooldownSeconds() const { return m_cooldownSeconds; }

    int questionCount() const { return m_questionCount; }
    int requiredCorrect() const { return m_requiredCorrect; }
    QString questionId() const { return m_question.id; }
    int questionNumber() const { return m_question.index + 1; }
    QString prompt() const { return m_question.prompt; }
    QString questionType() const { return m_question.type; }
    QStringList choices() const { return m_question.choices; }
    int swapsLeft() const { return m_question.swapsLeft; }
    int attemptsLeft() const { return m_question.attemptsLeft; }
    bool answerStarted() const { return m_answerStarted; }
    QString swapRefusedReason() const { return m_swapRefusedReason; }

    bool feedbackCorrect() const { return m_feedback.correct; }
    bool feedbackFinished() const { return m_feedback.questionFinished; }

    bool resultPassed() const { return m_result.passed; }
    int resultCorrect() const { return m_result.correct; }
    int resultRequired() const { return m_result.required; }
    int resultAsked() const { return m_result.asked; }
    QVariantList resultReview() const;

    Notice notice() const { return m_notice; }
    QString errorMessage() const { return m_errorMessage; }
    QString infoMessage() const { return m_infoMessage; }

    // Timings, adjustable so tests need not wait.
    void setFeedbackMs(int ms) { m_feedbackTimer.setInterval(ms); }
    void setSlowCheckingMs(int ms) { m_slowTimer.setInterval(ms); }

public slots:
    // Opens the quiz connection and sends QuizHello. Idempotent; a stock theme never calls it,
    // so quiz users stay refused by the daemon.
    void enable();
    // Selects a user and asks the daemon about them. A cooldown in the answer opens the
    // Cooldown state.
    void query(const QString &user);
    // For everyone, quiz user or not. The password is handed on and not kept.
    void login(const QString &user, const QString &password, int sessionIndex);
    void answer(const QString &text);
    void swap();
    void abandon();
    // Gives up waiting for a login result: a way out if the daemon never answers
    void cancelChecking();
    // Result (failed) -> Cooldown, or Idle when the cooldown is over.
    void continueFromResult();
    // Back to the login screen; clears messages and results.
    void dismiss();

    // Login results from GreeterProxy (or the mock).
    void onLoginSucceeded();
    void onLoginFailed();
    void onInformationMessage(const QString &message);

signals:
    void stateChanged();
    void waitingChanged();
    void connectionChanged();
    void slowCheckingChanged();
    void userChanged();
    void statusChanged();
    void cooldownSecondsChanged();
    void questionChanged();
    void feedbackChanged();
    void resultChanged();
    void messagesChanged();
    void swapRefused(const QString &reason);

private slots:
    void onConnected();
    void onReadyRead();
    void onSocketLost();
    void onFeedbackTimeout();
    void onSlowChecking();
    void onCooldownTick();

private:
    // One decoded daemon message; only the payload of `id` is filled in.
    struct Incoming {
        quint32 id = 0;
        QuizStatusMsg status;
        QuizStartedMsg started;
        QuizQuestionMsg question;
        QuizFeedbackMsg feedback;
        QuizSwapRefusedMsg refused;
        QuizResultMsg result;
        QuizCooldownMsg cooldown;
        QuizErrorMsg error;
        QuizLoginRefusedMsg loginRefused;
    };

    bool socketReady() const;
    void connectToDaemon();
    void send(const QByteArray &bytes);
    void processInput();
    static bool decode(QDataStream &in, Incoming *msg);
    void handle(const Incoming &msg);

    void setState(State state);
    void setInFlight(bool value);
    void setNotice(Notice notice);
    void setError(const QString &message);
    void setInfo(const QString &message);
    void setUser(const QString &user);
    void beginAttempt();
    void applyQuestion(const QuizQuestionMsg &question);
    void applyResult(const QuizResultMsg &result);
    void applyStatus(const QuizStatusMsg &status);
    void startCooldown(int seconds);
    void stopCooldown();
    int remainingCooldown() const;
    void resolveLoginFailure();
    void maybeResolveLoginFailure();
    void clearConfigError();
    void setConnection(bool connecting, bool ready);
    void clearQuiz();

    QString m_socketName;
    LoginSender m_sendLogin;
    QLocalSocket *m_socket;
    bool m_enabled = false;
    bool m_connecting = false;     // connection opened, QuizReady not yet seen
    bool m_ready = false;          // QuizReady seen on this connection
    bool m_lost = false;           // the loss of the connection was reported
    bool m_lostErrorShown = false; // errorMessage is that report
    QByteArray m_inbox;  // received, not yet a whole message
    QByteArray m_outbox; // written before the connection was up

    State m_state = Idle;
    bool m_answerStarted = false; // an answer was sent in this attempt
    bool m_inFlight = false; // an answer or swap is out and unanswered
    QString m_user;

    bool m_statusKnown = false;
    bool m_quizRequired = false;
    bool m_studentPasswords = true;

    int m_questionCount = 0;
    int m_requiredCorrect = 0;
    QuizQuestionMsg m_question;
    QString m_swapRefusedReason;
    QuizFeedbackMsg m_feedback;
    QuizResultMsg m_result;

    // Shown when the Feedback screen is done, if they arrived while it was up.
    bool m_hasPendingQuestion = false;
    QuizQuestionMsg m_pendingQuestion;
    bool m_hasPendingResult = false;
    QuizResultMsg m_pendingResult;

    // What the quiz socket said about the current login, before or after its LoginFailed
    enum class Verdict { None, NotQuiz, WrongPassword, Busy };
    Verdict m_verdict = Verdict::None;
    bool m_verdictExpected = false; // the daemon had this connection ready when the login went out
    bool m_slowChecking = false;
    bool m_loginFailed = false; // LoginFailed seen for the current attempt, not yet explained
    Notice m_notice = NoNotice;
    QString m_errorMessage;
    QString m_infoMessage;

    QTimer m_feedbackTimer;
    QTimer m_slowTimer; // only offers the escape from Checking, never interprets a result
    QTimer m_cooldownTimer;
    QElapsedTimer m_cooldownClock;
    qint64 m_cooldownTotalMs = 0;
    bool m_cooldownActive = false;
    int m_cooldownSeconds = 0;
};

}
