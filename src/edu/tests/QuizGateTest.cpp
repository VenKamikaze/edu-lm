// SPDX-License-Identifier: GPL-2.0-or-later
#include "daemon/QuizGate.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalServer>
#include <QLocalSocket>
#include <QTemporaryDir>
#include <QtTest>

using namespace EduLM;

namespace {

// Never answers by itself: the test decides when and how the check ends.
class FakeVerifier : public PasswordVerifier
{
public:
    void verify(const QString &user, const QString &password) override { calls.append({user, password}); }
    void finish(bool ok) { emit verified(ok); }

    QList<QPair<QString, QString>> calls;
};

// Everything the daemon can send, decoded; only the fields of `id` are meaningful.
struct Msg {
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
    QByteArray raw;
};

using Msgs = QList<Msg>;

quint32 id(EduDaemonMessage m) { return quint32(m); }

QList<quint32> ids(const Msgs &msgs)
{
    QList<quint32> out;
    for (const Msg &m : msgs)
        out << m.id;
    return out;
}

const QString wrongAnswer = "bogus";
const QString explanationSecret = "EXPLAIN-SECRET";
const QString numericKey = "1234567";

}

class QuizGateTest : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void nonQuizUserPassthrough();
    void exemptUserPassthrough();
    void disabledPassthrough();
    void noQuizConnectionFailsClosed();
    void wrongPassword();
    void fullPass();
    void fullFail();
    void loginDuringCooldown();
    void swapWorks();
    void swapRefusedNoSwapsLeft();
    void swapRefusedAlreadyAttempted();
    void swapRefusedNoMoreQuestions();
    void staleAndStrayMessagesIgnored();
    void abandonBeforeAnswer();
    void abandonAfterAnswer();
    void quizConnectionDropBeforeAnswer();
    void quizConnectionDropAfterAnswer();
    void loginSocketDropMidQuiz();
    void loginSocketDropBeforeAnswer();
    void loginSocketDropDuringVerification();
    void staleAnswerThenAbandonNoCooldown();
    void yearGroupsBrokenDuringVerification();
    void invalidConfig_data();
    void invalidConfig();
    void invalidConfigWithoutQuizConnection();
    void configReloadedAfterPasswordCheck();
    void packsReloadedAfterPasswordCheck();
    void poolTooSmall();
    void verificationInFlight();
    void quizInProgress();
    void statusQuery();
    void endEarly();
    void multiYearPools();
    void noAnswerKeyOnTheWire();
    void severalMessagesInOneStream();
    void unknownMessageNotConsumed();
    void lastHelloWins();
    void readyOnHello();
    void reservedUserRefusal();
    void abandonDuringVerification();
    void helloFromAnotherConnectionEndsQuiz();

private:
    // one client/server socket pair over a real local socket
    struct Pair {
        QLocalSocket *client = nullptr;
        QLocalSocket *server = nullptr;
    };

    Pair connectPair(const QString &label);
    void dispatch(QLocalSocket *socket);
    void writePack(const QString &name, const QString &subject, const QList<int> &years, int count,
                   const QString &prefix, bool numeric = false);
    void sendRaw(Pair &pair, const QByteArray &bytes);
    Msgs drain(Pair &pair);
    void hello();
    bool login(const QString &user = "alice", const QString &password = "pw", Pair *from = nullptr);
    Msgs startQuiz(const QString &user = "alice");
    Msgs answer(const QString &questionId, const QString &text);
    Msgs answerCurrent(bool right);
    Msgs runToEnd(bool right);
    Msgs swap(const QString &questionId);
    static Msg last(const Msgs &msgs, EduDaemonMessage message);
    QString currentId() const { return m_current.id; }
    QString label(QLocalSocket *socket) const { return m_labels.value(socket, "?"); }

    QLocalServer m_server;
    QTemporaryDir *m_packs = nullptr;
    QList<Pair> m_pairs;
    QHash<QLocalSocket *, QString> m_labels;
    QHash<QString, QString> m_key; // question id -> right answer
    QuizQuestionMsg m_current;
    QList<quint32> m_unknownIds;

    Pair loginPair;
    Pair quiz;

    FakeVerifier m_verifier;
    GateConfig cfg;
    QHash<QString, QStringList> m_groups;
    qint64 m_clockMs = 0;
    CooldownTracker *m_cooldown = nullptr;
    QRandomGenerator m_rng;
    QuizGate *m_gate = nullptr;

    QStringList events;
    QList<qint64> failPending; // quiz-socket bytes queued at the moment of each LoginFailed
};

void QuizGateTest::init()
{
    m_packs = new QTemporaryDir;
    failPending.clear();
    QVERIFY(m_packs->isValid());
    m_key.clear();
    writePack("maths4", "maths", {4}, 10, "m4");
    writePack("maths5", "maths", {5}, 10, "m5");
    writePack("english3", "english", {3}, 10, "e3");
    writePack("num", "num", {4}, 3, "n", true);
    writePack("tiny", "tiny", {4}, 3, "t");

    cfg = GateConfig();
    cfg.policy.groups = {"edu-students"};
    cfg.policy.exemptUsers = {"mum"};
    cfg.policy.yearGroups = {"edu-year3:3", "edu-year4:4", "edu-year5:5"};
    cfg.policy.defaultYear = 4;
    cfg.rules.questionCount = 3;
    cfg.rules.requiredCorrect = 2;
    cfg.rules.maxSwaps = 2;
    cfg.rules.answerAttempts = 1;
    cfg.rules.showCorrectAnswers = true;
    cfg.rules.endEarly = EndEarly::No;
    cfg.studentPasswords = true;
    cfg.cooldownSeconds = 60;
    cfg.questionDirs = {m_packs->path()};
    cfg.subjects = {"maths", "english"};

    m_groups = {
        {"alice", {"edu-students", "edu-year4"}},
        {"carol", {"edu-students", "edu-year4", "edu-year5"}},
        {"dave", {"edu-students", "edu-year3"}},
        {"mum", {"edu-students", "edu-year4"}},
        {"bob", {"users"}},
    };
    m_clockMs = 0;
    m_cooldown = new CooldownTracker([this] { return m_clockMs; });
    m_rng.seed(1234);
    m_verifier.calls.clear();
    events.clear();
    m_current = QuizQuestionMsg();
    m_unknownIds.clear();

    m_gate = new QuizGate(m_verifier, [this] { return cfg; },
                          [this](const QString &user) { return m_groups.value(user); }, *m_cooldown, m_rng);
    connect(m_gate, &QuizGate::loginFailed, this, [this](QLocalSocket *s) {
        events << "fail@" + label(s);
        failPending << (quiz.server ? quiz.server->bytesToWrite() : 0);
    });
    connect(m_gate, &QuizGate::informationMessage, this,
            [this](QLocalSocket *s, const QString &m) { events << "info@" + label(s) + ":" + m; });

    QLocalServer::removeServer("edu-gate-test");
    QVERIFY(m_server.listen("edu-gate-test"));
    loginPair = connectPair("login");
    quiz = connectPair("quiz");
}

