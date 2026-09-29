// SPDX-License-Identifier: GPL-2.0-or-later
#include "MockDaemon.h"

#include "core/Answer.h"
#include "core/CooldownTracker.h"
#include "daemon/PasswordVerifier.h"
#include "daemon/QuizGate.h"

#include <QCoreApplication>
#include <QDataStream>
#include <QDebug>
#include <QLocalServer>
#include <QTimer>

#include <cmath>

namespace EduLM {

namespace {

// Accepts every password but the literal "wrong", after a short delay.
class MockVerifier : public PasswordVerifier
{
public:
    MockVerifier(int delayMs, QObject *parent) : PasswordVerifier(parent), m_delayMs(delayMs) {}

    void verify(const QString &, const QString &password) override
    {
        const bool ok = password != QLatin1String("wrong");
        QTimer::singleShot(m_delayMs, this, [this, ok] { emit verified(ok); });
    }

private:
    int m_delayMs;
};

// The text QuizClient::answer() takes for the right answer.
QString answerText(const Question &q)
{
    switch (q.type) {
    case QuestionType::MultipleChoice: return QString::number(q.answerIndex);
    case QuestionType::TrueFalse: return q.answerBool ? QStringLiteral("true") : QStringLiteral("false");
    case QuestionType::Numeric: return QString::number(q.answerNumber, 'g', 15);
    case QuestionType::ShortText: return q.accept.value(0);
    case QuestionType::Generator: break;
    }
    return QString();
}

// What to press or type in the theme for that answer: "key:N" for a choice button, else "text:...".
QString answerKeys(const Question &q)
{
    switch (q.type) {
    case QuestionType::MultipleChoice: return QStringLiteral("key:%1").arg(q.answerIndex + 1);
    case QuestionType::TrueFalse: return QStringLiteral("key:%1").arg(q.answerBool ? 1 : 2);
    default: return QStringLiteral("text:") + answerText(q);
    }
}

}

MockDaemon::MockDaemon(const MockSettings &settings, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
    , m_rng(settings.seeded ? QRandomGenerator(settings.seed) : QRandomGenerator::securelySeeded())
    , m_cooldown(new CooldownTracker)
    , m_server(new QLocalServer(this))
    , m_verifier(new MockVerifier(settings.verifyDelayMs, this))
{
    const QStringList quizGroups = m_settings.gate.policy.groups;
    m_gate = new QuizGate(*m_verifier,
                          [this] { return m_settings.gate; },
                          [this, quizGroups](const QString &user) {
                              return m_settings.exemptUsers.contains(user) ? QStringList() : quizGroups;
                          },
                          *m_cooldown, m_rng, this);

    connect(m_gate, &QuizGate::loginFailed, this, [this](QLocalSocket *socket) {
        if (socket == &m_loginToken)
            deliverLater([this] { emit loginFailed(); });
    });
    connect(m_gate, &QuizGate::informationMessage, this, [this](QLocalSocket *socket, const QString &message) {
        if (socket == &m_loginToken)
            deliverLater([this, message] { emit informationMessage(message); });
    });
    connect(m_gate, &QuizGate::questionSent, this, &MockDaemon::onQuestionSent);

    connect(m_server, &QLocalServer::newConnection, this, &MockDaemon::onNewConnection);
    const QString name = QStringLiteral("edu-lm-mock-%1").arg(QCoreApplication::applicationPid());
    QLocalServer::removeServer(name);
    if (!m_server->listen(name))
        qWarning() << "Edu-LM test mode: cannot listen for the quiz connection:" << m_server->errorString();
}

// The gate and server hold references to members and react to socket closing, so they go first
MockDaemon::~MockDaemon()
{
    delete m_gate;
    delete m_server;
}

bool MockDaemon::isListening() const
{
    return m_server->isListening();
}

QString MockDaemon::serverName() const
{
    return m_server->serverName();
}

void MockDaemon::deliverLater(std::function<void()> deliver)
{
    QTimer::singleShot(m_settings.loginResultDelayMs, this, std::move(deliver));
}

void MockDaemon::login(const QString &user, const QString &password, int)
{
    const auto succeed = [this] {
        QTimer::singleShot(m_settings.sessionStartMs, this, [this] { deliverLater([this] { emit loginSucceeded(); }); });
    };

    if (m_gate->intercept(&m_loginToken, user, password, [succeed](const QString &) { succeed(); }))
        return;

    // not a quiz user: the plain login
    const bool ok = password != QLatin1String("wrong");
    QTimer::singleShot(m_settings.verifyDelayMs, this, [this, ok, succeed] {
        if (ok)
            succeed();
        else
            deliverLater([this] { emit loginFailed(); });
    });
}

void MockDaemon::dropConnections()
{
    const auto sockets = m_server->findChildren<QLocalSocket *>();
    for (QLocalSocket *socket : sockets)
        socket->disconnectFromServer();
}

void MockDaemon::onNewConnection()
{
    while (QLocalSocket *socket = m_server->nextPendingConnection()) {
        connect(socket, &QLocalSocket::readyRead, this, [this, socket] { readFrom(socket); });
        connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
    }
}

void MockDaemon::readFrom(QLocalSocket *socket)
{
    QDataStream in(socket);
    while (socket->bytesAvailable()) {
        quint32 message = 0;
        in >> message;
        if (!m_gate->handleMessage(socket, message, in)) {
            qWarning() << "Edu-LM test mode: unknown quiz message" << message;
            socket->readAll();
        }
    }
}

// Test mode only: the real greeter never sees answers.
void MockDaemon::onQuestionSent(const Question &question)
{
    emit answerKnown(question.id, answerText(question));
    if (m_settings.logAnswers)
        qDebug().noquote() << "EDU_LM_TEST_ANSWER" << question.id << answerKeys(question);
}

}
