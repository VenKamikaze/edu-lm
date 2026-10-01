// SPDX-License-Identifier: GPL-2.0-or-later
#include "greeter/MockDaemon.h"
#include "greeter/QuizClient.h"

#include <QLocalServer>
#include <QLocalSocket>
#include <QtTest>

using namespace EduLM;

namespace {

quint32 id(EduGreeterMessage m) { return quint32(m); }

// Plays the daemon on a local socket and records what the client sends.
class FakeDaemon : public QObject
{
public:
    struct Got {
        quint32 id = 0;
        QString text;   // the user of a query, or the question id
        QString answer;
    };

    explicit FakeDaemon(const QString &name)
    {
        QLocalServer::removeServer(name);
        listening = server.listen(name);
        connect(&server, &QLocalServer::newConnection, this, [this] {
            ++connections;
            conn = server.nextPendingConnection();
            buffer.clear();
            connect(conn, &QLocalSocket::readyRead, this, &FakeDaemon::readClient);
        });
    }

    bool connected() const { return conn && conn->state() == QLocalSocket::ConnectedState; }

    int count(EduGreeterMessage m) const
    {
        int n = 0;
        for (const Got &g : got)
            n += g.id == id(m);
        return n;
    }

    // Whole message in one write
    void send(const QByteArray &bytes)
    {
        conn->write(bytes);
        conn->flush();
    }

    template<typename Payload>
    void send(EduDaemonMessage m, const Payload &payload) { send(encodeMessage(m, payload)); }

    // The real daemon's reply to QuizHello: QuizVersion (unless sendVersion is off, as an old daemon)
    // then QuizReady
    void sendReady()
    {
        if (sendVersion)
            send(EduDaemonMessage::QuizVersion, QuizVersionMsg{version});
        send(encodeMessage(EduDaemonMessage::QuizReady));
    }

    bool autoReady = true; // answer QuizHello with sendReady()
    bool sendVersion = true;
    quint32 version = EduProtocolVersion;
    bool listening = false;
    int connections = 0;
    QLocalSocket *conn = nullptr;
    QList<Got> got;

private:
    void readClient()
    {
        buffer += conn->readAll();
        QDataStream in(&buffer, QIODevice::ReadOnly);
        while (!in.atEnd()) {
            Got g;
            in >> g.id;
            switch (static_cast<EduGreeterMessage>(g.id)) {
            case EduGreeterMessage::QuizQuery: { QuizQueryMsg m; in >> m; g.text = m.user; break; }
            case EduGreeterMessage::QuizAnswer: { QuizAnswerMsg m; in >> m; g.text = m.questionId; g.answer = m.answer; break; }
            case EduGreeterMessage::QuizSwap: { QuizSwapMsg m; in >> m; g.text = m.questionId; break; }
            default: break;
            }
            got.append(g);
            if (g.id == id(EduGreeterMessage::QuizHello) && autoReady) {
                if (sendVersion)
                    conn->write(encodeMessage(EduDaemonMessage::QuizVersion, QuizVersionMsg{version}));
                conn->write(encodeMessage(EduDaemonMessage::QuizReady));
            }
        }
        buffer.clear();
    }

    QLocalServer server;
    QByteArray buffer;
};

QuizQuestionMsg question(const QString &qid, int index, int swaps = 2, int attempts = 1)
{
    QuizQuestionMsg q;
    q.id = qid;
    q.index = index;
    q.count = 3;
    q.prompt = QStringLiteral("What is %1 + 1?").arg(index);
    q.type = QStringLiteral("multiple_choice");
    q.choices = {QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c"), QStringLiteral("d")};
    q.swapsLeft = swaps;
    q.attemptsLeft = attempts;
    return q;
}

QuizFeedbackMsg feedback(const QString &qid, bool correct, int attemptsLeft, bool finished,
                         const QString &answer = QString(), const QString &explanation = QString())
{
    QuizFeedbackMsg f;
    f.id = qid;
    f.correct = correct;
    f.attemptsLeft = attemptsLeft;
    f.questionFinished = finished;
    f.revealed = !answer.isEmpty();
    f.correctAnswer = answer;
    f.explanation = explanation;
    return f;
}

QuizResultMsg failedResult()
{
    QuizResultMsg r;
    r.passed = false;
    r.correct = 1;
    r.required = 2;
    r.asked = 3;
    r.cooldownSeconds = 30;
    for (int i = 0; i < 2; ++i)
        r.review.append({QStringLiteral("Prompt %1 %2").arg(i).arg(QString(200, QLatin1Char('p'))),
                         {QStringLiteral("x"), QStringLiteral("y")}, QStringLiteral("x"), QStringLiteral("y"),
                         QStringLiteral("Because.")});
    return r;
}

QuizResultMsg passedResult()
{
    QuizResultMsg r;
    r.passed = true;
    r.correct = 3;
    r.required = 2;
    r.asked = 3;
    return r;
}

QuizStatusMsg status(const QString &user, bool required, int cooldown = 0, bool passwords = true)
{
    QuizStatusMsg s;
    s.user = user;
    s.required = required;
    s.studentPasswords = passwords;
    s.cooldownSeconds = cooldown;
    return s;
}

}

class QuizClientTest : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void noHelloBeforeEnable();
    void helloExactlyOnce();
    void statusParsing();
    void fullPass();
    void tryAgainFeedback();
    void failedQuiz_data();
    void failedQuiz();
    void cooldownRefusal_data();
    void cooldownRefusal();
    void loginVerdicts_data();
    void loginVerdicts();
    void loginFailedWithoutVerdictWaits();
    void loginFailedWithNoQuizConnection();
    void readyAndConnecting();
    void loginIsNeverQueued();
    void queryAfterDropReachesDaemon();
    void dismissReconnects();
    void oversizedInputClosesConnection();
    void answerStartedNotifies();
    void statusCarriesQuestionCount();
    void errorDoesNotBlockNextUser();
    void passedButSessionFails_data();
    void passedButSessionFails();
    void messageSplitAcrossWrites();
    void twoMessagesInOneWrite();
    void abandonAfterAnswerQueriesStatus();
    void quizSocketLost();
    void unreadableMessageClosesConnection();
    void staleInputIgnored();
    void swapRefusedAndSwapped();
    void quizErrorReturnsToIdle();

    void mockPass();
    void mockFail_data();
    void mockFail();
    void mockWrongPasswordAndExempt();
    void mockDeletedMidQuiz();
    void mockLoginWhileConnectionDown();
    void readyBetweenLoginAndLoginFailed();
    void cancelCheckingAbandonsAndIgnoresLateVerdicts();
    void quizDownInfoIsCalm();
    void revealedFeedbackWaitsForNext();
    void revealedLastQuestionThenResult();
    void plainFeedbackDoesNotWait();

    void matchingVersionIsReady();
    void mismatch_data();
    void mismatch();
    void mismatchDoesNotReconnect();
    void mismatchSurvivesDismissAndFailedLogin();
    void versionBeforeReadyIsRequired();
    void mockVersionMismatch();
    void queryWaitsForVersionAndReady();
    void queuedQueryDroppedOnMismatch();
    void versionStateResetsOnReconnect();

private:
    void connectAndLogin(bool quizUser = true);
    void reachQuestion();
    void answerAndPass(int index);
    void waitState(QuizClient::State state) { QTRY_COMPARE_WITH_TIMEOUT(client->state(), state, 2000); }