void QuizGateTest::cleanup()
{
    for (const Pair &p : m_pairs) {
        p.client->disconnect();
        p.server->disconnect();
        delete p.client;
        delete p.server;
    }
    m_pairs.clear();
    m_labels.clear();
    loginPair = quiz = Pair();
    delete m_gate;
    m_gate = nullptr;
    delete m_cooldown;
    m_cooldown = nullptr;
    delete m_packs;
    m_packs = nullptr;
    m_server.close();
}

QuizGateTest::Pair QuizGateTest::connectPair(const QString &name)
{
    Pair pair;
    pair.client = new QLocalSocket;
    pair.client->connectToServer(m_server.fullServerName());
    if (!m_server.waitForNewConnection(2000))
        qFatal("no connection");
    pair.server = m_server.nextPendingConnection();
    if (!pair.client->waitForConnected(2000))
        qFatal("not connected");
    m_labels.insert(pair.server, name);
    connect(pair.server, &QLocalSocket::readyRead, this, [this, s = pair.server] { dispatch(s); });
    m_pairs.append(pair);
    return pair;
}

// What SocketServer::readyRead does: one message after another from the same stream.
void QuizGateTest::dispatch(QLocalSocket *socket)
{
    QDataStream in(socket);
    while (socket->bytesAvailable()) {
        quint32 message = 0;
        in >> message;
        if (!m_gate->handleMessage(socket, message, in)) {
            m_unknownIds << message;
            socket->readAll();
        }
    }
}

void QuizGateTest::writePack(const QString &name, const QString &subject, const QList<int> &years, int count,
                             const QString &prefix, bool numeric)
{
    QJsonArray yearArray;
    for (int y : years)
        yearArray.append(y);
    QJsonArray questions;
    for (int i = 0; i < count; ++i) {
        const QString qid = QString("%1-%2").arg(prefix).arg(i);
        QJsonObject q;
        q["id"] = qid;
        q["years"] = yearArray;
        q["prompt"] = "Prompt " + qid;
        q["explanation"] = explanationSecret;
        if (numeric) {
            q["type"] = "numeric";
            q["answer"] = numericKey.toInt();
            m_key[qid] = numericKey;
        } else {
            q["type"] = "multiple_choice";
            q["choices"] = QJsonArray{"right " + qid, "wrong " + qid};
            q["answer"] = 0;
            m_key[qid] = "0";
        }
        questions.append(q);
    }
    QJsonObject pack{{"schema", 1}, {"pack", name}, {"subject", subject}, {"questions", questions}};
    QFile f(m_packs->filePath(name + ".json"));
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(QJsonDocument(pack).toJson());
}

void QuizGateTest::sendRaw(Pair &pair, const QByteArray &bytes)
{
    pair.client->write(bytes);
    QVERIFY(pair.client->waitForBytesWritten(1000));
    QVERIFY(pair.server->waitForReadyRead(1000));
}

// The daemon answers a hello with QuizReady, and only that
void QuizGateTest::hello()
{
    sendRaw(quiz, encodeMessage(EduGreeterMessage::QuizHello));
    const QList<quint32> got = ids(drain(quiz));
    // a repeated hello on the same connection is harmless and gets no second reply
    QVERIFY(got.isEmpty() || got == QList<quint32>{id(EduDaemonMessage::QuizReady)});
}

Msgs QuizGateTest::drain(Pair &pair)
{
    pair.server->waitForBytesWritten(1000);
    QByteArray bytes;
    do {
        bytes += pair.client->readAll();
    } while (pair.client->waitForReadyRead(30));
    bytes += pair.client->readAll();

    Msgs msgs;
    QDataStream in(bytes);
    while (!in.atEnd()) {
        Msg m;
        const qint64 start = bytes.size() - in.device()->bytesAvailable();
        quint32 raw;
        in >> raw;
        m.id = raw;
        switch (EduDaemonMessage(raw)) {
        case EduDaemonMessage::QuizStatus: in >> m.status; break;
        case EduDaemonMessage::QuizStarted: in >> m.started; break;
        case EduDaemonMessage::QuizQuestion: in >> m.question; m_current = m.question; break;
        case EduDaemonMessage::QuizFeedback: in >> m.feedback; break;
        case EduDaemonMessage::QuizSwapRefused: in >> m.refused; break;
        case EduDaemonMessage::QuizResult: in >> m.result; break;
        case EduDaemonMessage::QuizCooldown: in >> m.cooldown; break;
        case EduDaemonMessage::QuizError: in >> m.error; break;
        case EduDaemonMessage::QuizReady: break;
        case EduDaemonMessage::QuizLoginRefused: in >> m.loginRefused; break;
        default: qFatal("unknown daemon message %x", raw);
        }
        if (in.status() != QDataStream::Ok)
            qFatal("undecodable message %x", raw);
        m.raw = bytes.mid(start, bytes.size() - in.device()->bytesAvailable() - start);
        msgs << m;
    }
    return msgs;
}

