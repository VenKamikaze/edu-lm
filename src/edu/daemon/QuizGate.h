// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "GateConfig.h"
#include "PasswordVerifier.h"
#include "SocketExtension.h"
#include "core/CooldownTracker.h"
#include "core/QuizSession.h"
#include "protocol/EduProtocol.h"

#include <QObject>
#include <QPointer>
#include <QRandomGenerator>

#include <functional>
#include <memory>

class QLocalSocket;

namespace EduLM {

// Password first, then quiz: takes over the logins of quiz users, verifies the password,
// runs the quiz on the quiz connection and continues the login when it is passed.
class QuizGate : public QObject, public SocketExtension
{
    Q_OBJECT
public:
    using ConfigProvider = std::function<GateConfig()>;
    using GroupLookup = std::function<QStringList(const QString &)>;

    // Everything passed by reference must outlive the gate.
    QuizGate(PasswordVerifier &verifier, ConfigProvider config, GroupLookup groups,
             CooldownTracker &cooldown, QRandomGenerator &rng, QObject *parent = nullptr);
    ~QuizGate() override;

    // Called with the stashed password once the quiz is passed; carries on with the normal login.
    using Continuation = std::function<void(const QString &password)>;

    // True if the gate took over this login.
    bool intercept(QLocalSocket *socket, const QString &user, const QString &password, Continuation proceed);

    bool needsQuiz(const QString &user) const;

    // For a login Display refuses before the gate sees it (the reserved "sddm" user): the greeter
    // still gets its verdict, so it does not wait for one.
    void refuseReservedUser();

    bool handleMessage(QLocalSocket *socket, quint32 message, QDataStream &in) override;

    // Only the greeter's test-mode mock uses this, to show the version mismatch screen.
    void setAnnouncedVersion(quint32 version) { m_announcedVersion = version; }

signals:
    void loginFailed(QLocalSocket *socket);
    void informationMessage(QLocalSocket *socket, const QString &message);
    // Only the greeter's test-mode mock listens, to log the answer key; the daemon does not.
    void questionSent(const EduLM::Question &question);

private slots:
    void onVerified(bool ok);
    void onLoginSocketDisconnected();
    void onQuizSocketDisconnected();

private:
    struct PendingLogin {
        QPointer<QLocalSocket> socket;
        QString user;
        QString password;
        Continuation proceed;

        void wipe();
    };

    bool busy() const { return m_verifying || m_quiz; }
    void reportConfigProblems(QLocalSocket *loginSocket, const QString &user, const QStringList &problems);
    void failLogin(QLocalSocket *loginSocket, const QString &information = QString());
    void startQuiz(const GateConfig &config);

    void onHello(QLocalSocket *socket);
    void onQuery(QLocalSocket *socket, const QString &user);
    void onAnswer(QLocalSocket *socket, const QuizAnswerMsg &msg);
    void onSwap(QLocalSocket *socket, const QuizSwapMsg &msg);
    void onAbandon(QLocalSocket *socket);
    void onNext(QLocalSocket *socket);

    void sendQuestion();
    void advance();
    void finishPassed();
    void recordFailure();
    void finishFailed(bool sendResult);
    void abandon();
    void discard();

    template<typename Payload>
    void sendTo(QLocalSocket *socket, EduDaemonMessage id, const Payload &payload);
    void sendError(const QString &message);
    void refuseLogin(const QString &reason);
    QuizStatusMsg statusFor(const QString &user, const GateConfig &config, bool required);
    bool fromQuizSocket(QLocalSocket *socket, const char *what) const;

    PasswordVerifier &m_verifier;
    ConfigProvider m_config;
    GroupLookup m_groups;
    CooldownTracker &m_cooldown;
    QRandomGenerator &m_rng;

    quint32 m_announcedVersion = EduProtocolVersion;
    QPointer<QLocalSocket> m_quizSocket;
    PendingLogin m_pending;
    bool m_hasPending = false;
    bool m_verifying = false;
    std::unique_ptr<QuizSession> m_quiz;
    int m_answerAttempts = 0;
    bool m_failureRecorded = false; // the running quiz has already started its cooldown
    bool m_waitingNext = false; // a revealing feedback is out; nothing more until QuizNext
    int m_cooldownSeconds = 0; // from the config the running quiz started with
};

}