    FakeDaemon *daemon = nullptr;
    QuizClient *client = nullptr;
    QList<QuizClient::Notice> notices;
    QList<QStringList> logins;
    QString socketName;
};

void QuizClientTest::init()
{
    static int counter = 0;
    socketName = QStringLiteral("edu-lm-test-%1-%2").arg(QCoreApplication::applicationPid()).arg(++counter);
    daemon = new FakeDaemon(socketName);
    QVERIFY(daemon->listening);
    logins.clear();
    notices.clear();
    client = new QuizClient(socketName, [this](const QString &user, const QString &password, int session) {
        logins.append({user, password, QString::number(session)});
    });
    client->setFeedbackMs(300);
    connect(client, &QuizClient::messagesChanged, this, [this] { notices.append(client->notice()); });
}

void QuizClientTest::cleanup()
{
    delete client;
    delete daemon;
}

void QuizClientTest::connectAndLogin(bool quizUser)
{
    client->enable();
    QTRY_VERIFY(daemon->connected());
    QTRY_COMPARE(daemon->count(EduGreeterMessage::QuizHello), 1);
    QTRY_VERIFY(client->ready());
    client->query(QStringLiteral("bob"));
    QTRY_COMPARE(daemon->count(EduGreeterMessage::QuizQuery), 1);
    daemon->send(EduDaemonMessage::QuizStatus, status(QStringLiteral("bob"), quizUser));
    QTRY_VERIFY(client->statusKnown());
    client->login(QStringLiteral("bob"), QStringLiteral("pw"), 2);
    QCOMPARE(client->state(), QuizClient::Checking);
}

void QuizClientTest::reachQuestion()
{
    connectAndLogin();
    daemon->send(EduDaemonMessage::QuizStarted, QuizStartedMsg{QStringLiteral("bob"), 3, 2, 2});
    daemon->send(EduDaemonMessage::QuizQuestion, question(QStringLiteral("q0"), 0));
    waitState(QuizClient::Question);
}

void QuizClientTest::noHelloBeforeEnable()
{
    client->query(QStringLiteral("bob"));
    client->login(QStringLiteral("bob"), QStringLiteral("pw"), 0);
    QTest::qWait(150);
    QCOMPARE(daemon->connections, 0);
    QCOMPARE(logins.size(), 1); // the login still goes out, on the normal path
    client->enable();
    QTRY_COMPARE(daemon->connections, 1);
    QTRY_COMPARE(daemon->count(EduGreeterMessage::QuizHello), 1);
    QCOMPARE(daemon->got.first().id, id(EduGreeterMessage::QuizHello));
}

void QuizClientTest::helloExactlyOnce()
{
    client->enable();
    client->enable();
    QTRY_COMPARE(daemon->count(EduGreeterMessage::QuizHello), 1);
    client->enable();
    client->query(QStringLiteral("bob"));
    QTest::qWait(150);
    QCOMPARE(daemon->connections, 1);
    QCOMPARE(daemon->count(EduGreeterMessage::QuizHello), 1);
}

void QuizClientTest::statusParsing()
{
    client->enable();
    client->query(QStringLiteral("bob")); // queued until the connection is up
    QTRY_COMPARE(daemon->count(EduGreeterMessage::QuizQuery), 1);
    QCOMPARE(daemon->got.at(0).id, id(EduGreeterMessage::QuizHello)); // hello goes first
    QCOMPARE(daemon->got.at(1).text, QStringLiteral("bob"));
    QVERIFY(!client->statusKnown());

    daemon->send(EduDaemonMessage::QuizStatus, status(QStringLiteral("someone else"), true, 50)); // stale
    QTest::qWait(100);
    QVERIFY(!client->statusKnown());
    QCOMPARE(client->state(), QuizClient::Idle);

    daemon->send(EduDaemonMessage::QuizStatus, status(QStringLiteral("bob"), true, 0, false));
    QTRY_VERIFY(client->statusKnown());
    QVERIFY(client->quizRequired());
    QVERIFY(!client->studentPasswords());
    QCOMPARE(client->cooldownSeconds(), 0);
    QCOMPARE(client->state(), QuizClient::Idle);
}

void QuizClientTest::fullPass()
{
    reachQuestion();
    QCOMPARE(logins.size(), 1);
    QCOMPARE(logins.first(), (QStringList{"bob", "pw", "2"}));
    QCOMPARE(client->questionCount(), 3);
    QCOMPARE(client->requiredCorrect(), 2);
    QCOMPARE(client->questionNumber(), 1);
    QCOMPARE(client->prompt(), QStringLiteral("What is 0 + 1?"));
    QCOMPARE(client->choices().size(), 4);
    QCOMPARE(client->swapsLeft(), 2);

    for (int i = 0; i < 3; ++i) {
        client->answer(QString::number(i));
        QTRY_COMPARE(daemon->count(EduGreeterMessage::QuizAnswer), i + 1);
        QCOMPARE(daemon->got.last().text, QStringLiteral("q%1").arg(i));
        QCOMPARE(daemon->got.last().answer, QString::number(i));

        daemon->send(EduDaemonMessage::QuizFeedback, feedback(QStringLiteral("q%1").arg(i), true, 0, true));
        // the next question or the result comes right behind the feedback, and waits for it
        if (i < 2)
            daemon->send(EduDaemonMessage::QuizQuestion, question(QStringLiteral("q%1").arg(i + 1), i + 1));
        else
            daemon->send(EduDaemonMessage::QuizResult, passedResult());
        waitState(QuizClient::Feedback);
        QVERIFY(client->feedbackCorrect());
        QVERIFY(client->feedbackFinished());
        if (i < 2) {
            QCOMPARE(client->questionId(), QStringLiteral("q%1").arg(i)); // still the old one on screen
            waitState(QuizClient::Question);
            QCOMPARE(client->questionNumber(), i + 2);
        }
    }
    waitState(QuizClient::Result);
    QVERIFY(client->resultPassed());
    QCOMPARE(client->resultCorrect(), 3);

    client->onLoginSucceeded();
    QCOMPARE(client->state(), QuizClient::LoggedIn);
    QVERIFY(!notices.contains(QuizClient::WrongPassword));
}

void QuizClientTest::tryAgainFeedback()
{
    connectAndLogin();
    daemon->send(EduDaemonMessage::QuizQuestion, question(QStringLiteral("q0"), 0, 2, 2));
    waitState(QuizClient::Question);
    client->answer(QStringLiteral("1"));
    QTRY_COMPARE(daemon->count(EduGreeterMessage::QuizAnswer), 1);
    daemon->send(EduDaemonMessage::QuizFeedback, feedback("q0", false, 1, false));
    waitState(QuizClient::Feedback);
    QVERIFY(!client->feedbackCorrect());
    QVERIFY(!client->feedbackFinished());
    waitState(QuizClient::Question); // same question again
    QCOMPARE(client->questionId(), QStringLiteral("q0"));
    QCOMPARE(client->attemptsLeft(), 1);
    QVERIFY(!client->waiting());
    client->answer(QStringLiteral("2")); // and answering works again
    QTRY_COMPARE(daemon->count(EduGreeterMessage::QuizAnswer), 2);
}