// True if the gate took the login over.
bool QuizGateTest::login(const QString &user, const QString &password, Pair *from)
{
    Pair &p = from ? *from : loginPair;
    // stands in for the continuation Display::login passes
    return m_gate->intercept(p.server, user, password,
                             [this, socket = p.server, user](const QString &pw) {
                                 events << "proceed@" + label(socket) + ":" + user + ":" + pw + ":plasma";
                             });
}

Msgs QuizGateTest::startQuiz(const QString &user)
{
    hello();
    if (!login(user))
        qFatal("login not intercepted");
    m_verifier.finish(true);
    return drain(quiz);
}

Msgs QuizGateTest::answer(const QString &questionId, const QString &text)
{
    sendRaw(quiz, encodeMessage(EduGreeterMessage::QuizAnswer, QuizAnswerMsg{questionId, text}));
    return drain(quiz);
}

Msgs QuizGateTest::answerCurrent(bool right)
{
    return answer(m_current.id, right ? m_key.value(m_current.id) : wrongAnswer);
}

Msgs QuizGateTest::runToEnd(bool right)
{
    Msgs all;
    for (int i = 0; i < 50; ++i) {
        all += answerCurrent(right);
        for (const Msg &m : all) {
            if (m.id == id(EduDaemonMessage::QuizResult))
                return all;
        }
    }
    qFatal("quiz never ended");
    return all;
}

Msgs QuizGateTest::swap(const QString &questionId)
{
    sendRaw(quiz, encodeMessage(EduGreeterMessage::QuizSwap, QuizSwapMsg{questionId}));
    return drain(quiz);
}

Msg QuizGateTest::last(const Msgs &msgs, EduDaemonMessage message)
{
    for (int i = msgs.size() - 1; i >= 0; --i) {
        if (msgs[i].id == id(message))
            return msgs[i];
    }
    qFatal("message not found");
    return Msg();
}

void QuizGateTest::nonQuizUserPassthrough()
{
    hello();
    QVERIFY(!login("bob"));
    QVERIFY(events.isEmpty());
    QVERIFY(m_verifier.calls.isEmpty());
    // the greeter is told "no quiz" so a later LoginFailed reads as a wrong password
    const Msgs bob = drain(quiz);
    QCOMPARE(ids(bob), QList<quint32>{id(EduDaemonMessage::QuizStatus)});
    QCOMPARE(bob[0].status.user, QString("bob"));
    QVERIFY(!bob[0].status.required);
    QCOMPARE(bob[0].status.questionCount, 3);
    // unknown users are not quiz users either
    QVERIFY(!login("nobody"));
    QVERIFY(events.isEmpty());
    QCOMPARE(ids(drain(quiz)), QList<quint32>{id(EduDaemonMessage::QuizStatus)});
}

void QuizGateTest::exemptUserPassthrough()
{
    hello();
    QVERIFY(!login("mum"));
    QVERIFY(events.isEmpty());
    QVERIFY(m_verifier.calls.isEmpty());
    QCOMPARE(ids(drain(quiz)), QList<quint32>{id(EduDaemonMessage::QuizStatus)});
}

void QuizGateTest::disabledPassthrough()
{
    cfg.policy.enabled = false;
    hello();
    QVERIFY(!login("alice"));
    QVERIFY(events.isEmpty());
    QVERIFY(m_verifier.calls.isEmpty());
    const Msgs off = drain(quiz);
    QCOMPARE(ids(off), QList<quint32>{id(EduDaemonMessage::QuizStatus)});
    QVERIFY(!off[0].status.required);
}

void QuizGateTest::noQuizConnectionFailsClosed()
{
    QVERIFY(login("alice"));
    QCOMPARE(events, (QStringList{"info@login:This account needs the Edu-LM login screen", "fail@login"}));
    QVERIFY(m_verifier.calls.isEmpty());
    QVERIFY(!m_gate->needsQuiz("bob"));
    QVERIFY(m_gate->needsQuiz("alice"));
}

void QuizGateTest::wrongPassword()
{
    hello();
    QVERIFY(login("alice", "nope"));
    QCOMPARE(m_verifier.calls.size(), 1);
    QCOMPARE(m_verifier.calls[0].first, QString("alice"));
    QCOMPARE(m_verifier.calls[0].second, QString("nope"));
    QVERIFY(events.isEmpty()); // nothing until PAM answers
    m_verifier.finish(false);
    QCOMPARE(events, QStringList{"fail@login"});
    QVERIFY2(failPending.last() > 0, "the verdict must be written before LoginFailed");
    const Msgs refused = drain(quiz);
    QCOMPARE(ids(refused), QList<quint32>{id(EduDaemonMessage::QuizLoginRefused)});
    QCOMPARE(refused[0].loginRefused.reason, QString("wrong-password"));
    QCOMPARE(m_cooldown->secondsLeft("alice"), 0);

    // the stash is gone: the next login is verified again instead of refused as busy
    QVERIFY(login("alice", "again"));
    QCOMPARE(m_verifier.calls.size(), 2);
    QCOMPARE(events, QStringList{"fail@login"});
}

