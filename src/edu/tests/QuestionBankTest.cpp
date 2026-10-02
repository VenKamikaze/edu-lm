// SPDX-License-Identifier: GPL-2.0-or-later
#include "core/QuestionBank.h"

#include <QtTest>

using namespace EduLM;

class QuestionBankTest : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void loadsAllTypes();
    void subjectInheritance();
    void generatorParamsResolved();
    void rejectsBadQuestion_data();
    void rejectsBadQuestion();
    void badQuestionDoesNotStopOthers();
    void duplicateIdAcrossPacks();
    void badFiles();
    void warnings();
    void loadsDirectoriesAndSkipsOthers();
    void filterAndCounts();
    void shippedPacksAreClean();
    void examplePackIsClean();

private:
    QString write(const QString &name, const QByteArray &content);
    QString pack(const QString &questions, const QString &header = R"("pack": "t", "subject": "maths",)");

    QTemporaryDir m_dir;
};

void QuestionBankTest::init()
{
    QVERIFY(m_dir.isValid());
    for (const QString &f : QDir(m_dir.path()).entryList(QDir::Files))
        QFile::remove(m_dir.filePath(f));
}

QString QuestionBankTest::write(const QString &name, const QByteArray &content)
{
    QFile f(m_dir.filePath(name));
    const bool opened = f.open(QIODevice::WriteOnly);
    Q_ASSERT(opened);
    Q_UNUSED(opened);
    f.write(content);
    f.close();
    return f.fileName();
}

QString QuestionBankTest::pack(const QString &questions, const QString &header)
{
    return QStringLiteral(R"({"schema": 1, %1 "questions": [%2]})").arg(header, questions);
}