void QuizClientTest::revealedFeedbackWaitsForNext()
{
    // long enough that a feedback timer started on the reveal would still hold the question below
    client->setFeedbackMs(5000);
    reachQuestion();
    client->answer(QStringLiteral("0"));
    QTRY_COMPARE(daemon->count(EduGreeterMessage::QuizAnswer), 1);
    daemon->send(EduDaemonMessage::QuizFeedback, feedback("q0", false, 0, true, "the answer", "because"));
    waitState(QuizClient::Feedback);
    QCOMPARE(client->feedbackAnswer(), QStringLiteral("the answer"));
    QCOMPARE(client->feedbackExplanation(), QStringLiteral("because"));
    QVERIFY(client->feedbackNeedsNext());

    QTest::qWait(700);
    QCOMPARE(client->state(), QuizClient::Feedback);
    QCOMPARE(daemon->count(EduGreeterMessage::QuizNext), 0);

    client->next();
    client->next(); // a second press sends nothing
    QTRY_COMPARE(daemon->count(EduGreeterMessage::QuizNext), 1);
    QTest::qWait(50);
    QCOMPARE(daemon->count(EduGreeterMessage::QuizNext), 1);
    QVERIFY(!client->feedbackNeedsNext());
    QCOMPARE(client->state(), QuizClient::Feedback); // until the daemon answers

    daemon->send(EduDaemonMessage::QuizQuestion, question(QStringLiteral("q1"), 1));
    QTRY_COMPARE_WITH_TIMEOUT(client->state(), QuizClient::Question, 1000);
    QCOMPARE(client->questionId(), QStringLiteral("q1"));
    client->next(); // not in Feedback: nothing goes out
    QTest::qWait(50);
    QCOMPARE(daemon->count(EduGreeterMessage::QuizNext), 1);
}

void QuizClientTest::revealedLastQuestionThenResult()
{
    client->setFeedbackMs(5000); // the reply after Next must not wait for the feedback timer
    reachQuestion();
    client->answer(QStringLiteral("0"));
    QTRY_COMPARE(daemon->count(EduGreeterMessage::QuizAnswer), 1);
    daemon->send(EduDaemonMessage::QuizFeedback, feedback("q0", false, 0, true, "the answer"));
    waitState(QuizClient::Feedback);
    QVERIFY(client->feedbackExplanation().isEmpty());
    client->next();
    QTRY_COMPARE(daemon->count(EduGreeterMessage::QuizNext), 1);
    daemon->send(EduDaemonMessage::QuizResult, passedResult());
    QTRY_COMPARE_WITH_TIMEOUT(client->state(), QuizClient::Result, 1000);
    QVERIFY(client->resultPassed());
}

void QuizClientTest::plainFeedbackDoesNotWait()
{
    reachQuestion();
    client->answer(QStringLiteral("0"));
    QTRY_COMPARE(daemon->count(EduGreeterMessage::QuizAnswer), 1);
    daemon->send(EduDaemonMessage::QuizFeedback, feedback("q0", false, 0, true));
    daemon->send(EduDaemonMessage::QuizResult, failedResult());
    waitState(QuizClient::Feedback);
    QVERIFY(!client->feedbackNeedsNext());
    QVERIFY(client->feedbackAnswer().isEmpty());
    client->next(); // nothing to continue: no message
    // the result is held for the feedback time, then shown without any Next
    QTest::qWait(50);
    QCOMPARE(client->state(), QuizClient::Feedback);
    waitState(QuizClient::Result);
    QCOMPARE(daemon->count(EduGreeterMessage::QuizNext), 0);
}

void QuizClientTest::failedQuiz_data()
{
    QTest::addColumn<int>("order"); // when LoginFailed arrives relative to the quiz messages
    QTest::newRow("before feedback") << 0;
    QTest::newRow("between feedback and result") << 1;
    QTest::newRow("after result") << 2;
    QTest::newRow("during the feedback screen") << 3;
}

void QuizClientTest::failedQuiz()
{
    QFETCH(int, order);
    reachQuestion();
    client->answer(QStringLiteral("0"));
    QTRY_COMPARE(daemon->count(EduGreeterMessage::QuizAnswer), 1);

    if (order == 0)
        client->onLoginFailed();
    daemon->send(EduDaemonMessage::QuizFeedback, feedback("q0", false, 0, true));
    if (order == 1)
        QTest::qWait(30); // feedback read first, then LoginFailed, then the result
    if (order == 1)
        client->onLoginFailed();
    daemon->send(EduDaemonMessage::QuizResult, failedResult());
    if (order == 2) {
        QTRY_COMPARE(client->state(), QuizClient::Feedback);
        client->onLoginFailed();
    }
    if (order == 3) {
        waitState(QuizClient::Feedback);
        client->onLoginFailed();
    }

    waitState(QuizClient::Result);
    QVERIFY(!client->resultPassed());
    QCOMPARE(client->resultCorrect(), 1);
    QCOMPARE(client->resultRequired(), 2);
    QCOMPARE(client->resultAsked(), 3);
    const QVariantList review = client->resultReview();
    QCOMPARE(review.size(), 2);
    QCOMPARE(review.at(1).toMap().value("correctAnswer").toString(), QStringLiteral("y"));
    QCOMPARE(review.at(0).toMap().value("explanation").toString(), QStringLiteral("Because."));

    // the login failure must not turn the result into a "wrong password" or a generic error
    QTest::qWait(300);
    QCOMPARE(client->state(), QuizClient::Result);
    QCOMPARE(client->notice(), QuizClient::NoNotice);
    QVERIFY(!notices.contains(QuizClient::WrongPassword));
    QVERIFY(!notices.contains(QuizClient::LoginFailed));

    client->continueFromResult();
    QCOMPARE(client->state(), QuizClient::Cooldown);
    QVERIFY(client->cooldownSeconds() > 28 && client->cooldownSeconds() <= 30);
}

void QuizClientTest::cooldownRefusal_data()
{
    QTest::addColumn<bool>("loginFailedFirst");
    QTest::newRow("cooldown first") << false;
    QTest::newRow("login failed first") << true;
}

void QuizClientTest::cooldownRefusal()
{
    QFETCH(bool, loginFailedFirst);
    connectAndLogin();
    if (loginFailedFirst) {
        client->onLoginFailed();
        QTest::qWait(20);
    }
    daemon->send(EduDaemonMessage::QuizCooldown, QuizCooldownMsg{25});
    waitState(QuizClient::Cooldown);
    if (!loginFailedFirst)
        client->onLoginFailed();

    QTest::qWait(300); // long after any verdict would be interpreted
    QCOMPARE(client->state(), QuizClient::Cooldown);
    QCOMPARE(client->notice(), QuizClient::NoNotice);
    QVERIFY(!notices.contains(QuizClient::WrongPassword));
    QVERIFY(client->cooldownSeconds() > 23 && client->cooldownSeconds() <= 25);
}

