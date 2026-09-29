// SPDX-License-Identifier: GPL-2.0-or-later
#include "protocol/EduProtocol.h"

#include <QtTest>

using namespace EduLM;

class EduProtocolTest : public QObject
{
    Q_OBJECT

private slots:
    void idsAreDistinctAndClearOfUpstream();
    void greeterPayloads();
    void status();
    void started();
    void loginRefusedAndReady();
    void question();
    void feedback();
    void swapRefused();
    void resultWithReview();
    void resultWithoutReview();
    void cooldownAndError();
    void noPayloadMessages();
    void backToBack();
    void truncatedPayloadSetsStatus();
};

template<typename T>
static T decode(const QByteArray &bytes, quint32 expectedId)
{
    QDataStream in(bytes);
    quint32 id = 0;
    in >> id;
    T payload;
    in >> payload;
    if (id != expectedId || in.status() != QDataStream::Ok || !in.atEnd())
        QTest::qFail("message did not decode exactly", __FILE__, __LINE__);
    return payload;
}

void EduProtocolTest::idsAreDistinctAndClearOfUpstream()
{
    const QList<quint32> ids = {
        quint32(EduGreeterMessage::QuizHello), quint32(EduGreeterMessage::QuizQuery),
        quint32(EduGreeterMessage::QuizAnswer), quint32(EduGreeterMessage::QuizSwap),
        quint32(EduGreeterMessage::QuizAbandon), quint32(EduDaemonMessage::QuizStatus),
        quint32(EduDaemonMessage::QuizStarted), quint32(EduDaemonMessage::QuizQuestion),
        quint32(EduDaemonMessage::QuizFeedback), quint32(EduDaemonMessage::QuizSwapRefused),
        quint32(EduDaemonMessage::QuizResult), quint32(EduDaemonMessage::QuizCooldown),
        quint32(EduDaemonMessage::QuizError), quint32(EduDaemonMessage::QuizReady),
        quint32(EduDaemonMessage::QuizLoginRefused)};
    QCOMPARE(QSet<quint32>(ids.begin(), ids.end()).size(), ids.size());
    for (quint32 id : ids)
        QVERIFY(id >= 0x45440000); // upstream enums are small integers
}

void EduProtocolTest::greeterPayloads()
{
    const auto query = decode<QuizQueryMsg>(encodeMessage(EduGreeterMessage::QuizQuery, QuizQueryMsg{"alice"}),
                                            quint32(EduGreeterMessage::QuizQuery));
    QCOMPARE(query.user, QString("alice"));

    const auto answer = decode<QuizAnswerMsg>(
        encodeMessage(EduGreeterMessage::QuizAnswer, QuizAnswerMsg{"q-1", "forty two"}),
        quint32(EduGreeterMessage::QuizAnswer));
    QCOMPARE(answer.questionId, QString("q-1"));
    QCOMPARE(answer.answer, QString("forty two"));

    const auto swap = decode<QuizSwapMsg>(encodeMessage(EduGreeterMessage::QuizSwap, QuizSwapMsg{"q-9"}),
                                          quint32(EduGreeterMessage::QuizSwap));
    QCOMPARE(swap.questionId, QString("q-9"));
}

void EduProtocolTest::status()
{
    QuizStatusMsg in;
    in.user = "bob";
    in.required = true;
    in.studentPasswords = false;
    in.cooldownSeconds = 42;
    in.questionCount = 6;
    in.requiredCorrect = 5;
    const auto out = decode<QuizStatusMsg>(encodeMessage(EduDaemonMessage::QuizStatus, in),
                                           quint32(EduDaemonMessage::QuizStatus));
    QCOMPARE(out.user, in.user);
    QCOMPARE(out.required, true);
    QCOMPARE(out.studentPasswords, false);
    QCOMPARE(out.cooldownSeconds, 42);
    QCOMPARE(out.questionCount, 6);
    QCOMPARE(out.requiredCorrect, 5);
}

