// SPDX-License-Identifier: GPL-2.0-or-later
#include "core/Answer.h"
#include "core/Generators.h"

#include <QtTest>

using namespace EduLM;

class GeneratorsTest : public QObject
{
    Q_OBJECT

private slots:
    void deterministic();
    void differentSeedsDiffer();
    void subtractNeverNegative();
    void divideIsExact();
    void addAndMultiply();
    void placeValue();
    void instanceIds();
    void unresolvedPlaceValueTerminates();
    void paramValidation_data();
    void paramValidation();
};

static Question entry(const QString &generator, QMap<QString, IntRange> params = {})
{
    Question q;
    q.id = "gen-" + generator;
    q.subject = "maths";
    q.type = QuestionType::Generator;
    q.generator = generator;
    q.params = params;
    const QString err = resolveGeneratorParams(generator, &q.params);
    Q_ASSERT(err.isEmpty());
    Q_UNUSED(err);
    return q;
}

static const QStringList allGenerators = {"add", "subtract", "multiply", "divide", "place_value"};

void GeneratorsTest::deterministic()
{
    for (const QString &g : allGenerators) {
        QRandomGenerator a(42), b(42);
        for (int i = 0; i < 20; ++i) {
            const Question x = instantiateGenerator(entry(g), a);
            const Question y = instantiateGenerator(entry(g), b);
            QCOMPARE(x.id, y.id);
            QCOMPARE(x.prompt, y.prompt);
            QCOMPARE(x.answerNumber, y.answerNumber);
        }
    }
}

void GeneratorsTest::differentSeedsDiffer()
{
    QRandomGenerator a(1), b(2);
    QSet<QString> ids;
    for (int i = 0; i < 10; ++i) {
        ids.insert(instantiateGenerator(entry("multiply"), a).id);
        ids.insert(instantiateGenerator(entry("multiply"), b).id);
    }
    QVERIFY(ids.size() > 5);
}

void GeneratorsTest::subtractNeverNegative()
{
    QRandomGenerator rng(7);
    // b's range sits above a's, so the operands must be swapped
    const Question e = entry("subtract", {{"a", {1, 5}}, {"b", {10, 20}}});
    for (int i = 0; i < 500; ++i) {
        const Question q = instantiateGenerator(e, rng);
        QVERIFY2(q.answerNumber >= 0, qPrintable(q.id));
        const QStringList ops = q.id.section('#', 1).split('-');
        QCOMPARE(q.answerNumber, ops[0].toDouble() - ops[1].toDouble());
    }
}

void GeneratorsTest::divideIsExact()
{
    QRandomGenerator rng(9);
    const Question e = entry("divide", {{"quotient", {0, 12}}, {"divisor", {1, 12}}});
    for (int i = 0; i < 500; ++i) {
        const Question q = instantiateGenerator(e, rng);
        const QStringList ops = q.id.section('#', 1).split('/');
        const qint64 dividend = ops[0].toLongLong(), divisor = ops[1].toLongLong();
        QVERIFY(divisor >= 1 && divisor <= 12);
        QCOMPARE(dividend % divisor, qint64(0));
        QCOMPARE(q.answerNumber, double(dividend / divisor));
        QVERIFY(q.answerNumber >= 0 && q.answerNumber <= 12);
    }
}

void GeneratorsTest::addAndMultiply()
{
    QRandomGenerator rng(3);
    const Question add = entry("add", {{"a", {5, 8}}, {"b", {20, 30}}});
    const Question mul = entry("multiply", {{"a", {3, 4}}, {"b", {6, 7}}});
    for (int i = 0; i < 200; ++i) {
        Question q = instantiateGenerator(add, rng);
        QStringList ops = q.id.section('#', 1).split('+');
        QVERIFY(ops[0].toInt() >= 5 && ops[0].toInt() <= 8 && ops[1].toInt() >= 20 && ops[1].toInt() <= 30);
        QCOMPARE(q.answerNumber, ops[0].toDouble() + ops[1].toDouble());
        QVERIFY(isCorrect(q, QString::number(q.answerNumber)));

        q = instantiateGenerator(mul, rng);
        ops = q.id.section('#', 1).split('x');
        QVERIFY(ops[0].toInt() >= 3 && ops[0].toInt() <= 4 && ops[1].toInt() >= 6 && ops[1].toInt() <= 7);
        QCOMPARE(q.answerNumber, ops[0].toDouble() * ops[1].toDouble());
    }
}