void QuizClientTest::loginVerdicts_data()
{
    QTest::addColumn<int>("verdict"); // 0 wrong password, 1 busy, 2 not a quiz user
    QTest::addColumn<bool>("loginFailedFirst");
    QTest::addColumn<QuizClient::Notice>("notice");
    QTest::newRow("wrong password, verdict first") << 0 << false << QuizClient::WrongPassword;
    QTest::newRow("wrong password, login failed first") << 0 << true << QuizClient::WrongPassword;
    QTest::newRow("busy, verdict first") << 1 << false << QuizClient::LoginFailed;
    QTest::newRow("busy, login failed first") << 1 << true << QuizClient::LoginFailed;
    QTest::newRow("refused, verdict first") << 3 << false << QuizClient::LoginFailed;
    QTest::newRow("refused, login failed first") << 3 << true << QuizClient::LoginFailed;
    QTest::newRow("no quiz, verdict first") << 2 << false << QuizClient::WrongPassword;
    QTest::newRow("no quiz, login failed first") << 2 << true << QuizClient::WrongPassword;
}

void QuizClientTest::loginVerdicts()
{
    QFETCH(int, verdict);
    QFETCH(bool, loginFailedFirst);
    QFETCH(QuizClient::Notice, notice);
    connectAndLogin(verdict != 2);

    auto sendVerdict = [&] {
        if (verdict == 2)
            daemon->send(EduDaemonMessage::QuizStatus, status(QStringLiteral("bob"), false));
        else
            daemon->send(EduDaemonMessage::QuizLoginRefused,
                         QuizLoginRefusedMsg{verdict == 0 ? QStringLiteral("wrong-password")
                                                       : verdict == 3 ? QStringLiteral("refused") : QStringLiteral("busy")});
    };
    if (loginFailedFirst) {
        client->onLoginFailed();
        QTest::qWait(150);
        QCOMPARE(client->state(), QuizClient::Checking); // no verdict yet: not interpreted
        sendVerdict();
    } else {
        sendVerdict();
        QTest::qWait(150);
        QCOMPARE(client->state(), QuizClient::Checking); // the verdict alone changes nothing
        client->onLoginFailed();
    }
    waitState(QuizClient::Idle);
    QCOMPARE(client->notice(), notice);
}

// There is no timer: without a verdict a LoginFailed waits, however long
void QuizClientTest::loginFailedWithoutVerdictWaits()
{
    connectAndLogin();
    client->onLoginFailed();
    QTest::qWait(800);
    QCOMPARE(client->state(), QuizClient::Checking);
    daemon->send(EduDaemonMessage::QuizCooldown, QuizCooldownMsg{20}); // e.g. the verdict turns out to be this
    waitState(QuizClient::Cooldown);
    QCOMPARE(client->notice(), QuizClient::NoNotice);
}

void QuizClientTest::loginFailedWithNoQuizConnection()
{
    // stock theme: never enabled, so a LoginFailed is a wrong password straight away
    client->login(QStringLiteral("bob"), QStringLiteral("pw"), 0);
    client->onLoginFailed();
    QCOMPARE(client->state(), QuizClient::Idle);
    QCOMPARE(client->notice(), QuizClient::WrongPassword);

    // enabled but the daemon never said ready: no verdict can come, and it is not a "wrong password"
    daemon->autoReady = false;
    client->enable();
    QTRY_COMPARE(daemon->count(EduGreeterMessage::QuizHello), 1);
    QVERIFY(client->connecting());
    QVERIFY(!client->ready());
    QCOMPARE(logins.size(), 1);
    client->login(QStringLiteral("bob"), QStringLiteral("pw"), 0); // refused while connecting
    QCOMPARE(logins.size(), 1);
    daemon->conn->disconnectFromServer();
    QTRY_VERIFY(!client->connecting());
    QVERIFY(!client->errorMessage().isEmpty());
    // the connection is down: a parent can log in, and a quiz user fails closed
    client->login(QStringLiteral("dad"), QStringLiteral("pw"), 0);
    QCOMPARE(logins.size(), 2);
    client->onLoginFailed();
    QCOMPARE(client->state(), QuizClient::Idle);
    QCOMPARE(client->notice(), QuizClient::LoginFailed);
}

void QuizClientTest::readyAndConnecting()
{
    QVERIFY(!client->connecting());
    QVERIFY(!client->ready());
    QSignalSpy spy(client, &QuizClient::connectionChanged);
    daemon->autoReady = false;
    client->enable();
    QVERIFY(client->connecting());
    QTRY_COMPARE(daemon->count(EduGreeterMessage::QuizHello), 1);
    QVERIFY(client->connecting());
    daemon->sendReady();
    QTRY_VERIFY(client->ready());
    QVERIFY(!client->connecting());
    QVERIFY(spy.size() >= 2);

    daemon->conn->disconnectFromServer();
    QTRY_VERIFY(!client->ready());
    QVERIFY(!client->connecting());
    // reconnect: not ready again until the next QuizReady
    daemon->autoReady = true;
    client->dismiss();
    QTRY_COMPARE(daemon->connections, 2);
    QTRY_VERIFY(client->ready());
}

void QuizClientTest::loginIsNeverQueued()
{
    daemon->autoReady = false;
    client->enable();
    QTRY_COMPARE(daemon->count(EduGreeterMessage::QuizHello), 1);
    client->login(QStringLiteral("bob"), QStringLiteral("secret"), 1); // connecting: dropped, not kept
    QCOMPARE(logins.size(), 0);
    QCOMPARE(client->state(), QuizClient::Idle);
    daemon->sendReady();
    QTRY_VERIFY(client->ready());
    QTest::qWait(150);
    QCOMPARE(logins.size(), 0); // becoming ready does not replay it
    client->login(QStringLiteral("bob"), QStringLiteral("secret"), 1);
    QCOMPARE(logins.size(), 1);
    QCOMPARE(logins.first(), (QStringList{"bob", "secret", "1"}));
}

void QuizClientTest::queryAfterDropReachesDaemon()
{
    client->enable();
    QTRY_VERIFY(client->ready());
    daemon->conn->disconnectFromServer();
    QTRY_VERIFY(!client->ready());
    client->query(QStringLiteral("bob")); // reconnects; the query must go out after the new hello
    QTRY_COMPARE(daemon->connections, 2);
    QTRY_COMPARE(daemon->count(EduGreeterMessage::QuizQuery), 1);
    QCOMPARE(daemon->got.at(daemon->got.size() - 2).id, id(EduGreeterMessage::QuizHello));
}

void QuizClientTest::dismissReconnects()
{
    client->enable();
    QTRY_VERIFY(client->ready());
    daemon->conn->disconnectFromServer();
    QTRY_VERIFY(!client->errorMessage().isEmpty());
    client->dismiss();
    QVERIFY(client->errorMessage().isEmpty());
    QTRY_COMPARE(daemon->connections, 2);
    QTRY_VERIFY(client->ready());
}

void QuizClientTest::oversizedInputClosesConnection()
{
    reachQuestion();
    // a QuizError that never ends: bigger than the client will buffer
    QByteArray big;
    QDataStream out(&big, QIODevice::WriteOnly);
    out << quint32(EduDaemonMessage::QuizError) << quint32(3 * 1024 * 1024);
    big += QByteArray(2 * 1024 * 1024, 'x');
    daemon->send(big);
    waitState(QuizClient::Idle);
    QVERIFY(!client->errorMessage().isEmpty());
    QTRY_VERIFY(!daemon->connected());
}

void QuizClientTest::answerStartedNotifies()
{
    reachQuestion();
    QVERIFY(!client->answerStarted());
    QSignalSpy spy(client, &QuizClient::questionChanged);
    client->answer(QStringLiteral("0"));
    QVERIFY(client->answerStarted());
    QVERIFY(spy.size() >= 1);
}