void QuizGateTest::fullPass()
{
    const Msgs start = startQuiz();
    QCOMPARE(ids(start), (QList<quint32>{id(EduDaemonMessage::QuizStarted), id(EduDaemonMessage::QuizQuestion)}));
    QCOMPARE(start[0].started.user, QString("alice"));
    QCOMPARE(start[0].started.count, 3);
    QCOMPARE(start[0].started.required, 2);
    QCOMPARE(start[0].started.maxSwaps, 2);
    QCOMPARE(start[1].question.index, 0);
    QCOMPARE(start[1].question.count, 3);
    QCOMPARE(start[1].question.swapsLeft, 2);
    QCOMPARE(start[1].question.attemptsLeft, 1);
    QCOMPARE(start[1].question.type, QString("multiple_choice"));
    QCOMPARE(start[1].question.choices.size(), 2);
    QVERIFY(events.isEmpty()); // the login socket hears nothing during the quiz

    const Msgs rest = runToEnd(true);
    QCOMPARE(ids(rest), (QList<quint32>{
        id(EduDaemonMessage::QuizFeedback), id(EduDaemonMessage::QuizQuestion),
        id(EduDaemonMessage::QuizFeedback), id(EduDaemonMessage::QuizQuestion),
        id(EduDaemonMessage::QuizFeedback), id(EduDaemonMessage::QuizResult)}));
    QVERIFY(rest[0].feedback.correct);
    QVERIFY(rest[0].feedback.questionFinished);
    const Msg result = last(rest, EduDaemonMessage::QuizResult);
    QVERIFY(result.result.passed);
    QCOMPARE(result.result.correct, 3);
    QCOMPARE(result.result.required, 2);
    QCOMPARE(result.result.asked, 3);
    QVERIFY(result.result.review.isEmpty());
    QCOMPARE(result.result.cooldownSeconds, 0);

    // the login continues exactly once, with the original credentials
    QCOMPARE(events, QStringList{"proceed@login:alice:pw:plasma"});
    QCOMPARE(m_verifier.calls.size(), 1);

    // the approval is used up: the same user has to take the quiz again
    events.clear();
    QVERIFY(login("alice", "pw2"));
    QCOMPARE(m_verifier.calls.size(), 2);
    QVERIFY(events.isEmpty());
    m_verifier.finish(true);
    QCOMPARE(ids(drain(quiz)).first(), id(EduDaemonMessage::QuizStarted));
}

void QuizGateTest::fullFail()
{
    startQuiz();
    const Msgs rest = runToEnd(false);
    const Msg result = last(rest, EduDaemonMessage::QuizResult);
    QVERIFY(!result.result.passed);
    QCOMPARE(result.result.correct, 0);
    QCOMPARE(result.result.required, 2);
    QCOMPARE(result.result.asked, 3);
    QCOMPARE(result.result.cooldownSeconds, 60);
    QCOMPARE(result.result.review.size(), 3);
    for (const QuizReviewItem &item : result.result.review) {
        QVERIFY(item.prompt.startsWith("Prompt "));
        QCOMPARE(item.choices.size(), 2);
        QCOMPARE(item.givenAnswer, wrongAnswer);
        QVERIFY(item.correctAnswer.startsWith("right "));
        QCOMPARE(item.explanation, explanationSecret);
    }
    QCOMPARE(events, QStringList{"fail@login"});
    QCOMPARE(m_cooldown->secondsLeft("alice"), 60);
    QVERIFY(m_verifier.calls.size() == 1);
}

void QuizGateTest::loginDuringCooldown()
{
    startQuiz();
    runToEnd(false);
    events.clear();

    m_clockMs = 15000;
    QVERIFY(login("alice"));
    QCOMPARE(m_verifier.calls.size(), 1); // no new password check
    QCOMPARE(events, QStringList{"fail@login"});
    QVERIFY2(failPending.last() > 0, "the verdict must be written before LoginFailed");
    const Msgs msgs = drain(quiz);
    QCOMPARE(ids(msgs), QList<quint32>{id(EduDaemonMessage::QuizCooldown)});
    QCOMPARE(msgs[0].cooldown.seconds, 45);

    // other users are unaffected, and alice can retry when it ends
    m_clockMs = 60001;
    events.clear();
    QVERIFY(login("alice"));
    QCOMPARE(m_verifier.calls.size(), 2);
    QVERIFY(events.isEmpty());
}

void QuizGateTest::swapWorks()
{
    const Msgs start = startQuiz();
    const QString before = m_current.id;
    const Msgs msgs = swap(before);
    QCOMPARE(ids(msgs), QList<quint32>{id(EduDaemonMessage::QuizQuestion)});
    QVERIFY(msgs[0].question.id != before);
    QCOMPARE(msgs[0].question.index, 0);
    QCOMPARE(msgs[0].question.swapsLeft, 1);
    QVERIFY(events.isEmpty());
}

void QuizGateTest::swapRefusedNoSwapsLeft()
{
    cfg.rules.maxSwaps = 1;
    startQuiz();
    QCOMPARE(ids(swap(m_current.id)), QList<quint32>{id(EduDaemonMessage::QuizQuestion)});
    const Msgs msgs = swap(m_current.id);
    QCOMPARE(ids(msgs), QList<quint32>{id(EduDaemonMessage::QuizSwapRefused)});
    QCOMPARE(msgs[0].refused.reason, QString("no-swaps-left"));
}

void QuizGateTest::swapRefusedAlreadyAttempted()
{
    cfg.rules.answerAttempts = 2;
    startQuiz();
    const Msgs fb = answer(m_current.id, wrongAnswer);
    QCOMPARE(ids(fb), QList<quint32>{id(EduDaemonMessage::QuizFeedback)}); // same question stays
    QVERIFY(!fb[0].feedback.correct);
    QVERIFY(!fb[0].feedback.questionFinished);
    QCOMPARE(fb[0].feedback.attemptsLeft, 1);

    const Msgs msgs = swap(m_current.id);
    QCOMPARE(ids(msgs), QList<quint32>{id(EduDaemonMessage::QuizSwapRefused)});
    QCOMPARE(msgs[0].refused.reason, QString("already-attempted"));
}

void QuizGateTest::swapRefusedNoMoreQuestions()
{
    cfg.subjects = {"tiny"}; // exactly N entries
    startQuiz();
    const Msgs msgs = swap(m_current.id);
    QCOMPARE(ids(msgs), QList<quint32>{id(EduDaemonMessage::QuizSwapRefused)});
    QCOMPARE(msgs[0].refused.reason, QString("no-more-questions"));
}

