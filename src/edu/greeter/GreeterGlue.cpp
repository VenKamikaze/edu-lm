// SPDX-License-Identifier: GPL-2.0-or-later
#include "GreeterGlue.h"

#include "EduConfiguration.h"
#include "GreeterProxy.h"
#include "MockDaemon.h"
#include "QuizClient.h"

#include <QDebug>
#include <QFileInfo>
#include <QQmlEngine>

namespace EduLM {

namespace {

// Test mode (sddm-greeter --test-mode) settings, all optional:
//   EDU_LM_TEST_CONF           edu-lm.conf-format file for the rules and content (default: N=5, K=4,
//                              R=3, 20 s cooldown, one attempt per question, passwords on)
//   EDU_LM_TEST_QUESTIONS      question pack directory or file (default: the source tree's data/edu/questions)
//   EDU_LM_TEST_EXEMPT         comma-separated users who skip the quiz (default: nobody)
//   EDU_LM_TEST_SEED           integer seed, for the same questions in the same order every run
//   EDU_LM_TEST_VERIFY_MS      how long the password check takes (default 400)
//   EDU_LM_TEST_SESSION_MS     time from an accepted login to "logged in" (default 1500)
//   EDU_LM_TEST_LOGIN_DELAY_MS delay before the login result is reported (default 0); a large value
//                              makes LoginFailed arrive after the quiz messages, 0 usually before
//   EDU_LM_TEST_PROTOCOL_VERSION  the version the mock daemon announces (default: the real one); any
//                              other number shows the "quiz was updated" screen
// Any password is accepted except "wrong". The answer to each question is logged as
// "EDU_LM_TEST_ANSWER <id> key:<n>|text:<answer>".
MockSettings mockSettingsFromEnvironment()
{
    MockSettings settings;

    const QString confFile = qEnvironmentVariable("EDU_LM_TEST_CONF");
    if (!confFile.isEmpty()) {
        setConfigFileOverride(confFile);
        const EduConfig config;
        settings.gate.policy = quizPolicyConfig(config);
        settings.gate.rules = quizRules(config);
        settings.gate.studentPasswords = config.Quiz.StudentPasswords.get();
        settings.gate.cooldownSeconds = config.Quiz.FailCooldownSeconds.get();
        settings.gate.subjects = config.Content.Subjects.get();
        setConfigFileOverride(QString());
    } else {
        settings.gate.policy.groups = {QStringLiteral("edu-students")};
        settings.gate.cooldownSeconds = 20;
        settings.gate.subjects = {QStringLiteral("maths"), QStringLiteral("science"),
                                  QStringLiteral("hass"), QStringLiteral("english")};
    }

    settings.gate.questionDirs = {qEnvironmentVariable("EDU_LM_TEST_QUESTIONS", QStringLiteral(EDU_LM_SAMPLE_QUESTIONS_DIR))};
    if (!QFileInfo::exists(settings.gate.questionDirs.first()))
        qCritical().noquote() << "Edu-LM test mode: the question packs" << settings.gate.questionDirs.first()
                              << "do not exist (the default is inside the source tree); set EDU_LM_TEST_QUESTIONS";

    const QString exempt = qEnvironmentVariable("EDU_LM_TEST_EXEMPT");
    for (const QString &user : exempt.split(QLatin1Char(','), Qt::SkipEmptyParts))
        settings.exemptUsers << user.trimmed();

    bool ok = false;
    const quint32 seed = qEnvironmentVariable("EDU_LM_TEST_SEED").toUInt(&ok);
    settings.seeded = ok;
    settings.seed = seed;

    settings.verifyDelayMs = qEnvironmentVariableIntValue("EDU_LM_TEST_VERIFY_MS", &ok);
    if (!ok)
        settings.verifyDelayMs = 400;
    settings.sessionStartMs = qEnvironmentVariableIntValue("EDU_LM_TEST_SESSION_MS", &ok);
    if (!ok)
        settings.sessionStartMs = 1500;
    settings.loginResultDelayMs = qEnvironmentVariableIntValue("EDU_LM_TEST_LOGIN_DELAY_MS", &ok);
    if (!ok)
        settings.loginResultDelayMs = 0;
    const quint32 version = qEnvironmentVariable("EDU_LM_TEST_PROTOCOL_VERSION").toUInt(&ok);
    if (ok)
        settings.protocolVersion = version;
    return settings;
}

}

QObject *createQuizClient(SDDM::GreeterProxy *proxy, const QString &socketName, bool testMode, QObject *parent)
{
    // lets themes write QuizClient.Checking instead of numbers
    qmlRegisterUncreatableType<QuizClient>("EduLM", 1, 0, "QuizClient", QStringLiteral("use eduQuiz"));

    if (testMode) {
        auto *mock = new MockDaemon(mockSettingsFromEnvironment(), parent);
        auto *client = new QuizClient(mock->serverName(), [mock](const QString &user, const QString &password, int session) {
            mock->login(user, password, session);
        }, parent);
        QObject::connect(mock, &MockDaemon::loginSucceeded, client, &QuizClient::onLoginSucceeded);
        QObject::connect(mock, &MockDaemon::loginFailed, client, &QuizClient::onLoginFailed);
        QObject::connect(mock, &MockDaemon::informationMessage, client, &QuizClient::onInformationMessage);
        return client;
    }

    auto *client = new QuizClient(socketName, [proxy](const QString &user, const QString &password, int session) {
        proxy->login(user, password, session);
    }, parent);
    QObject::connect(proxy, &SDDM::GreeterProxy::loginSucceeded, client, &QuizClient::onLoginSucceeded);
    QObject::connect(proxy, &SDDM::GreeterProxy::loginFailed, client, &QuizClient::onLoginFailed);
    QObject::connect(proxy, &SDDM::GreeterProxy::informationMessage, client, &QuizClient::onInformationMessage);
    return client;
}

}