void QuizClientTest::statusCarriesQuestionCount()
{
    client->enable();
    QTRY_VERIFY(client->ready());
    client->query(QStringLiteral("bob"));
    QTRY_COMPARE(daemon->count(EduGreeterMessage::QuizQuery), 1);
    QuizStatusMsg s = status(QStringLiteral("bob"), true);
    s.questionCount = 7;
    s.requiredCorrect = 6;
    daemon->send(EduDaemonMessage::QuizStatus, s);
    QTRY_COMPARE(client->questionCount(), 7);
    QCOMPARE(client->requiredCorrect(), 6);
}

// A quiz-connection or configuration error must not stop anyone else logging in
void QuizClientTest::errorDoesNotBlockNextUser()
{
    connectAndLogin();
    daemon->send(EduDaemonMessage::QuizError, QuizErrorMsg{QStringLiteral("misconfigured")});
    waitState(QuizClient::Idle);
    QVERIFY(!client->errorMessage().isEmpty());
    client->query(QStringLiteral("mum")); // no dismiss() needed
    QVERIFY(client->errorMessage().isEmpty());
    client->login(QStringLiteral("mum"), QStringLiteral("pw"), 0);
    QCOMPARE(client->state(), QuizClient::Checking);
    QCOMPARE(logins.last().first(), QStringLiteral("mum"));
}

void QuizClientTest::passedButSessionFails_data()
{
    QTest::addColumn<bool>("loginFailedFirst");
    QTest::newRow("result first") << false;
    QTest::newRow("login failed first") << true;
}

void QuizClientTest::passedButSessionFails()
{
    QFETCH(bool, loginFailedFirst);
    reachQuestion();
    client->answer(QStringLiteral("0"));
    QTRY_COMPARE(daemon->count(EduGreeterMessage::QuizAnswer), 1);
    daemon->send(EduDaemonMessage::QuizFeedback, feedback("q0", true, 0, true));
    daemon->send(EduDaemonMessage::QuizResult, passedResult());
    if (loginFailedFirst) {
        client->onLoginFailed();
    } else {
        waitState(QuizClient::Result);
        client->onLoginFailed();
    }
    waitState(QuizClient::Idle);
    QCOMPARE(client->notice(), QuizClient::LoginFailed);
    QVERIFY(!notices.contains(QuizClient::WrongPassword));
}

void QuizClientTest::messageSplitAcrossWrites()
{
    reachQuestion();
    client->answer(QStringLiteral("0"));
    QTRY_COMPARE(daemon->count(EduGreeterMessage::QuizAnswer), 1);

    // feedback + a big result (it has a review list), cut in the middle of the result
    QByteArray bytes = encodeMessage(EduDaemonMessage::QuizFeedback, feedback("q0", false, 0, true));
    const qsizetype feedbackSize = bytes.size();
    bytes += encodeMessage(EduDaemonMessage::QuizResult, failedResult());
    QVERIFY(bytes.size() > feedbackSize + 400);

    const qsizetype cuts[] = {feedbackSize - 1, feedbackSize + 2, feedbackSize + 30, bytes.size() - 1};
    qsizetype from = 0;
    for (qsizetype cut : cuts) {
        daemon->send(bytes.mid(from, cut - from));
        from = cut;
        QTest::qWait(30);
        QVERIFY2(client->state() != QuizClient::Result, "a half message was acted on");
    }
    daemon->send(bytes.mid(from));

    waitState(QuizClient::Result);
    QCOMPARE(client->resultReview().size(), 2);
    QCOMPARE(client->resultCorrect(), 1);
    QCOMPARE(daemon->conn->state(), QLocalSocket::ConnectedState);
}

void QuizClientTest::twoMessagesInOneWrite()
{
    connectAndLogin();
    QByteArray bytes = encodeMessage(EduDaemonMessage::QuizStarted, QuizStartedMsg{QStringLiteral("bob"), 3, 2, 2});
    bytes += encodeMessage(EduDaemonMessage::QuizQuestion, question(QStringLiteral("q0"), 0));
    bytes += encodeMessage(EduDaemonMessage::QuizSwapRefused, QuizSwapRefusedMsg{QStringLiteral("no-swaps-left")});
    daemon->send(bytes);
    waitState(QuizClient::Question);
    QCOMPARE(client->questionCount(), 3);
    QCOMPARE(client->questionId(), QStringLiteral("q0"));
    QCOMPARE(client->swapRefusedReason(), QStringLiteral("no-swaps-left"));
}

void QuizClientTest::abandonAfterAnswerQueriesStatus()
{
    reachQuestion();
    client->answer(QStringLiteral("0"));
    QTRY_COMPARE(daemon->count(EduGreeterMessage::QuizAnswer), 1);
    daemon->send(EduDaemonMessage::QuizFeedback, feedback("q0", true, 0, true));
    daemon->send(EduDaemonMessage::QuizQuestion, question(QStringLiteral("q1"), 1));
    waitState(QuizClient::Question);

    const int queriesBefore = daemon->count(EduGreeterMessage::QuizQuery);
    client->abandon();
    QCOMPARE(client->state(), QuizClient::Idle);
    QTRY_COMPARE(daemon->count(EduGreeterMessage::QuizAbandon), 1);
    QTRY_COMPARE(daemon->count(EduGreeterMessage::QuizQuery), queriesBefore + 1);
    QCOMPARE(daemon->got.at(daemon->got.size() - 2).id, id(EduGreeterMessage::QuizAbandon));
    QCOMPARE(daemon->got.last().text, QStringLiteral("bob"));

    // the daemon counted it as a failure; the status carries the cooldown
    daemon->send(EduDaemonMessage::QuizStatus, status(QStringLiteral("bob"), true, 40));
    waitState(QuizClient::Cooldown);
    QVERIFY(client->cooldownSeconds() > 38 && client->cooldownSeconds() <= 40);
    // and the LoginFailed the daemon sends for the abandoned login changes nothing
    client->onLoginFailed();
    QTest::qWait(200);
    QCOMPARE(client->state(), QuizClient::Cooldown);
    QCOMPARE(client->notice(), QuizClient::NoNotice);
}

void QuizClientTest::quizSocketLost()
{
    reachQuestion();
    daemon->conn->disconnectFromServer();
    waitState(QuizClient::Idle);
    QVERIFY(!client->errorMessage().isEmpty());

    // no reconnect loop, but the next enable() reconnects and says hello again
    QTest::qWait(300);
    QCOMPARE(daemon->connections, 1);
    client->enable();
    QTRY_COMPARE(daemon->connections, 2);
    QTRY_COMPARE(daemon->count(EduGreeterMessage::QuizHello), 2);
    QTRY_VERIFY(client->errorMessage().isEmpty());
}

void QuizClientTest::unreadableMessageClosesConnection()
{
    reachQuestion();
    QByteArray junk;
    QDataStream(&junk, QIODevice::WriteOnly) << quint32(0x12345678);
    daemon->send(junk);
    waitState(QuizClient::Idle);
    QVERIFY(!client->errorMessage().isEmpty());
    QTRY_VERIFY(!daemon->connected());
}