void QuizGateTest::staleAndStrayMessagesIgnored()
{
    // no quiz running yet
    hello();
    QVERIFY(answer("m4-0", "0").isEmpty());
    QVERIFY(swap("m4-0").isEmpty());

    QVERIFY(login("alice"));
    m_verifier.finish(true);
    drain(quiz);
    const QString cur = m_current.id;

    QVERIFY(answer("no-such-question", "0").isEmpty());
    QVERIFY(swap("no-such-question").isEmpty());
    // from a connection that is not the quiz connection
    sendRaw(loginPair, encodeMessage(EduGreeterMessage::QuizAnswer, QuizAnswerMsg{cur, m_key.value(cur)}));
    sendRaw(loginPair, encodeMessage(EduGreeterMessage::QuizSwap, QuizSwapMsg{cur}));
    sendRaw(loginPair, encodeMessage(EduGreeterMessage::QuizAbandon));
    QVERIFY(drain(quiz).isEmpty());
    QVERIFY(events.isEmpty());

    // still running, and the ignored messages did not count as attempts or end it
    QCOMPARE(m_cooldown->secondsLeft("alice"), 0);
    QCOMPARE(ids(answer(cur, m_key.value(cur))).first(), id(EduDaemonMessage::QuizFeedback));
    sendRaw(quiz, encodeMessage(EduGreeterMessage::QuizAbandon));
    QCOMPARE(m_cooldown->secondsLeft("alice"), 60); // the accepted answer counts as an attempt
    QCOMPARE(events, QStringList{"fail@login"});
}

void QuizGateTest::abandonBeforeAnswer()
{
    startQuiz();
    swap(m_current.id); // swapping is not answering
    sendRaw(quiz, encodeMessage(EduGreeterMessage::QuizAbandon));
    QVERIFY(events.isEmpty());
    QCOMPARE(m_cooldown->secondsLeft("alice"), 0);
    QVERIFY(drain(quiz).isEmpty());

    // discarded: a new login is verified, not refused
    QVERIFY(login("alice"));
    QCOMPARE(m_verifier.calls.size(), 2);
    QVERIFY(events.isEmpty());
    // answers to the dead quiz go nowhere
    m_verifier.finish(false);
    events.clear();
}

void QuizGateTest::abandonAfterAnswer()
{
    startQuiz();
    answerCurrent(true);
    sendRaw(quiz, encodeMessage(EduGreeterMessage::QuizAbandon));
    QCOMPARE(events, QStringList{"fail@login"});
    QCOMPARE(m_cooldown->secondsLeft("alice"), 60);
    QVERIFY(drain(quiz).isEmpty());

    // and the quiz is gone
    QVERIFY(answer(m_current.id, "0").isEmpty());
}

void QuizGateTest::quizConnectionDropBeforeAnswer()
{
    startQuiz();
    quiz.client->disconnectFromServer();
    QTRY_VERIFY(quiz.server->state() != QLocalSocket::ConnectedState);
    QTest::qWait(20);
    QVERIFY(events.isEmpty());
    QCOMPARE(m_cooldown->secondsLeft("alice"), 0);

    // with no quiz connection the gate fails closed again
    QVERIFY(login("alice"));
    QCOMPARE(events, (QStringList{"info@login:This account needs the Edu-LM login screen", "fail@login"}));
}

void QuizGateTest::quizConnectionDropAfterAnswer()
{
    startQuiz();
    answerCurrent(false);
    quiz.client->disconnectFromServer();
    QTRY_COMPARE(events, QStringList{"fail@login"});
    QCOMPARE(m_cooldown->secondsLeft("alice"), 60);
}

void QuizGateTest::loginSocketDropMidQuiz()
{
    startQuiz();
    answerCurrent(true);
    loginPair.client->disconnectFromServer();
    QTRY_VERIFY(loginPair.server->state() != QLocalSocket::ConnectedState);
    QTest::qWait(20);
    QVERIFY(events.isEmpty()); // nobody left to tell, but the answer counts: cooldown
    QCOMPARE(m_cooldown->secondsLeft("alice"), 60);
    QVERIFY(drain(quiz).isEmpty());
    QVERIFY(answer(m_current.id, "0").isEmpty()); // quiz is gone

    m_clockMs = 61000; // cooldown over
    Pair other = connectPair("login2");
    QVERIFY(login("alice", "pw", &other));
    QCOMPARE(m_verifier.calls.size(), 2);
}

void QuizGateTest::loginSocketDropBeforeAnswer()
{
    startQuiz();
    loginPair.client->disconnectFromServer();
    QTRY_VERIFY(loginPair.server->state() != QLocalSocket::ConnectedState);
    QTest::qWait(20);
    QVERIFY(events.isEmpty());
    QCOMPARE(m_cooldown->secondsLeft("alice"), 0); // nothing answered: discarded
    QVERIFY(answer(m_current.id, "0").isEmpty());
}

void QuizGateTest::staleAnswerThenAbandonNoCooldown()
{
    startQuiz();
    QVERIFY(answer("no-such-question", "0").isEmpty());
    sendRaw(quiz, encodeMessage(EduGreeterMessage::QuizAbandon));
    QVERIFY(events.isEmpty());
    QCOMPARE(m_cooldown->secondsLeft("alice"), 0);
}

void QuizGateTest::yearGroupsBrokenDuringVerification()
{
    hello();
    QVERIFY(login("alice"));
    cfg.policy.yearGroups = {"edu-year4:99"};
    m_verifier.finish(true);
    const Msgs msgs = drain(quiz);
    QCOMPARE(ids(msgs), QList<quint32>{id(EduDaemonMessage::QuizError)});
    QVERIFY(msgs[0].error.message.contains("99"));
    QCOMPARE(events.size(), 2);
    QCOMPARE(events[1], QString("fail@login"));
    QVERIFY2(failPending.last() > 0, "the verdict must be written before LoginFailed");
}

