// SPDX-License-Identifier: GPL-2.0-or-later
#include "core/CooldownTracker.h"

#include <QtTest>

using namespace EduLM;

class CooldownTrackerTest : public QObject
{
    Q_OBJECT

private slots:
    void countsDown();
    void perUser();
    void clear();
    void rerecordRestarts();
    void zeroCooldown();
    void hugeCooldown();
    void lengthComesWithEachFailure();
    void defaultClockWorks();
};

void CooldownTrackerTest::countsDown()
{
    qint64 now = 1000;
    CooldownTracker tracker([&] { return now; });
    QCOMPARE(tracker.secondsLeft("alice"), 0);
    tracker.recordFailure("alice", 60);
    QCOMPARE(tracker.secondsLeft("alice"), 60);
    now += 1;
    QCOMPARE(tracker.secondsLeft("alice"), 60);
    now += 999;
    QCOMPARE(tracker.secondsLeft("alice"), 59);
    now += 58000;
    QCOMPARE(tracker.secondsLeft("alice"), 1);
    now += 999;
    QCOMPARE(tracker.secondsLeft("alice"), 1);
    now += 1;
    QCOMPARE(tracker.secondsLeft("alice"), 0);
    now += 100000;
    QCOMPARE(tracker.secondsLeft("alice"), 0);
}

void CooldownTrackerTest::perUser()
{
    qint64 now = 0;
    CooldownTracker tracker([&] { return now; });
    tracker.recordFailure("alice", 30);
    now = 10000;
    tracker.recordFailure("bob", 30);
    QCOMPARE(tracker.secondsLeft("alice"), 20);
    QCOMPARE(tracker.secondsLeft("bob"), 30);
    QCOMPARE(tracker.secondsLeft("carol"), 0);
}

void CooldownTrackerTest::clear()
{
    qint64 now = 0;
    CooldownTracker tracker([&] { return now; });
    tracker.recordFailure("alice", 30);
    tracker.recordFailure("bob", 30);
    tracker.clear("alice");
    QCOMPARE(tracker.secondsLeft("alice"), 0);
    QCOMPARE(tracker.secondsLeft("bob"), 30);
}

void CooldownTrackerTest::rerecordRestarts()
{
    qint64 now = 0;
    CooldownTracker tracker([&] { return now; });
    tracker.recordFailure("alice", 30);
    now = 20000;
    tracker.recordFailure("alice", 30);
    QCOMPARE(tracker.secondsLeft("alice"), 30);
}

void CooldownTrackerTest::zeroCooldown()
{
    qint64 now = 0;
    CooldownTracker tracker([&] { return now; });
    tracker.recordFailure("alice", 0);
    QCOMPARE(tracker.secondsLeft("alice"), 0);
}

void CooldownTrackerTest::hugeCooldown()
{
    qint64 now = 0;
    // 30 days overflows a 32-bit millisecond count
    CooldownTracker tracker([&] { return now; });
    tracker.recordFailure("alice", 30 * 24 * 3600);
    QCOMPARE(tracker.secondsLeft("alice"), 30 * 24 * 3600);
    now = 24LL * 3600 * 1000;
    QCOMPARE(tracker.secondsLeft("alice"), 29 * 24 * 3600);
}

void CooldownTrackerTest::lengthComesWithEachFailure()
{
    qint64 now = 0;
    CooldownTracker tracker([&] { return now; });
    tracker.recordFailure("alice", 10);
    tracker.recordFailure("bob", 50);
    QCOMPARE(tracker.secondsLeft("alice"), 10);
    QCOMPARE(tracker.secondsLeft("bob"), 50);
}

void CooldownTrackerTest::defaultClockWorks()
{
    CooldownTracker tracker;
    tracker.recordFailure("alice", 60);
    const int left = tracker.secondsLeft("alice");
    QVERIFY(left >= 59 && left <= 60);
}

QTEST_APPLESS_MAIN(CooldownTrackerTest)
#include "CooldownTrackerTest.moc"