void QuizClientTest::staleInputIgnored()
{
    client->enable();
    QTRY_COMPARE(daemon->count(EduGreeterMessage::QuizHello), 1);
    client->answer(QStringLiteral("0")); // not in a quiz
    client->swap();
    client->abandon();
    QTest::qWait(100);
    QCOMPARE(daemon->got.size(), 1);

    reachQuestion();
    client->answer(QStringLiteral("0"));
    client->answer(QStringLiteral("1")); // double click before the feedback
    client->swap();
    QTRY_COMPARE(daemon->count(EduGreeterMessage::QuizAnswer), 1);
    QTest::qWait(60);
    QCOMPARE(daemon->count(EduGreeterMessage::QuizAnswer), 1);
    QCOMPARE(daemon->count(EduGreeterMessage::QuizSwap), 0);

    // in Feedback too
    daemon->send(EduDaemonMessage::QuizFeedback, feedback("q0", true, 0, true));
    daemon->send(EduDaemonMessage::QuizQuestion, question(QStringLiteral("q1"), 1));
    waitState(QuizClient::Feedback);
    client->answer(QStringLiteral("2"));
    client->swap();
    QTest::qWait(60);
    QCOMPARE(daemon->count(EduGreeterMessage::QuizAnswer), 1);
    QCOMPARE(daemon->count(EduGreeterMessage::QuizSwap), 0);
    waitState(QuizClient::Question);
    QCOMPARE(client->questionId(), QStringLiteral("q1"));
    client->answer(QStringLiteral("2")); // and it works again afterwards
    QTRY_COMPARE(daemon->count(EduGreeterMessage::QuizAnswer), 2);
    QCOMPARE(daemon->got.last().text, QStringLiteral("q1"));
}

void QuizClientTest::swapRefusedAndSwapped()
{
    reachQuestion();
    client->swap();
    QTRY_COMPARE(daemon->count(EduGreeterMessage::QuizSwap), 1);
    QCOMPARE(daemon->got.last().text, QStringLiteral("q0"));
    QVERIFY(client->waiting());

    QSignalSpy refused(client, &QuizClient::swapRefused);
    daemon->send(EduDaemonMessage::QuizSwapRefused, QuizSwapRefusedMsg{QStringLiteral("already-attempted")});
    QTRY_COMPARE(refused.size(), 1);
    QCOMPARE(client->swapRefusedReason(), QStringLiteral("already-attempted"));
    QVERIFY(!client->waiting());
    QCOMPARE(client->state(), QuizClient::Question);

    client->swap();
    QTRY_COMPARE(daemon->count(EduGreeterMessage::QuizSwap), 2);
    daemon->send(EduDaemonMessage::QuizQuestion, question(QStringLiteral("q0b"), 0, 1));
    QTRY_COMPARE(client->questionId(), QStringLiteral("q0b"));
    QCOMPARE(client->swapsLeft(), 1);
    QVERIFY(client->swapRefusedReason().isEmpty());
    QVERIFY(!client->waiting());
    QCOMPARE(client->state(), QuizClient::Question);
}

void QuizClientTest::quizErrorReturnsToIdle()
{
    connectAndLogin();
    daemon->send(EduDaemonMessage::QuizError, QuizErrorMsg{QStringLiteral("Edu-LM is misconfigured")});
    client->onInformationMessage(QStringLiteral("Edu-LM is misconfigured"));
    client->onLoginFailed();
    waitState(QuizClient::Idle);
    QCOMPARE(client->errorMessage(), QStringLiteral("Edu-LM is misconfigured"));
    QTest::qWait(250);
    QCOMPARE(client->notice(), QuizClient::NoNotice); // not "wrong password"
    client->dismiss();
    QVERIFY(client->errorMessage().isEmpty());
}

namespace {

// A greeter wired to the mock daemon, as sddm-greeter --test-mode does it
struct MockRig {
    explicit MockRig(int loginDelayMs = 0)
    {
        MockSettings s;
        s.gate.policy.groups = {QStringLiteral("edu-students")};
        s.gate.cooldownSeconds = 2;
        s.gate.subjects = {QStringLiteral("maths"), QStringLiteral("science"), QStringLiteral("hass"), QStringLiteral("english")};
        s.gate.questionDirs = {QStringLiteral(EDU_SAMPLE_DIR)};
        s.exemptUsers = {QStringLiteral("parent")};
        s.seeded = true;
        s.seed = 7;
        s.verifyDelayMs = 0;
        s.sessionStartMs = 700;
        s.loginResultDelayMs = loginDelayMs;
        s.logAnswers = false;
        mock = new MockDaemon(s);
        client = new QuizClient(mock->serverName(), [this](const QString &u, const QString &p, int i) { mock->login(u, p, i); });
        client->setFeedbackMs(300);
        QObject::connect(mock, &MockDaemon::loginSucceeded, client, &QuizClient::onLoginSucceeded);
        QObject::connect(mock, &MockDaemon::loginFailed, client, &QuizClient::onLoginFailed);
        QObject::connect(mock, &MockDaemon::informationMessage, client, &QuizClient::onInformationMessage);
        QObject::connect(mock, &MockDaemon::answerKnown, client, [this](const QString &id, const QString &a) { answers[id] = a; });
        QObject::connect(client, &QuizClient::messagesChanged, client, [this] { notices.append(client->notice()); });
        client->enable();
        client->query(QStringLiteral("kid"));
        QTRY_VERIFY_WITH_TIMEOUT(client->ready(), 2000);
    }
    ~MockRig() { delete client; delete mock; }

    MockDaemon *mock = nullptr;
    QuizClient *client = nullptr;
    QMap<QString, QString> answers;
    QList<QuizClient::Notice> notices;
};

}

void QuizClientTest::mockPass()
{
    MockRig rig;
    QuizClient &c = *rig.client;
    QTRY_VERIFY_WITH_TIMEOUT(c.statusKnown(), 2000);
    QVERIFY(c.quizRequired());
    c.login(QStringLiteral("kid"), QStringLiteral("anything"), 0);
    for (int i = 0; i < 5; ++i) {
        QTRY_COMPARE_WITH_TIMEOUT(c.state(), QuizClient::Question, 2000);
        QCOMPARE(c.questionNumber(), i + 1);
        QVERIFY(rig.answers.contains(c.questionId()));
        c.answer(rig.answers.value(c.questionId()));
        QTRY_COMPARE_WITH_TIMEOUT(c.state(), QuizClient::Feedback, 2000);
        QVERIFY(c.feedbackCorrect());
    }
    QTRY_COMPARE_WITH_TIMEOUT(c.state(), QuizClient::Result, 2000);
    QVERIFY(c.resultPassed());
    QCOMPARE(c.resultCorrect(), 5);
    QTRY_COMPARE_WITH_TIMEOUT(c.state(), QuizClient::LoggedIn, 2000);
    QVERIFY(!rig.notices.contains(QuizClient::WrongPassword));
}

void QuizClientTest::mockFail_data()
{
    QTest::addColumn<int>("loginDelayMs");
    QTest::newRow("login result soon") << 0;
    QTest::newRow("login result late") << 120;
}

