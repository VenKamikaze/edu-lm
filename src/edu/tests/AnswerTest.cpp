// SPDX-License-Identifier: GPL-2.0-or-later
#include "core/Answer.h"

#include <QtTest>

using namespace EduLM;

class AnswerTest : public QObject
{
    Q_OBJECT

private slots:
    void formatNumber_data();
    void formatNumber();
    void numeric_data();
    void numeric();
    void numericTolerance();
    void multipleChoice();
    void trueFalse();
    void shortText();
    void answerText();
};

static Question numericQuestion(double answer, double tolerance = 0)
{
    Question q;
    q.type = QuestionType::Numeric;
    q.answerNumber = answer;
    q.tolerance = tolerance;
    return q;
}

void AnswerTest::formatNumber_data()
{
    QTest::addColumn<qint64>("value");
    QTest::addColumn<QString>("text");
    QTest::newRow("small") << qint64(7) << "7";
    QTest::newRow("four digits") << qint64(4527) << "4527";
    QTest::newRow("five digits") << qint64(14527) << QString::fromUtf8("14\u00A0527");
    QTest::newRow("six digits") << qint64(123456) << QString::fromUtf8("123\u00A0456");
    QTest::newRow("seven digits") << qint64(1234567) << QString::fromUtf8("1\u00A0234\u00A0567");
    QTest::newRow("negative") << qint64(-3) << "-3";
    QTest::newRow("negative big") << qint64(-14527) << QString::fromUtf8("-14\u00A0527");
}

void AnswerTest::formatNumber()
{
    QFETCH(qint64, value);
    QFETCH(QString, text);
    QCOMPARE(EduLM::formatNumber(value), text);
}

void AnswerTest::numeric_data()
{
    QTest::addColumn<QString>("input");
    QTest::addColumn<bool>("correct");

    QTest::newRow("plain") << "4527" << true;
    QTest::newRow("space group") << "4 527" << true;
    QTest::newRow("comma group") << "4,527" << true;
    QTest::newRow("padded") << "  4527 \n" << true;
    QTest::newRow("nbsp group") << QString::fromUtf8("4 527") << true;
    QTest::newRow("wrong number") << "4528" << false;
    QTest::newRow("empty") << "" << false;
    QTest::newRow("words") << "four thousand" << false;
    QTest::newRow("nan") << "nan" << false;
    QTest::newRow("inf") << "inf" << false;
    QTest::newRow("exponent") << "4.527e3" << false;
    QTest::newRow("bad grouping") << "45 27" << false;
    QTest::newRow("mixed separators") << "1,234 567" << false;
    QTest::newRow("comma decimal is not accepted") << "4,5" << false;
    QTest::newRow("grouped decimal") << "4,527.0" << true;
    QTest::newRow("trailing junk") << "4527x" << false;
    QTest::newRow("two numbers") << "4527 4527" << false;
}

void AnswerTest::numeric()
{
    QFETCH(QString, input);
    QFETCH(bool, correct);
    QCOMPARE(isCorrect(numericQuestion(4527), input), correct);
}

void AnswerTest::numericTolerance()
{
    QVERIFY(isCorrect(numericQuestion(-3), "-3"));
    QVERIFY(isCorrect(numericQuestion(-3), QString::fromUtf8("−3")));
    QVERIFY(isCorrect(numericQuestion(2.5), "2.5"));
    QVERIFY(isCorrect(numericQuestion(2.5), " 2.50 "));
    QVERIFY(!isCorrect(numericQuestion(2.5), "3"));
    QVERIFY(isCorrect(numericQuestion(12), " 12 "));
    QVERIFY(isCorrect(numericQuestion(10, 0.5), "10.5"));
    QVERIFY(isCorrect(numericQuestion(10, 0.5), "9.5"));
    QVERIFY(!isCorrect(numericQuestion(10, 0.5), "10.6"));
    QVERIFY(!isCorrect(numericQuestion(10), "10.1"));
}

void AnswerTest::multipleChoice()
{
    Question q;
    q.type = QuestionType::MultipleChoice;
    q.choices = {"a", "b", "c"};
    q.answerIndex = 1;
    QVERIFY(isCorrect(q, "1"));
    QVERIFY(isCorrect(q, " 1 "));
    QVERIFY(!isCorrect(q, "0"));
    QVERIFY(!isCorrect(q, "-1"));
    QVERIFY(!isCorrect(q, "3"));
    QVERIFY(!isCorrect(q, "b"));
    QVERIFY(!isCorrect(q, "1.0"));
    QVERIFY(!isCorrect(q, ""));
}

void AnswerTest::trueFalse()
{
    Question q;
    q.type = QuestionType::TrueFalse;
    q.answerBool = false;
    QVERIFY(isCorrect(q, "false"));
    QVERIFY(isCorrect(q, " FALSE "));
    QVERIFY(!isCorrect(q, "true"));
    QVERIFY(!isCorrect(q, "yes"));
    QVERIFY(!isCorrect(q, ""));
    q.answerBool = true;
    QVERIFY(isCorrect(q, "True"));
    QVERIFY(!isCorrect(q, "0"));
}

void AnswerTest::shortText()
{
    Question q;
    q.type = QuestionType::ShortText;
    q.accept = {"Gravity", "the  force of gravity"};
    QVERIFY(isCorrect(q, "gravity"));
    QVERIFY(isCorrect(q, "  GRAVITY\t"));
    QVERIFY(isCorrect(q, "The force   of Gravity"));
    QVERIFY(!isCorrect(q, "gravit"));
    QVERIFY(!isCorrect(q, "gravity gravity"));
    QVERIFY(!isCorrect(q, ""));
    QVERIFY(!isCorrect(q, "   "));
    QVERIFY(!isCorrect(q, "..."));

    // stray punctuation around the answer is ignored
    QVERIFY(isCorrect(q, "gravity."));
    QVERIFY(isCorrect(q, " \"Gravity\"! "));
    QVERIFY(isCorrect(q, "The force of gravity."));
    QVERIFY(!isCorrect(q, "gra.vity"));
}

void AnswerTest::answerText()
{
    Question mc;
    mc.type = QuestionType::MultipleChoice;
    mc.choices = {"a", "b"};
    mc.answerIndex = 1;
    QCOMPARE(correctAnswerText(mc), QString("b"));
    QCOMPARE(displayAnswer(mc, "0"), QString("a"));
    QCOMPARE(displayAnswer(mc, "9"), QString("9"));

    Question tf;
    tf.type = QuestionType::TrueFalse;
    tf.answerBool = true;
    QCOMPARE(correctAnswerText(tf), QString("True"));
    QCOMPARE(displayAnswer(tf, "false"), QString("False"));

    QCOMPARE(correctAnswerText(numericQuestion(14527)), QString::fromUtf8("14\u00A0527"));
    QCOMPARE(correctAnswerText(numericQuestion(2.5)), QString("2.5"));
}

QTEST_APPLESS_MAIN(AnswerTest)
#include "AnswerTest.moc"