void QuizGateTest::loginSocketDropDuringVerification()
{
    hello();
    QVERIFY(login("alice"));
    loginPair.client->disconnectFromServer();
    QTRY_VERIFY(loginPair.server->state() != QLocalSocket::ConnectedState);
    QTest::qWait(20);
    m_verifier.finish(true);
    QVERIFY(events.isEmpty());
    QVERIFY(drain(quiz).isEmpty()); // no quiz for a login nobody waits for
}

void QuizGateTest::invalidConfig_data()
{
    QTest::addColumn<int>("variant");
    QTest::addColumn<QString>("mentions");
    QTest::newRow("bad EndEarly") << 0 << "EndEarly";
    QTest::newRow("K greater than N") << 1 << "RequiredCorrect";
    QTest::newRow("DefaultYear 0") << 2 << "DefaultYear";
    QTest::newRow("DefaultYear 13") << 3 << "DefaultYear";
    QTest::newRow("malformed YearGroups") << 4 << "edu-year4";
    QTest::newRow("YearGroups year 13") << 5 << "13";
}

void QuizGateTest::invalidConfig()
{
    QFETCH(int, variant);
    QFETCH(QString, mentions);
    switch (variant) {
    case 0: cfg.rules.endEarly = EndEarly::Invalid; break;
    case 1: cfg.rules.requiredCorrect = 4; break;
    case 2: cfg.policy.defaultYear = 0; break;
    case 3: cfg.policy.defaultYear = 13; break;
    case 4: cfg.policy.yearGroups = {"edu-year4"}; break;
    case 5: cfg.policy.yearGroups = {"edu-year13:13"}; break;
    }
    hello();
    QVERIFY(login("alice"));
    QVERIFY(m_verifier.calls.isEmpty());
    const Msgs msgs = drain(quiz);
    QCOMPARE(ids(msgs), QList<quint32>{id(EduDaemonMessage::QuizError)});
    QVERIFY2(msgs[0].error.message.contains(mentions), qPrintable(msgs[0].error.message));
    QVERIFY2(failPending.last() > 0, "the verdict must be written before LoginFailed");
    QCOMPARE(events.size(), 2);
    QVERIFY(events[0].startsWith("info@login:"));
    QVERIFY(events[0].contains(msgs[0].error.message));
    QCOMPARE(events[1], QString("fail@login"));

    // non-quiz users are not affected by a broken quiz config
    events.clear();
    QVERIFY(!login("bob"));
}

void QuizGateTest::invalidConfigWithoutQuizConnection()
{
    cfg.rules.requiredCorrect = 9;
    QVERIFY(login("alice"));
    QCOMPARE(events.size(), 2);
    QVERIFY(events[0].contains("RequiredCorrect"));
    QVERIFY(!events[0].contains("needs the Edu-LM login screen"));
    QCOMPARE(events[1], QString("fail@login"));
}

void QuizGateTest::configReloadedAfterPasswordCheck()
{
    hello();
    QVERIFY(login("alice"));
    cfg.rules.questionCount = 4; // a parent edits the config while PAM is thinking
    cfg.rules.requiredCorrect = 3;
    m_verifier.finish(true);
    const Msgs msgs = drain(quiz);
    QCOMPARE(msgs[0].started.count, 4);
    QCOMPARE(msgs[0].started.required, 3);

    // ...and one that breaks the config is caught at the same point
    runToEnd(true);
    events.clear();
    QVERIFY(login("alice"));
    cfg.rules.requiredCorrect = 99;
    m_verifier.finish(true);
    const Msgs err = drain(quiz);
    QCOMPARE(ids(err), QList<quint32>{id(EduDaemonMessage::QuizError)});
    QCOMPARE(events.size(), 2);
    QCOMPARE(events[1], QString("fail@login"));
    // the stash was dropped
    cfg.rules.requiredCorrect = 3;
    QVERIFY(login("alice"));
    QCOMPARE(m_verifier.calls.size(), 3);
}

void QuizGateTest::packsReloadedAfterPasswordCheck()
{
    QFile::remove(m_packs->filePath("maths4.json")); // nothing to ask yet
    hello();
    QVERIFY(login("alice"));
    writePack("maths4", "maths", {4}, 10, "m4");
    m_verifier.finish(true);
    QCOMPARE(ids(drain(quiz)).first(), id(EduDaemonMessage::QuizStarted));
}

void QuizGateTest::poolTooSmall()
{
    cfg.subjects = {"nothing-here"};
    hello();
    QVERIFY(login("alice"));
    m_verifier.finish(true);
    const Msgs msgs = drain(quiz);
    QCOMPARE(ids(msgs), QList<quint32>{id(EduDaemonMessage::QuizError)});
    QVERIFY(msgs[0].error.message.contains("pool"));
    QCOMPARE(events.size(), 2);
    QVERIFY(events[0].startsWith("info@login:"));
    QCOMPARE(events[1], QString("fail@login"));
    QCOMPARE(m_cooldown->secondsLeft("alice"), 0);
    QVERIFY(login("alice")); // not stuck
    QCOMPARE(m_verifier.calls.size(), 2);
}

void QuizGateTest::verificationInFlight()
{
    hello();
    QVERIFY(login("alice"));
    Pair second = connectPair("login2");
    QVERIFY(login("alice", "other", &second));
    QCOMPARE(events, QStringList{"fail@login2"});
    QVERIFY2(failPending.last() > 0, "the verdict must be written before LoginFailed");
    QCOMPARE(m_verifier.calls.size(), 1);

    // the first login is unaffected
    m_verifier.finish(true);
    const Msgs msgs = drain(quiz);
    QCOMPARE(ids(msgs), (QList<quint32>{id(EduDaemonMessage::QuizLoginRefused), id(EduDaemonMessage::QuizStarted),
                                        id(EduDaemonMessage::QuizQuestion)}));
    QCOMPARE(msgs[0].loginRefused.reason, QString("busy"));
}