void QuizClientTest::mockFail()
{
    QFETCH(int, loginDelayMs);
    MockRig rig(loginDelayMs);
    QuizClient &c = *rig.client;
    QTRY_VERIFY_WITH_TIMEOUT(c.statusKnown(), 2000);
    c.login(QStringLiteral("kid"), QStringLiteral("pw"), 0);
    for (int i = 0; i < 5; ++i) {
        QTRY_COMPARE_WITH_TIMEOUT(c.state(), QuizClient::Question, 2000);
        c.answer(QStringLiteral("not an answer"));
        QTRY_COMPARE_WITH_TIMEOUT(c.state(), QuizClient::Feedback, 2000);
        QVERIFY(!c.feedbackCorrect());
        // the mock shows answers, so each miss waits for Next
        QVERIFY(c.feedbackNeedsNext());
        QVERIFY(!c.feedbackAnswer().isEmpty());
        c.next();
    }
    QTRY_COMPARE_WITH_TIMEOUT(c.state(), QuizClient::Result, 2000);
    QVERIFY(!c.resultPassed());
    QCOMPARE(c.resultCorrect(), 0);
    QCOMPARE(c.resultReview().size(), 5);
    QTest::qWait(400); // LoginFailed has long arrived
    QCOMPARE(c.state(), QuizClient::Result);
    QVERIFY(!rig.notices.contains(QuizClient::WrongPassword));
    QVERIFY(!rig.notices.contains(QuizClient::LoginFailed));

    c.continueFromResult();
    QCOMPARE(c.state(), QuizClient::Cooldown);
    QVERIFY(c.cooldownSeconds() >= 1 && c.cooldownSeconds() <= 2);

    // trying again during the cooldown is refused with the cooldown, in either arrival order
    c.login(QStringLiteral("kid"), QStringLiteral("pw"), 0);
    QTest::qWait(500);
    QCOMPARE(c.state(), QuizClient::Cooldown);
    QVERIFY(!rig.notices.contains(QuizClient::WrongPassword));
    QTRY_COMPARE_WITH_TIMEOUT(c.state(), QuizClient::Idle, 4000);
    c.login(QStringLiteral("kid"), QStringLiteral("pw"), 0);
    QTRY_COMPARE_WITH_TIMEOUT(c.state(), QuizClient::Question, 2000); // a new attempt
}

void QuizClientTest::mockWrongPasswordAndExempt()
{
    MockRig rig;
    QuizClient &c = *rig.client;
    c.login(QStringLiteral("kid"), QStringLiteral("wrong"), 0);
    QTRY_COMPARE_WITH_TIMEOUT(c.state(), QuizClient::Idle, 2000);
    QCOMPARE(c.notice(), QuizClient::WrongPassword);

    c.login(QStringLiteral("parent"), QStringLiteral("wrong"), 0);
    QTRY_COMPARE_WITH_TIMEOUT(c.state(), QuizClient::Idle, 2000);
    QCOMPARE(c.notice(), QuizClient::WrongPassword);

    c.login(QStringLiteral("parent"), QStringLiteral("right"), 0);
    QTRY_COMPARE_WITH_TIMEOUT(c.state(), QuizClient::LoggedIn, 3000); // no quiz
}

// Tearing the mock down with a quiz running and answers given used to crash
void QuizClientTest::mockDeletedMidQuiz()
{
    auto *rig = new MockRig;
    QuizClient &c = *rig->client;
    QTRY_VERIFY_WITH_TIMEOUT(c.statusKnown(), 2000);
    c.login(QStringLiteral("kid"), QStringLiteral("pw"), 0);
    QTRY_COMPARE_WITH_TIMEOUT(c.state(), QuizClient::Question, 2000);
    c.answer(QStringLiteral("not an answer"));
    QTRY_COMPARE_WITH_TIMEOUT(c.state(), QuizClient::Feedback, 2000);
    // the mock goes first while its client is still connected and answers have been counted
    delete rig->mock;
    rig->mock = nullptr;
    delete rig;
    QVERIFY(true);
}

// B1: a login sent while the quiz connection is down, the reconnect finishing before its LoginFailed
void QuizClientTest::readyBetweenLoginAndLoginFailed()
{
    client->enable();
    QTRY_VERIFY(client->ready());
    daemon->conn->disconnectFromServer();
    QTRY_VERIFY(!client->ready());
    client->login(QStringLiteral("bob"), QStringLiteral("pw"), 0);
    QCOMPARE(client->state(), QuizClient::Checking);
    QCOMPARE(daemon->connections, 1); // login itself does not reconnect
    client->enable(); // something else reconnects
    QTRY_COMPARE(daemon->connections, 2);
    QTRY_VERIFY(client->ready()); // QuizReady lands before the LoginFailed
    client->onLoginFailed();
    QCOMPARE(client->state(), QuizClient::Idle); // not stuck waiting for a verdict that cannot come
    QCOMPARE(client->notice(), QuizClient::LoginFailed);
}

void QuizClientTest::quizDownInfoIsCalm()
{
    client->enable();
    QTRY_VERIFY(client->ready());
    daemon->conn->disconnectFromServer();
    QTRY_VERIFY(!client->ready());
    client->login(QStringLiteral("bob"), QStringLiteral("pw"), 0);
    client->onInformationMessage(QStringLiteral("This account needs the Edu-LM login screen"));
    client->onLoginFailed();
    QVERIFY(!client->infoMessage().isEmpty());
    QVERIFY(!client->infoMessage().contains(QStringLiteral("login screen")));
    QTRY_COMPARE(daemon->connections, 2); // and it reconnects for the next try
}

void QuizClientTest::cancelCheckingAbandonsAndIgnoresLateVerdicts()
{
    client->setSlowCheckingMs(100);
    connectAndLogin();
    QVERIFY(!client->slowChecking());
    QTRY_VERIFY(client->slowChecking());
    client->cancelChecking();
    QCOMPARE(client->state(), QuizClient::Idle);
    QVERIFY(!client->slowChecking());
    QTRY_COMPARE(daemon->count(EduGreeterMessage::QuizAbandon), 1);

    // late news about the given-up login changes nothing
    client->onLoginFailed();
    daemon->send(EduDaemonMessage::QuizLoginRefused, QuizLoginRefusedMsg{QStringLiteral("wrong-password")});
    QTest::qWait(100);
    QCOMPARE(client->state(), QuizClient::Idle);
    QCOMPARE(client->notice(), QuizClient::NoNotice);
    // a quiz that started anyway is ended, not shown
    daemon->send(EduDaemonMessage::QuizStarted, QuizStartedMsg{QStringLiteral("bob"), 3, 2, 2});
    daemon->send(EduDaemonMessage::QuizQuestion, question(QStringLiteral("q0"), 0));
    QTRY_COMPARE(daemon->count(EduGreeterMessage::QuizAbandon), 2);
    QCOMPARE(client->state(), QuizClient::Idle);
}

// The same B1 with the real gate: connection down, login, reconnect, then the LoginFailed
void QuizClientTest::mockLoginWhileConnectionDown()
{
    MockRig rig(150); // LoginFailed arrives after the reconnect
    QuizClient &c = *rig.client;
    QTRY_VERIFY_WITH_TIMEOUT(c.ready(), 2000);
    rig.mock->dropConnections();
    QTRY_VERIFY_WITH_TIMEOUT(!c.ready(), 2000);
    c.login(QStringLiteral("kid"), QStringLiteral("pw"), 0);
    c.enable();
    QTRY_VERIFY_WITH_TIMEOUT(c.ready(), 2000);
    QTRY_COMPARE_WITH_TIMEOUT(c.state(), QuizClient::Idle, 3000);
    QCOMPARE(c.notice(), QuizClient::LoginFailed);
}