void EduProtocolTest::loginRefusedAndReady()
{
    const auto refused = decode<QuizLoginRefusedMsg>(
        encodeMessage(EduDaemonMessage::QuizLoginRefused, QuizLoginRefusedMsg{"wrong-password"}),
        quint32(EduDaemonMessage::QuizLoginRefused));
    QCOMPARE(refused.reason, QString("wrong-password"));
    // QuizReady has no payload
    QCOMPARE(encodeMessage(EduDaemonMessage::QuizReady).size(), 4);
}

void EduProtocolTest::started()
{
    const auto out = decode<QuizStartedMsg>(
        encodeMessage(EduDaemonMessage::QuizStarted, QuizStartedMsg{"bob", 5, 4, 3}),
        quint32(EduDaemonMessage::QuizStarted));
    QCOMPARE(out.user, QString("bob"));
    QCOMPARE(out.count, 5);
    QCOMPARE(out.required, 4);
    QCOMPARE(out.maxSwaps, 3);
}

void EduProtocolTest::question()
{
    QuizQuestionMsg in;
    in.id = "m-1";
    in.index = 2;
    in.count = 5;
    in.prompt = "6 × 7?";
    in.type = "multiple_choice";
    in.choices = {"36", "42"};
    in.swapsLeft = 3;
    in.attemptsLeft = 2;
    const auto out = decode<QuizQuestionMsg>(encodeMessage(EduDaemonMessage::QuizQuestion, in),
                                             quint32(EduDaemonMessage::QuizQuestion));
    QCOMPARE(out.id, in.id);
    QCOMPARE(out.index, 2);
    QCOMPARE(out.count, 5);
    QCOMPARE(out.prompt, in.prompt);
    QCOMPARE(out.type, in.type);
    QCOMPARE(out.choices, in.choices);
    QCOMPARE(out.swapsLeft, 3);
    QCOMPARE(out.attemptsLeft, 2);
}

void EduProtocolTest::feedback()
{
    const auto out = decode<QuizFeedbackMsg>(
        encodeMessage(EduDaemonMessage::QuizFeedback, QuizFeedbackMsg{"q", true, 1, false}),
        quint32(EduDaemonMessage::QuizFeedback));
    QCOMPARE(out.id, QString("q"));
    QCOMPARE(out.correct, true);
    QCOMPARE(out.attemptsLeft, 1);
    QCOMPARE(out.questionFinished, false);
}

void EduProtocolTest::swapRefused()
{
    const auto out = decode<QuizSwapRefusedMsg>(
        encodeMessage(EduDaemonMessage::QuizSwapRefused, QuizSwapRefusedMsg{"no-swaps-left"}),
        quint32(EduDaemonMessage::QuizSwapRefused));
    QCOMPARE(out.reason, QString("no-swaps-left"));
}

void EduProtocolTest::resultWithReview()
{
    QuizResultMsg in;
    in.passed = false;
    in.correct = 2;
    in.required = 4;
    in.asked = 5;
    in.review = {{"p1", {"a", "b"}, "a", "b", "because"}, {"p2", {}, "7", "8", ""}};
    in.cooldownSeconds = 60;
    const auto out = decode<QuizResultMsg>(encodeMessage(EduDaemonMessage::QuizResult, in),
                                           quint32(EduDaemonMessage::QuizResult));
    QCOMPARE(out.passed, false);
    QCOMPARE(out.correct, 2);
    QCOMPARE(out.required, 4);
    QCOMPARE(out.asked, 5);
    QCOMPARE(out.cooldownSeconds, 60);
    QCOMPARE(out.review.size(), 2);
    QCOMPARE(out.review[0].prompt, QString("p1"));
    QCOMPARE(out.review[0].choices, (QStringList{"a", "b"}));
    QCOMPARE(out.review[0].givenAnswer, QString("a"));
    QCOMPARE(out.review[0].correctAnswer, QString("b"));
    QCOMPARE(out.review[0].explanation, QString("because"));
    QCOMPARE(out.review[1].givenAnswer, QString("7"));
    QCOMPARE(out.review[1].correctAnswer, QString("8"));
}

