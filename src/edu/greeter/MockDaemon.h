// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "daemon/GateConfig.h"

#include <QLocalSocket>
#include <QObject>
#include <QRandomGenerator>
#include <QStringList>

#include <functional>
#include <memory>

class QLocalServer;

namespace EduLM {

class CooldownTracker;
class PasswordVerifier;
class QuizGate;
struct Question;

struct MockSettings {
    GateConfig gate;
    QStringList exemptUsers; // everybody else needs the quiz
    bool seeded = false;
    quint32 seed = 0;
    int verifyDelayMs = 400;     // how long "checking the password" takes
    int sessionStartMs = 1500;   // from an accepted login to LoginSucceeded
    int loginResultDelayMs = 0;  // added to LoginFailed/LoginSucceeded, to test both arrival orders
    bool logAnswers = true;      // the answer key goes to qDebug so scripts can pass the quiz
};

// Plays the daemon's part in `sddm-greeter --test-mode`: the real QuizGate on a local server, with a
// password check that accepts everything except "wrong". Talk to it through QuizClient.
class MockDaemon : public QObject
{
    Q_OBJECT
public:
    explicit MockDaemon(const MockSettings &settings, QObject *parent = nullptr);
    ~MockDaemon() override;

    bool isListening() const;
    QString serverName() const;

    // Stands in for GreeterProxy::login.
    void login(const QString &user, const QString &password, int sessionIndex);
    // Closes every quiz connection, as a daemon restart would
    void dropConnections();

signals:
    void loginSucceeded();
    void loginFailed();
    void informationMessage(const QString &message);
    // The right answer to the question just sent, in the form QuizClient::answer() takes.
    void answerKnown(const QString &questionId, const QString &answer);

private:
    void deliverLater(std::function<void()> deliver);
    void onNewConnection();
    void readFrom(QLocalSocket *socket);
    void onQuestionSent(const Question &question);

    MockSettings m_settings;
    QRandomGenerator m_rng;
    std::unique_ptr<CooldownTracker> m_cooldown;
    QLocalServer *m_server;
    PasswordVerifier *m_verifier;
    QuizGate *m_gate;
    QLocalSocket m_loginToken; // stands in for the login connection; never connected
};

}