void GeneratorsTest::placeValue()
{
    QRandomGenerator rng(11);
    const QRegularExpression prompt("^What is the value of the (\\d) in (.+)\\?$");
    for (int digits = 2; digits <= 7; ++digits) {
        const Question e = entry("place_value", {{"digits", {digits, digits}}});
        for (int i = 0; i < 200; ++i) {
            const Question q = instantiateGenerator(e, rng);
            const auto m = prompt.match(q.prompt);
            QVERIFY2(m.hasMatch(), qPrintable(q.prompt));
            const QString digit = m.captured(1);
            const QString number = QString(m.captured(2)).remove(QChar(0x00A0));
            QCOMPARE(number.size(), digits);
            QVERIFY(number[0] != '0');
            QCOMPARE(number.count(digit), 1);
            const int pos = number.indexOf(digit);
            QCOMPARE(q.answerNumber, digit.toDouble() * std::pow(10, digits - 1 - pos));
            QCOMPARE(q.type, QuestionType::Numeric);
        }
    }
}

void GeneratorsTest::instanceIds()
{
    QRandomGenerator rng(5);
    const Question q = instantiateGenerator(entry("multiply", {{"a", {7, 7}}, {"b", {8, 8}}}), rng);
    QCOMPARE(q.id, QString("gen-multiply#7x8"));
    QCOMPARE(q.prompt, QString::fromUtf8("What is 7 × 8?"));
    QCOMPARE(q.subject, QString("maths"));
    QCOMPARE(q.answerNumber, 56.0);

    const Question big = instantiateGenerator(entry("add", {{"a", {14000, 14000}}, {"b", {527, 527}}}), rng);
    QCOMPARE(big.prompt, QString::fromUtf8("What is 14\u00A0000 + 527?"));
    QCOMPARE(correctAnswerText(big), QString::fromUtf8("14\u00A0527"));

    const Question sub = instantiateGenerator(entry("subtract", {{"a", {9, 9}}, {"b", {4, 4}}}), rng);
    QCOMPARE(sub.prompt, QString::fromUtf8("What is 9 \u2212 4?"));
}

void GeneratorsTest::unresolvedPlaceValueTerminates()
{
    QRandomGenerator rng(1);
    Question raw;
    raw.id = "raw";
    raw.type = QuestionType::Generator;
    raw.generator = "place_value";
    QVERIFY(instantiateGenerator(raw, rng).prompt.startsWith("What is the value"));
    raw.params["digits"] = {0, 0};
    QVERIFY(instantiateGenerator(raw, rng).prompt.startsWith("What is the value"));
}

void GeneratorsTest::paramValidation_data()
{
    QTest::addColumn<QString>("generator");
    QTest::addColumn<QString>("param");
    QTest::addColumn<int>("lo");
    QTest::addColumn<int>("hi");
    QTest::addColumn<bool>("valid");
    QTest::newRow("ok") << "add" << "a" << 1 << 10 << true;
    QTest::newRow("reversed") << "add" << "a" << 10 << 1 << false;
    QTest::newRow("negative") << "add" << "a" << -1 << 10 << false;
    QTest::newRow("too big") << "multiply" << "a" << 1 << 1000000 << false;
    QTest::newRow("unknown param") << "add" << "c" << 1 << 2 << false;
    QTest::newRow("divisor zero") << "divide" << "divisor" << 0 << 5 << false;
    QTest::newRow("quotient zero ok") << "divide" << "quotient" << 0 << 5 << true;
    QTest::newRow("one digit") << "place_value" << "digits" << 1 << 3 << false;
    QTest::newRow("too many digits") << "place_value" << "digits" << 2 << 8 << false;
    QTest::newRow("digits ok") << "place_value" << "digits" << 2 << 7 << true;
    QTest::newRow("unknown generator") << "power" << "a" << 1 << 2 << false;
}

void GeneratorsTest::paramValidation()
{
    QFETCH(QString, generator);
    QFETCH(QString, param);
    QFETCH(int, lo);
    QFETCH(int, hi);
    QFETCH(bool, valid);
    QMap<QString, IntRange> params{{param, {lo, hi}}};
    QCOMPARE(resolveGeneratorParams(generator, &params).isEmpty(), valid);
}

QTEST_APPLESS_MAIN(GeneratorsTest)
#include "GeneratorsTest.moc"