void QuizGateTest::quizInProgress()
{
    startQuiz();
    Pair second = connectPair("login2");
    QVERIFY(login("carol", "pw", &second));
    QCOMPARE(events, QStringList{"fail@login2"});
    QVERIFY2(failPending.last() > 0, "the verdict must be written before LoginFailed");
    QCOMPARE(m_verifier.calls.size(), 1);
    const Msgs busy = drain(quiz);
    QCOMPARE(ids(busy), QList<quint32>{id(EduDaemonMessage::QuizLoginRefused)});
    QCOMPARE(busy[0].loginRefused.reason, QString("busy"));
}

void QuizGateTest::statusQuery()
{
    hello();
    auto query = [this](const QString &user) {
        sendRaw(quiz, encodeMessage(EduGreeterMessage::QuizQuery, QuizQueryMsg{user}));
        const Msgs msgs = drain(quiz);
        Q_ASSERT(msgs.size() == 1);
        return msgs[0];
    };

    Msg m = query("alice");
    QCOMPARE(m.id, id(EduDaemonMessage::QuizStatus));
    QCOMPARE(m.status.user, QString("alice"));
    QVERIFY(m.status.required);
    QVERIFY(m.status.studentPasswords);
    QCOMPARE(m.status.cooldownSeconds, 0);
    QCOMPARE(m.status.questionCount, 3);
    QCOMPARE(m.status.requiredCorrect, 2);

    QVERIFY(!query("bob").status.required);
    QVERIFY(!query("mum").status.required);
    QVERIFY(!query("nobody").status.required);

    cfg.studentPasswords = false;
    QVERIFY(!query("alice").status.studentPasswords);

    m_cooldown->recordFailure("alice", 60);
    m_clockMs = 10000;
    QCOMPARE(query("alice").status.cooldownSeconds, 50);
    QCOMPARE(query("carol").status.cooldownSeconds, 0);

    cfg.policy.enabled = false;
    QVERIFY(!query("alice").status.required);

    // answered on the asking connection, even one that never said hello
    sendRaw(loginPair, encodeMessage(EduGreeterMessage::QuizQuery, QuizQueryMsg{"alice"}));
    const Msgs viaLogin = drain(loginPair);
    QCOMPARE(ids(viaLogin), QList<quint32>{id(EduDaemonMessage::QuizStatus)});
    QVERIFY(drain(quiz).isEmpty());
}

void QuizGateTest::endEarly()
{
    cfg.rules.questionCount = 5;
    cfg.rules.requiredCorrect = 3;
    cfg.rules.endEarly = EndEarly::Both;

    startQuiz();
    Msgs msgs;
    for (int i = 0; i < 3; ++i)
        msgs += answerCurrent(true);
    Msg result = last(msgs, EduDaemonMessage::QuizResult);
    QVERIFY(result.result.passed);
    QCOMPARE(result.result.asked, 3);
    QCOMPARE(events, QStringList{"proceed@login:alice:pw:plasma"});

    events.clear();
    m_clockMs += 1000;
    startQuiz();
    msgs.clear();
    for (int i = 0; i < 3; ++i)
        msgs += answerCurrent(false);
    result = last(msgs, EduDaemonMessage::QuizResult);
    QVERIFY(!result.result.passed);
    QCOMPARE(result.result.asked, 3);
    QCOMPARE(result.result.review.size(), 3);
    QCOMPARE(events, QStringList{"fail@login"});
}

void QuizGateTest::multiYearPools()
{
    cfg.rules.questionCount = 8;
    cfg.rules.requiredCorrect = 1;

    auto askedPrefixes = [this](const QString &user) {
        QSet<QString> prefixes;
        Msgs first = startQuiz(user);
        prefixes << m_current.id.left(2);
        for (int i = 0; i < 20 && !events.contains("proceed@login:" + user + ":pw:plasma"); ++i) {
            for (const Msg &m : answerCurrent(true)) {
                if (m.id == id(EduDaemonMessage::QuizQuestion))
                    prefixes << m.question.id.left(2);
            }
        }
        events.clear();
        return prefixes;
    };

    // two year groups: the union of both pools
    QCOMPARE(askedPrefixes("carol"), (QSet<QString>{"m4", "m5"}));
    // a different year gets its own subject and year
    QCOMPARE(askedPrefixes("dave"), (QSet<QString>{"e3"}));
    // no year group: DefaultYear
    m_groups["erin"] = {"edu-students"};
    QCOMPARE(askedPrefixes("erin"), (QSet<QString>{"m4"}));
}

void QuizGateTest::noAnswerKeyOnTheWire()
{
    cfg.subjects = {"num"}; // numeric questions, key 1234567
    cfg.rules.answerAttempts = 2;
    Msgs all = startQuiz();
    all += answer(m_current.id, wrongAnswer); // wrong with a retry left
    all += swap(m_current.id);                // refused: already attempted
    all += answerCurrent(false);
    all += runToEnd(false);

    auto utf16 = [](const QString &s) {
        return QByteArray(reinterpret_cast<const char *>(s.utf16()), s.size() * 2);
    };

    int checked = 0;
    for (const Msg &m : all) {
        if (m.id != id(EduDaemonMessage::QuizQuestion) && m.id != id(EduDaemonMessage::QuizFeedback))
            continue;
        ++checked;
        QVERIFY(!m.raw.contains(utf16(explanationSecret)));
        QVERIFY(!m.raw.contains(utf16(numericKey)));
        QVERIFY(!m.raw.contains(numericKey.toUtf8()));
        if (m.id == id(EduDaemonMessage::QuizQuestion))
            QVERIFY(m.question.choices.isEmpty());
    }
    QVERIFY(checked >= 6);

    // the failed result does carry the key, in the review only
    const Msg result = last(all, EduDaemonMessage::QuizResult);
    QVERIFY(!result.result.passed);
    QCOMPARE(QString(result.result.review.first().correctAnswer).remove(QChar(0xA0)), numericKey); // formatted with no-break spaces

    // a passed quiz carries no key and no review at all
    m_clockMs += 61000;
    events.clear();
    const Msgs pass = startQuiz() + runToEnd(true);
    QVERIFY(last(pass, EduDaemonMessage::QuizResult).result.review.isEmpty());
    for (const Msg &m : pass)
        QVERIFY(!m.raw.contains(utf16(explanationSecret)));
}

