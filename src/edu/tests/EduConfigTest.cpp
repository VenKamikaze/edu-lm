// SPDX-License-Identifier: GPL-2.0-or-later
#include "EduConfiguration.h"

#include <QtTest>

using namespace EduLM;

class EduConfigTest : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void defaults();
    void parsesFile();
    void dropInOverridesFile();
    void convertsToRulesAndPolicy();
    void badEndEarlyIsAProblem();

private:
    static QString confFile() { return QStringLiteral(EDU_CONFIG_FILE); }
    static QString confDir() { return QStringLiteral(EDU_CONFIG_DIR); }
    void writeFile(const QString &path, const QByteArray &content);
};

void EduConfigTest::init()
{
    // paths come from the test-only EduConstants.h
    QDir(confDir()).removeRecursively();
    QVERIFY(QDir().mkpath(confDir()));
    QFile::remove(confFile());
}

void EduConfigTest::writeFile(const QString &path, const QByteArray &content)
{
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(content);
}

static const QByteArray sampleConfig = R"(
[Quiz]
Enabled=true
Groups=edu-students, kids     # trailing comment
Users=bob
ExemptUsers=dad,mum
StudentPasswords=false
QuestionCount=6
RequiredCorrect=5
MaxSwaps=2
ShowCorrectAnswers=false
FailCooldownSeconds=120
AnswerAttempts=2
EndEarly= Both

[Content]
QuestionDirs=/srv/packs,/srv/more
Subjects=maths,english
YearGroups=y5:5,y3:3
DefaultYear=3
)";

void EduConfigTest::defaults()
{
    EduConfig c;
    QVERIFY(c.Quiz.Enabled.get());
    QCOMPARE(c.Quiz.Groups.get(), QStringList{"edu-students"});
    QVERIFY(c.Quiz.Users.get().isEmpty());
    QVERIFY(c.Quiz.StudentPasswords.get());
    QCOMPARE(c.Quiz.QuestionCount.get(), 5);
    QCOMPARE(c.Quiz.RequiredCorrect.get(), 4);
    QCOMPARE(c.Quiz.MaxSwaps.get(), 3);
    QVERIFY(c.Quiz.ShowCorrectAnswers.get());
    QCOMPARE(c.Quiz.FailCooldownSeconds.get(), 60);
    QCOMPARE(c.Quiz.AnswerAttempts.get(), 1);
    QCOMPARE(c.Content.Subjects.get(), (QStringList{"maths", "science", "hass", "english"}));
    QCOMPARE(c.Content.YearGroups.get(), (QStringList{"edu-year3:3", "edu-year4:4", "edu-year5:5"}));
    QCOMPARE(c.Content.DefaultYear.get(), 4);
    QCOMPARE(c.Content.QuestionDirs.get().size(), 2);
    QVERIFY(c.Content.QuestionDirs.get()[0].endsWith("edu-lm/questions"));
    QVERIFY(c.Quiz.QuestionCount.isDefault());
    QCOMPARE(c.Quiz.EndEarly.get(), QString("no"));
    QCOMPARE(quizRules(c).endEarly, EndEarly::No);
    QVERIFY(quizRules(c).validate().isEmpty());
}

void EduConfigTest::parsesFile()
{
    writeFile(confFile(), sampleConfig);
    EduConfig c;
    QCOMPARE(c.Quiz.Groups.get(), (QStringList{"edu-students", "kids"}));
    QCOMPARE(c.Quiz.Users.get(), QStringList{"bob"});
    QCOMPARE(c.Quiz.ExemptUsers.get(), (QStringList{"dad", "mum"}));
    QVERIFY(!c.Quiz.StudentPasswords.get());
    QCOMPARE(c.Quiz.QuestionCount.get(), 6);
    QCOMPARE(c.Quiz.RequiredCorrect.get(), 5);
    QCOMPARE(c.Quiz.MaxSwaps.get(), 2);
    QVERIFY(!c.Quiz.ShowCorrectAnswers.get());
    QCOMPARE(c.Quiz.FailCooldownSeconds.get(), 120);
    QCOMPARE(c.Quiz.AnswerAttempts.get(), 2);
    QCOMPARE(c.Content.QuestionDirs.get(), (QStringList{"/srv/packs", "/srv/more"}));
    QCOMPARE(c.Content.Subjects.get(), (QStringList{"maths", "english"}));
    QCOMPARE(c.Content.YearGroups.get(), (QStringList{"y5:5", "y3:3"}));
    QCOMPARE(c.Content.DefaultYear.get(), 3);
}

void EduConfigTest::dropInOverridesFile()
{
    writeFile(confFile(), "[Quiz]\nQuestionCount=6\nMaxSwaps=2\n");
    writeFile(confDir() + "/10-site.conf", "[Quiz]\nQuestionCount=8\nAnswerAttempts=3\n");
    EduConfig c;
    // the main file wins over the drop-in directory, as in SDDM
    QCOMPARE(c.Quiz.QuestionCount.get(), 6);
    QCOMPARE(c.Quiz.AnswerAttempts.get(), 3);
    QCOMPARE(c.Quiz.MaxSwaps.get(), 2);
}

void EduConfigTest::convertsToRulesAndPolicy()
{
    writeFile(confFile(), sampleConfig);
    EduConfig c;
    const QuizRules rules = quizRules(c);
    QCOMPARE(rules.questionCount, 6);
    QCOMPARE(rules.requiredCorrect, 5);
    QCOMPARE(rules.maxSwaps, 2);
    QCOMPARE(rules.answerAttempts, 2);
    QVERIFY(!rules.showCorrectAnswers);
    QCOMPARE(rules.endEarly, EndEarly::Both);
    QVERIFY(rules.validate().isEmpty());

    const QuizPolicyConfig policy = quizPolicyConfig(c);
    QVERIFY(decideQuiz("alice", {"kids"}, policy).quizRequired);
    QVERIFY(decideQuiz("bob", {"bob"}, policy).quizRequired);
    QVERIFY(!decideQuiz("dad", {"kids"}, policy).quizRequired);
    QCOMPARE(decideQuiz("alice", {"kids", "y3", "y5"}, policy).years, (QList<int>{3, 5}));
    QCOMPARE(decideQuiz("alice", {"kids"}, policy).years, (QList<int>{3}));
}

void EduConfigTest::badEndEarlyIsAProblem()
{
    writeFile(confFile(), "[Quiz]\nEndEarly=sometimes\n");
    EduConfig c;
    const QuizRules rules = quizRules(c);
    QCOMPARE(rules.endEarly, EduLM::EndEarly::Invalid);
    QVERIFY(rules.validate().join(' ').contains("EndEarly"));
}

QTEST_APPLESS_MAIN(EduConfigTest)
#include "EduConfigTest.moc"
