// SPDX-License-Identifier: GPL-2.0-or-later
#include "core/QuizPolicy.h"

#include <QtTest>
#include <pwd.h>
#include <unistd.h>

using namespace EduLM;

class QuizPolicyTest : public QObject
{
    Q_OBJECT

private slots:
    void groupMembership();
    void explicitUser();
    void exemptOverrides();
    void disabled();
    void yearMapping();
    void yearFirstMatchWins();
    void defaultYear();
    void badYearGroups();
    void currentUserGroups();
    void unknownUserGroups();
};

static QuizPolicyConfig config()
{
    QuizPolicyConfig c;
    c.groups = {"edu-students"};
    c.yearGroups = {"edu-year3:3", "edu-year4:4", "edu-year5:5"};
    c.defaultYear = 4;
    return c;
}

void QuizPolicyTest::groupMembership()
{
    QVERIFY(decideQuiz("alice", {"alice", "edu-students"}, config()).quizRequired);
    QVERIFY(!decideQuiz("dad", {"dad", "wheel"}, config()).quizRequired);
    QVERIFY(!decideQuiz("dad", {}, config()).quizRequired);
    auto c = config();
    c.groups = {"other", "edu-students"};
    QVERIFY(decideQuiz("alice", {"edu-students"}, c).quizRequired);
}

void QuizPolicyTest::explicitUser()
{
    auto c = config();
    c.users = {"bob"};
    QVERIFY(decideQuiz("bob", {"bob"}, c).quizRequired);
    QVERIFY(!decideQuiz("carol", {"carol"}, c).quizRequired);
}

void QuizPolicyTest::exemptOverrides()
{
    auto c = config();
    c.users = {"bob"};
    c.exemptUsers = {"alice", "bob"};
    QVERIFY(!decideQuiz("alice", {"edu-students"}, c).quizRequired);
    QVERIFY(!decideQuiz("bob", {"bob"}, c).quizRequired);
}

void QuizPolicyTest::disabled()
{
    auto c = config();
    c.enabled = false;
    QVERIFY(!decideQuiz("alice", {"edu-students"}, c).quizRequired);
}

void QuizPolicyTest::yearMapping()
{
    QCOMPARE(decideQuiz("a", {"edu-students", "edu-year3"}, config()).yearLevel, 3);
    QCOMPARE(decideQuiz("a", {"edu-students", "edu-year5"}, config()).yearLevel, 5);
}

void QuizPolicyTest::yearFirstMatchWins()
{
    // config order decides, not the order of the user's groups
    QCOMPARE(decideQuiz("a", {"edu-year5", "edu-year3"}, config()).yearLevel, 3);
    auto c = config();
    c.yearGroups = {"edu-year5:5", "edu-year3:3"};
    QCOMPARE(decideQuiz("a", {"edu-year3", "edu-year5"}, c).yearLevel, 5);
}

void QuizPolicyTest::defaultYear()
{
    QCOMPARE(decideQuiz("a", {"edu-students"}, config()).yearLevel, 4);
    auto c = config();
    c.defaultYear = 5;
    QCOMPARE(decideQuiz("a", {"edu-students"}, c).yearLevel, 5);
}

void QuizPolicyTest::badYearGroups()
{
    QStringList errors;
    const auto parsed = parseYearGroups({"edu-year3:3", "nonsense", "x:abc", ":4", "y:0", " edu-year4 : 4 "}, &errors);
    QCOMPARE(parsed.size(), 2);
    QCOMPARE(parsed[1].first, QString("edu-year4"));
    QCOMPARE(parsed[1].second, 4);
    QCOMPARE(errors.size(), 4);
}

void QuizPolicyTest::currentUserGroups()
{
    const passwd *pw = getpwuid(getuid());
    QVERIFY(pw);
    const QStringList groups = userGroups(QString::fromLocal8Bit(pw->pw_name));
    QVERIFY(!groups.isEmpty());
}

void QuizPolicyTest::unknownUserGroups()
{
    QVERIFY(userGroups("no-such-user-edu-lm").isEmpty());
}

QTEST_APPLESS_MAIN(QuizPolicyTest)
#include "QuizPolicyTest.moc"