void EduProtocolTest::resultWithoutReview()
{
    QuizResultMsg in;
    in.passed = true;
    in.correct = 5;
    in.required = 4;
    in.asked = 5;
    const auto out = decode<QuizResultMsg>(encodeMessage(EduDaemonMessage::QuizResult, in),
                                           quint32(EduDaemonMessage::QuizResult));
    QCOMPARE(out.passed, true);
    QVERIFY(out.review.isEmpty());
    QCOMPARE(out.cooldownSeconds, 0);
}

void EduProtocolTest::cooldownAndError()
{
    QCOMPARE(decode<QuizCooldownMsg>(encodeMessage(EduDaemonMessage::QuizCooldown, QuizCooldownMsg{17}),
                                     quint32(EduDaemonMessage::QuizCooldown)).seconds, 17);
    QCOMPARE(decode<QuizErrorMsg>(encodeMessage(EduDaemonMessage::QuizError, QuizErrorMsg{"bad config"}),
                                  quint32(EduDaemonMessage::QuizError)).message, QString("bad config"));
}

void EduProtocolTest::noPayloadMessages()
{
    for (auto id : {EduGreeterMessage::QuizHello, EduGreeterMessage::QuizAbandon}) {
        const QByteArray bytes = encodeMessage(id);
        QCOMPARE(bytes.size(), 4);
        QDataStream in(bytes);
        quint32 read = 0;
        in >> read;
        QCOMPARE(read, quint32(id));
        QVERIFY(in.atEnd());
    }
}

void EduProtocolTest::backToBack()
{
    QuizResultMsg result;
    result.review = {{"p", {"x"}, "x", "y", "e"}};
    QByteArray buffer = encodeMessage(EduGreeterMessage::QuizHello)
                        + encodeMessage(EduGreeterMessage::QuizAnswer, QuizAnswerMsg{"q", "1"})
                        + encodeMessage(EduDaemonMessage::QuizResult, result)
                        + encodeMessage(EduGreeterMessage::QuizAbandon)
                        + encodeMessage(EduGreeterMessage::QuizSwap, QuizSwapMsg{"q2"});

    QDataStream in(buffer);
    quint32 id;
    in >> id;
    QCOMPARE(id, quint32(EduGreeterMessage::QuizHello));
    in >> id;
    QCOMPARE(id, quint32(EduGreeterMessage::QuizAnswer));
    QuizAnswerMsg answer;
    in >> answer;
    QCOMPARE(answer.answer, QString("1"));
    in >> id;
    QCOMPARE(id, quint32(EduDaemonMessage::QuizResult));
    QuizResultMsg parsed;
    in >> parsed;
    QCOMPARE(parsed.review.size(), 1);
    QCOMPARE(parsed.review[0].correctAnswer, QString("y"));
    in >> id;
    QCOMPARE(id, quint32(EduGreeterMessage::QuizAbandon));
    in >> id;
    QCOMPARE(id, quint32(EduGreeterMessage::QuizSwap));
    QuizSwapMsg swap;
    in >> swap;
    QCOMPARE(swap.questionId, QString("q2"));
    QVERIFY(in.atEnd());
    QCOMPARE(in.status(), QDataStream::Ok);
}

void EduProtocolTest::truncatedPayloadSetsStatus()
{
    QByteArray bytes = encodeMessage(EduDaemonMessage::QuizQuestion, QuizQuestionMsg{"q", 0, 1, "p", "t", {}, 0, 1});
    bytes.chop(3);
    QDataStream in(bytes);
    quint32 id;
    in >> id;
    QuizQuestionMsg msg;
    in >> msg;
    QVERIFY(in.status() != QDataStream::Ok);
}

QTEST_APPLESS_MAIN(EduProtocolTest)
#include "EduProtocolTest.moc"