void QuestionBankTest::loadsAllTypes()
{
    const QString path = write("all.json", pack(R"(
        {"id": "mc", "years": [3, 4], "type": "multiple_choice", "prompt": "P?", "choices": ["a", "b", "c"],
         "answer": 2, "curriculum": "AC9M3N01", "difficulty": 2, "explanation": "Because"},
        {"id": "tf", "years": [3], "type": "true_false", "prompt": "P?", "answer": true},
        {"id": "num", "years": [5], "type": "numeric", "prompt": "P?", "answer": 2.5, "tolerance": 0.1},
        {"id": "num0", "years": [5], "type": "numeric", "prompt": "P?", "answer": 7},
        {"id": "st", "years": [4], "type": "short_text", "prompt": "P?", "accept": ["x", "y z"]},
        {"id": "gen", "years": [3], "type": "generator", "generator": "add", "params": {"a": [1, 5]}}
    )").toUtf8());

    QuestionBank bank;
    bank.load({path});
    QVERIFY2(bank.errors().isEmpty(), qPrintable(bank.errors().value(0).toString()));
    QCOMPARE(bank.questions().size(), 6);

    const Question mc = bank.questions()[0];
    QCOMPARE(mc.type, QuestionType::MultipleChoice);
    QCOMPARE(mc.years, (QList<int>{3, 4}));
    QCOMPARE(mc.choices, (QStringList{"a", "b", "c"}));
    QCOMPARE(mc.answerIndex, 2);
    QCOMPARE(mc.curriculum, QString("AC9M3N01"));
    QCOMPARE(mc.difficulty, 2);
    QCOMPARE(mc.explanation, QString("Because"));
    QCOMPARE(mc.pack, QString("t"));

    QVERIFY(bank.questions()[1].answerBool);
    QCOMPARE(bank.questions()[2].answerNumber, 2.5);
    QCOMPARE(bank.questions()[2].tolerance, 0.1);
    QCOMPARE(bank.questions()[3].tolerance, 0.0);
    QCOMPARE(bank.questions()[4].accept, (QStringList{"x", "y z"}));
    QCOMPARE(bank.questions()[5].type, QuestionType::Generator);
}

void QuestionBankTest::subjectInheritance()
{
    const QString path = write("s.json", pack(R"(
        {"id": "a", "years": [3], "type": "true_false", "prompt": "P?", "answer": true},
        {"id": "b", "years": [3], "subject": "Science", "type": "true_false", "prompt": "P?", "answer": true}
    )").toUtf8());
    QuestionBank bank;
    bank.load({path});
    QCOMPARE(bank.questions()[0].subject, QString("maths"));
    QCOMPARE(bank.questions()[1].subject, QString("science"));

    // no subject anywhere is an error
    QuestionBank none;
    none.load({write("n.json", pack(R"({"id": "a", "years": [3], "type": "true_false", "prompt": "P?", "answer": true})", "").toUtf8())});
    QCOMPARE(none.questions().size(), 0);
    QVERIFY(none.errors().value(0).message.contains("subject"));
}

void QuestionBankTest::generatorParamsResolved()
{
    QuestionBank bank;
    bank.load({write("g.json", pack(R"({"id": "g", "years": [3], "type": "generator", "generator": "add", "params": {"a": [1, 5]}})").toUtf8())});
    QCOMPARE(bank.questions().size(), 1);
    QCOMPARE(bank.questions()[0].params["a"].hi, 5);
    QVERIFY(bank.questions()[0].params.contains("b")); // default filled in
}

void QuestionBankTest::rejectsBadQuestion_data()
{
    QTest::addColumn<QString>("question");
    QTest::addColumn<QString>("expected");

    const QString mc = R"({"id": "q", "years": [3], "type": "multiple_choice", "prompt": "P?", "choices": %1, "answer": %2})";
    QTest::newRow("mc answer too big") << mc.arg(R"(["a","b"])", "2") << "answer";
    QTest::newRow("mc answer negative") << mc.arg(R"(["a","b"])", "-1") << "answer";
    QTest::newRow("mc answer not int") << mc.arg(R"(["a","b"])", "0.5") << "answer";
    QTest::newRow("mc answer string") << mc.arg(R"(["a","b"])", "\"0\"") << "answer";
    QTest::newRow("mc one choice") << mc.arg(R"(["a"])", "0") << "at least 2 choices";
    QTest::newRow("mc no choices") << mc.arg("[]", "0") << "at least 2 choices";
    QTest::newRow("mc duplicate choices") << mc.arg(R"(["a","b","a"])", "0") << "duplicate choice";
    QTest::newRow("mc duplicate after trim") << mc.arg(R"(["a","b "," b"])", "0") << "duplicate choice";
    QTest::newRow("mc empty choice") << mc.arg(R"(["a",""])", "0") << "empty choice";
    QTest::newRow("empty prompt") << R"({"id": "q", "years": [3], "type": "true_false", "prompt": "  ", "answer": true})" << "empty prompt";
    QTest::newRow("missing prompt") << R"({"id": "q", "years": [3], "type": "numeric", "answer": 1})" << "empty prompt";
    QTest::newRow("unknown type") << R"({"id": "q", "years": [3], "type": "essay", "prompt": "P?"})" << "unknown type";
    QTest::newRow("missing type") << R"({"id": "q", "years": [3], "prompt": "P?"})" << "missing type";
    QTest::newRow("unknown generator") << R"({"id": "q", "years": [3], "type": "generator", "generator": "power"})" << "unknown generator";
    QTest::newRow("years missing") << R"({"id": "q", "type": "true_false", "prompt": "P?", "answer": true})" << "years";
    QTest::newRow("years empty") << R"({"id": "q", "years": [], "type": "true_false", "prompt": "P?", "answer": true})" << "years";
    QTest::newRow("year zero") << R"({"id": "q", "years": [0], "type": "true_false", "prompt": "P?", "answer": true})" << "bad year";
    QTest::newRow("year too big") << R"({"id": "q", "years": [3, 13], "type": "true_false", "prompt": "P?", "answer": true})" << "bad year";
    QTest::newRow("year string") << R"({"id": "q", "years": ["3"], "type": "true_false", "prompt": "P?", "answer": true})" << "bad year";
    QTest::newRow("year fraction") << R"({"id": "q", "years": [3.5], "type": "true_false", "prompt": "P?", "answer": true})" << "bad year";
    QTest::newRow("missing id") << R"({"years": [3], "type": "true_false", "prompt": "P?", "answer": true})" << "missing id";
    QTest::newRow("tf answer not bool") << R"({"id": "q", "years": [3], "type": "true_false", "prompt": "P?", "answer": "true"})" << "true or false";
    QTest::newRow("numeric answer string") << R"({"id": "q", "years": [3], "type": "numeric", "prompt": "P?", "answer": "4"})" << "number";
    QTest::newRow("negative tolerance") << R"({"id": "q", "years": [3], "type": "numeric", "prompt": "P?", "answer": 4, "tolerance": -1})" << "tolerance";
    QTest::newRow("short_text no accept") << R"({"id": "q", "years": [3], "type": "short_text", "prompt": "P?"})" << "accept";
    QTest::newRow("short_text empty accept") << R"({"id": "q", "years": [3], "type": "short_text", "prompt": "P?", "accept": [" "]})" << "empty accepted";
    QTest::newRow("param reversed") << R"({"id": "q", "years": [3], "type": "generator", "generator": "add", "params": {"a": [9, 1]}})" << "greater than max";
    QTest::newRow("param negative") << R"({"id": "q", "years": [3], "type": "generator", "generator": "add", "params": {"a": [-1, 1]}})" << "must be within";
    QTest::newRow("param wrong shape") << R"({"id": "q", "years": [3], "type": "generator", "generator": "add", "params": {"a": [1]}})" << "[min, max]";
    QTest::newRow("param unknown") << R"({"id": "q", "years": [3], "type": "generator", "generator": "add", "params": {"z": [1, 2]}})" << "unknown param";
    QTest::newRow("divisor zero") << R"({"id": "q", "years": [3], "type": "generator", "generator": "divide", "params": {"divisor": [0, 2]}})" << "must be within";
}

void QuestionBankTest::rejectsBadQuestion()
{
    QFETCH(QString, question);
    QFETCH(QString, expected);
    QuestionBank bank;
    const QString path = write("bad.json", pack(question).toUtf8());
    bank.load({path});
    QCOMPARE(bank.questions().size(), 0);
    QCOMPARE(bank.errors().size(), 1);
    const LoadIssue e = bank.errors()[0];
    QVERIFY2(e.message.contains(expected), qPrintable(e.message));
    QCOMPARE(e.file, path);
    QVERIFY(e.toString().contains(path));
}

void QuestionBankTest::badQuestionDoesNotStopOthers()
{
    const QString path = write("mixed.json", pack(R"(
        {"id": "good1", "years": [3], "type": "true_false", "prompt": "P?", "answer": true},
        {"id": "bad", "years": [3], "type": "multiple_choice", "prompt": "P?", "choices": ["a"], "answer": 0},
        {"years": [3], "type": "true_false", "prompt": "P?", "answer": true},
        {"id": "good2", "years": [3], "type": "true_false", "prompt": "P?", "answer": false}
    )").toUtf8());
    QuestionBank bank;
    bank.load({path});
    QCOMPARE(bank.questions().size(), 2);
    QCOMPARE(bank.questions()[1].id, QString("good2"));
    QCOMPARE(bank.errors().size(), 2);
    QCOMPARE(bank.errors()[0].question, QString("bad"));
    QCOMPARE(bank.errors()[1].question, QString("#2"));
}

void QuestionBankTest::duplicateIdAcrossPacks()
{
    const QByteArray q = pack(R"({"id": "same", "years": [3], "type": "true_false", "prompt": "P?", "answer": true})").toUtf8();
    write("a.json", q);
    write("b.json", q);
    QuestionBank bank;
    bank.load({m_dir.path()});
    QCOMPARE(bank.questions().size(), 1);
    QCOMPARE(bank.errors().size(), 1);
    QVERIFY(bank.errors()[0].message.contains("duplicate id"));
    QVERIFY(bank.errors()[0].file.endsWith("b.json"));

    // duplicates inside one file too
    QuestionBank one;
    one.load({write("c.json", pack(R"(
        {"id": "x", "years": [3], "type": "true_false", "prompt": "P?", "answer": true},
        {"id": "x", "years": [3], "type": "true_false", "prompt": "Q?", "answer": true})").toUtf8())});
    QCOMPARE(one.questions().size(), 1);
    QCOMPARE(one.errors().size(), 1);
}

void QuestionBankTest::badFiles()
{
    QuestionBank bank;
    bank.load({write("garbage.json", "{not json"),
               write("array.json", "[1, 2]"),
               write("schema2.json", R"({"schema": 2, "questions": []})"),
               write("noschema.json", R"({"questions": []})"),
               write("noquestions.json", R"({"schema": 1})"),
               write("good.json", pack(R"({"id": "ok", "years": [3], "type": "true_false", "prompt": "P?", "answer": true})").toUtf8())});
    QCOMPARE(bank.errors().size(), 5);
    QVERIFY(bank.errors()[0].message.contains("invalid JSON"));
    QVERIFY(bank.errors()[1].message.contains("object"));
    QVERIFY(bank.errors()[2].message.contains("schema"));
    QVERIFY(bank.errors()[3].message.contains("schema"));
    QVERIFY(bank.errors()[4].message.contains("questions"));
    QCOMPARE(bank.questions().size(), 1);

    QuestionBank missing;
    missing.loadFile(m_dir.filePath("does-not-exist.json"));
    QCOMPARE(missing.errors().size(), 1);
}

void QuestionBankTest::warnings()
{
    QuestionBank bank;
    bank.load({write("w.json", pack(R"({"id": "q", "years": [3], "type": "true_false", "prompt": "P?", "answer": true, "anwser": 1})", R"("subject": "maths", "colour": "red",)").toUtf8()),
               m_dir.filePath("nope")});
    QVERIFY(bank.errors().isEmpty());
    QCOMPARE(bank.questions().size(), 1);
    QStringList messages;
    for (const LoadIssue &w : bank.warnings())
        messages << w.message;
    const QString all = messages.join('|');
    QVERIFY(all.contains("unknown field 'anwser'"));
    QVERIFY(all.contains("missing 'pack'"));
    QVERIFY(all.contains("unknown pack field 'colour'"));
    QVERIFY(all.contains("no such file or directory"));

    QTemporaryDir empty;
    QuestionBank e;
    e.load({empty.path()});
    QCOMPARE(e.warnings().size(), 1);
    QVERIFY(e.warnings()[0].message.contains("no .json"));
}

void QuestionBankTest::loadsDirectoriesAndSkipsOthers()
{
    const QByteArray q = R"({"id": "%1", "years": [3], "type": "true_false", "prompt": "P?", "answer": true})";
    write("one.json", pack(QString::fromUtf8(q).arg("one")).toUtf8());
    write("two.json", pack(QString::fromUtf8(q).arg("two")).toUtf8());
    write("notes.txt", "not a pack");
    QuestionBank bank;
    bank.load({m_dir.path()});
    QCOMPARE(bank.questions().size(), 2);
    QVERIFY(bank.errors().isEmpty());
}

void QuestionBankTest::filterAndCounts()
{
    write("f.json", pack(R"(
        {"id": "m3", "years": [3], "type": "true_false", "prompt": "P?", "answer": true},
        {"id": "m34", "years": [3, 4], "type": "true_false", "prompt": "P?", "answer": true},
        {"id": "s3", "years": [3], "subject": "science", "type": "true_false", "prompt": "P?", "answer": true},
        {"id": "h4", "years": [4], "subject": "hass", "type": "true_false", "prompt": "P?", "answer": true})").toUtf8());
    QuestionBank bank;
    bank.load({m_dir.path()});

    const auto ids = [&](const QList<int> &years, const QStringList &subjects) {
        QStringList out;
        for (const Question &q : bank.filter(years, subjects))
            out << q.id;
        return out;
    };
    QCOMPARE(ids({3}, {"maths"}), (QStringList{"m3", "m34"}));
    QCOMPARE(ids({3}, {"maths", "science"}), (QStringList{"m3", "m34", "s3"}));
    QCOMPARE(ids({4}, {"maths", "science", "hass"}), (QStringList{"m34", "h4"}));
    QCOMPARE(ids({5}, {"maths"}), QStringList());
    QCOMPARE(ids({3}, QStringList()), QStringList());
    QCOMPARE(ids({3}, {" Maths "}), (QStringList{"m3", "m34"}));

    // several years: any overlap qualifies, and no question is listed twice
    QCOMPARE(ids({3, 4}, {"maths"}), (QStringList{"m3", "m34"}));
    QCOMPARE(ids({4, 5}, {"maths"}), (QStringList{"m34"}));
    QCOMPARE(ids({4, 5}, {"maths", "hass"}), (QStringList{"m34", "h4"}));
    QCOMPARE(ids(QList<int>(), {"maths"}), QStringList());

    const auto counts = bank.counts();
    QCOMPARE(counts["maths"][3], 2);
    QCOMPARE(counts["maths"][4], 1);
    QCOMPARE(counts["science"][3], 1);
    QCOMPARE(counts["hass"][4], 1);
}

void QuestionBankTest::shippedPacksAreClean()
{
    QuestionBank bank;
    bank.load({EDU_SAMPLE_DIR});
    for (const LoadIssue &e : bank.errors())
        qWarning() << e.toString();
    QVERIFY(bank.errors().isEmpty());
    QVERIFY(bank.warnings().isEmpty());
    const auto counts = bank.counts();
    for (const QString subject : {"maths", "science", "hass", "english"}) {
        QVERIFY2(counts.contains(subject), qPrintable(subject));
        for (int year : {3, 4, 5})
            QVERIFY2(counts[subject].value(year) >= 2, qPrintable(subject + QString::number(year)));
    }
}

// docs/QUESTIONS.md shows this pack, so it must load with no errors or warnings.
void QuestionBankTest::examplePackIsClean()
{
    QuestionBank bank;
    bank.load({EDU_EXAMPLE_PACK});
    for (const LoadIssue &i : bank.errors() + bank.warnings())
        qWarning() << i.toString();
    QVERIFY(bank.errors().isEmpty());
    QVERIFY(bank.warnings().isEmpty());

    QSet<int> types;
    for (const Question &q : bank.questions())
        types.insert(static_cast<int>(q.type));
    QCOMPARE(types.size(), 5);
    QCOMPARE(bank.questions().size(), 5);
}

QTEST_APPLESS_MAIN(QuestionBankTest)
#include "QuestionBankTest.moc"