void QuizGateTest::severalMessagesInOneStream()
{
    hello();
    QVERIFY(login("alice"));
    m_verifier.finish(true);
    drain(quiz);
    const QString cur = m_current.id;

    QByteArray burst = encodeMessage(EduGreeterMessage::QuizQuery, QuizQueryMsg{"alice"})
                       + encodeMessage(EduGreeterMessage::QuizAnswer, QuizAnswerMsg{cur, m_key.value(cur)})
                       + encodeMessage(EduGreeterMessage::QuizHello)
                       + encodeMessage(EduGreeterMessage::QuizQuery, QuizQueryMsg{"bob"});
    sendRaw(quiz, burst);
    const Msgs msgs = drain(quiz);
    QCOMPARE(ids(msgs), (QList<quint32>{id(EduDaemonMessage::QuizStatus), id(EduDaemonMessage::QuizFeedback),
                                        id(EduDaemonMessage::QuizQuestion), id(EduDaemonMessage::QuizStatus)}));
    QVERIFY(msgs[1].feedback.correct);
    QVERIFY(msgs[2].question.id != cur);
    QCOMPARE(msgs[3].status.user, QString("bob"));
    QVERIFY(m_unknownIds.isEmpty());
}

void QuizGateTest::unknownMessageNotConsumed()
{
    QDataStream dummy;
    QVERIFY(!m_gate->handleMessage(loginPair.server, 0x1234, dummy));
    QVERIFY(!m_gate->handleMessage(loginPair.server, 3, dummy)); // an upstream id
    // and through the dispatcher, ahead of a message the gate does own
    sendRaw(loginPair, encodeMessage(EduDaemonMessage::QuizError, QuizErrorMsg{"x"}));
    QCOMPARE(m_unknownIds, QList<quint32>{id(EduDaemonMessage::QuizError)});
}

void QuizGateTest::readyOnHello()
{
    // the first reply to a hello is QuizReady, and nothing is sent before it
    sendRaw(quiz, encodeMessage(EduGreeterMessage::QuizHello));
    QCOMPARE(ids(drain(quiz)), QList<quint32>{id(EduDaemonMessage::QuizReady)});
    // a stock theme never says hello: the login is refused with no quiz-socket traffic at all
    Pair other = connectPair("stock");
    QVERIFY(m_gate->needsQuiz("alice"));
    QVERIFY(drain(other).isEmpty());
}

void QuizGateTest::reservedUserRefusal()
{
    m_gate->refuseReservedUser(); // no quiz connection: nothing to write, nothing breaks
    hello();
    m_gate->refuseReservedUser();
    const Msgs msgs = drain(quiz);
    QCOMPARE(ids(msgs), QList<quint32>{id(EduDaemonMessage::QuizLoginRefused)});
    QCOMPARE(msgs[0].loginRefused.reason, QString("refused"));
}

// Giving up during the password check discards the login: a late "ok" starts nothing
void QuizGateTest::abandonDuringVerification()
{
    hello();
    QVERIFY(login("alice"));
    sendRaw(quiz, encodeMessage(EduGreeterMessage::QuizAbandon));
    m_verifier.finish(true);
    QVERIFY(drain(quiz).isEmpty());
    QVERIFY(events.isEmpty()); // no quiz, no proceed
    // and the gate is usable again
    QVERIFY(login("alice"));
    QCOMPARE(m_verifier.calls.size(), 2);
    m_verifier.finish(true);
    QCOMPARE(ids(drain(quiz)).mid(0, 1), QList<quint32>{id(EduDaemonMessage::QuizStarted)});
}

void QuizGateTest::lastHelloWins()
{
    hello();
    Pair second = connectPair("quiz2");
    sendRaw(second, encodeMessage(EduGreeterMessage::QuizHello));
    sendRaw(second, encodeMessage(EduGreeterMessage::QuizHello)); // harmless twice

    QVERIFY(login("alice"));
    m_verifier.finish(true);
    QVERIFY(drain(quiz).isEmpty());
    // one QuizReady for the two hellos, then the quiz
    QCOMPARE(ids(drain(second)).mid(0, 2), (QList<quint32>{id(EduDaemonMessage::QuizReady), id(EduDaemonMessage::QuizStarted)}));

    // the old connection can neither answer nor abandon
    sendRaw(quiz, encodeMessage(EduGreeterMessage::QuizAbandon));
    quiz.client->disconnectFromServer();
    QTest::qWait(50);
    QVERIFY(events.isEmpty());
    const Msgs fb = [&] {
        sendRaw(second, encodeMessage(EduGreeterMessage::QuizAnswer,
                                      QuizAnswerMsg{m_current.id, wrongAnswer}));
        return drain(second);
    }();
    QVERIFY(!fb.isEmpty());
}

void QuizGateTest::helloFromAnotherConnectionEndsQuiz()
{
    startQuiz();
    answerCurrent(true);
    Pair second = connectPair("quiz2");
    sendRaw(second, encodeMessage(EduGreeterMessage::QuizHello));
    // the running quiz had an answer: counted as failed
    QCOMPARE(events, QStringList{"fail@login"});
    QCOMPARE(m_cooldown->secondsLeft("alice"), 60);
}

QTEST_MAIN(QuizGateTest)
#include "QuizGateTest.moc"