// ---- protocol version ----

void QuizClientTest::matchingVersionIsReady()
{
    client->enable();
    QTRY_VERIFY(client->ready());
    QVERIFY(!client->connecting());
    QVERIFY(client->errorMessage().isEmpty());
    QCOMPARE(daemon->connections, 1);
}

void QuizClientTest::mismatch_data()
{
    QTest::addColumn<bool>("sendVersion");
    QTest::addColumn<quint32>("version");
    QTest::newRow("old daemon: no version") << false << quint32(0);
    QTest::newRow("older number") << true << quint32(1);
    QTest::newRow("newer number") << true << quint32(EduProtocolVersion + 1);
}

void QuizClientTest::mismatch()
{
    QFETCH(bool, sendVersion);
    QFETCH(quint32, version);
    daemon->sendVersion = sendVersion;
    daemon->version = version;
    client->enable();
    QTRY_VERIFY(!client->errorMessage().isEmpty());
    QCOMPARE(client->errorMessage(), QStringLiteral("The quiz was updated. Please restart the computer."));
    QVERIFY(!client->ready());
    QVERIFY(!client->connecting());
    QTRY_VERIFY(!daemon->connected()); // the quiz socket was closed

    // no reconnect from query() (nor anything it queues), and the error is not cleared
    client->query(QStringLiteral("bob"));
    QTest::qWait(150);
    QCOMPARE(daemon->connections, 1);
    QCOMPARE(daemon->count(EduGreeterMessage::QuizQuery), 0);
    QCOMPARE(daemon->count(EduGreeterMessage::QuizHello), 1);
    QVERIFY(!client->errorMessage().isEmpty());
    QCOMPARE(client->state(), QuizClient::Idle);

    // a parent still logs in, with no verdict to wait for
    client->login(QStringLiteral("dad"), QStringLiteral("pw"), 3);
    QCOMPARE(logins.size(), 1);
    QCOMPARE(logins.first(), (QStringList{"dad", "pw", "3"}));
    QCOMPARE(client->state(), QuizClient::Checking);
    client->onLoginSucceeded();
    QCOMPARE(client->state(), QuizClient::LoggedIn);
}

void QuizClientTest::mismatchDoesNotReconnect()
{
    daemon->sendVersion = false;
    client->enable();
    QTRY_VERIFY(!client->errorMessage().isEmpty());
    // even a daemon that is now current is not asked again until the greeter restarts
    daemon->sendVersion = true;
    client->enable();
    client->query(QStringLiteral("bob"));
    client->dismiss();
    QTest::qWait(200);
    QCOMPARE(daemon->connections, 1);
    QVERIFY(!client->ready());
    QVERIFY(!client->connecting());
}

void QuizClientTest::mismatchSurvivesDismissAndFailedLogin()
{
    daemon->version = EduProtocolVersion + 1;
    client->enable();
    QTRY_VERIFY(!client->errorMessage().isEmpty());
    const QString error = client->errorMessage();

    // a student's login fails closed, with the explanation still showing and no "try again" hint
    client->login(QStringLiteral("bob"), QStringLiteral("pw"), 0);
    QCOMPARE(logins.size(), 1);
    // the real daemon says this before LoginFailed
    client->onInformationMessage(QStringLiteral("This account needs the Edu-LM login screen"));
    client->onLoginFailed();
    QCOMPARE(client->state(), QuizClient::Idle);
    QCOMPARE(client->notice(), QuizClient::LoginFailed);
    QCOMPARE(client->errorMessage(), error);
    QVERIFY(client->versionMismatch());
    QVERIFY(client->infoMessage().isEmpty());

    client->dismiss();
    QCOMPARE(client->errorMessage(), error);
    QTest::qWait(100);
    QCOMPARE(daemon->connections, 1);
}

// QuizReady alone is not enough, even after other traffic
void QuizClientTest::versionBeforeReadyIsRequired()
{
    daemon->autoReady = false;
    client->enable();
    QTRY_COMPARE(daemon->count(EduGreeterMessage::QuizHello), 1);
    daemon->send(EduDaemonMessage::QuizStatus, status(QStringLiteral("bob"), true));
    daemon->send(encodeMessage(EduDaemonMessage::QuizReady));
    QTRY_VERIFY(!client->errorMessage().isEmpty());
    QVERIFY(!client->ready());
}

void QuizClientTest::mockVersionMismatch()
{
    MockSettings settings;
    settings.protocolVersion = EduProtocolVersion + 1;
    MockDaemon mock(settings);
    QVERIFY(mock.isListening());
    QuizClient mocked(mock.serverName(), [](const QString &, const QString &, int) {});
    mocked.enable();
    QTRY_VERIFY(!mocked.errorMessage().isEmpty());
    QVERIFY(!mocked.ready());
}

// Only QuizHello goes out before a matching version and QuizReady; a query waits behind them
void QuizClientTest::queryWaitsForVersionAndReady()
{
    client->enable();
    QTRY_VERIFY(client->ready());
    daemon->autoReady = false;
    daemon->conn->disconnectFromServer();
    QTRY_VERIFY(!client->ready());
    client->query(QStringLiteral("bob")); // reconnects, with the query queued behind the hello
    QTRY_COMPARE(daemon->count(EduGreeterMessage::QuizHello), 2);
    QTest::qWait(100);
    QCOMPARE(daemon->count(EduGreeterMessage::QuizQuery), 0);
    daemon->send(EduDaemonMessage::QuizVersion, QuizVersionMsg{EduProtocolVersion});
    QTest::qWait(100);
    QCOMPARE(daemon->count(EduGreeterMessage::QuizQuery), 0);
    daemon->send(encodeMessage(EduDaemonMessage::QuizReady));
    QTRY_COMPARE(daemon->count(EduGreeterMessage::QuizQuery), 1);
    QCOMPARE(daemon->got.size(), 3); // hello, hello, query
    QCOMPARE(daemon->got.at(2).text, QStringLiteral("bob"));
}

void QuizClientTest::queuedQueryDroppedOnMismatch()
{
    daemon->autoReady = false;
    client->enable();
    client->query(QStringLiteral("bob"));
    QTRY_COMPARE(daemon->count(EduGreeterMessage::QuizHello), 1);
    daemon->send(encodeMessage(EduDaemonMessage::QuizReady)); // no version before it
    QTRY_VERIFY(client->versionMismatch());
    QTest::qWait(100);
    QCOMPARE(daemon->count(EduGreeterMessage::QuizQuery), 0);
    QCOMPARE(daemon->got.size(), 1); // just the hello
}

// A match on one connection says nothing about the next
void QuizClientTest::versionStateResetsOnReconnect()
{
    client->enable();
    QTRY_VERIFY(client->ready());
    daemon->conn->disconnectFromServer();
    QTRY_VERIFY(!client->ready());
    daemon->sendVersion = false;
    client->dismiss(); // reconnects
    QTRY_COMPARE(daemon->connections, 2);
    QTRY_VERIFY(client->versionMismatch());
    QVERIFY(!client->ready());
}

QTEST_MAIN(QuizClientTest)
#include "QuizClientTest.moc"
