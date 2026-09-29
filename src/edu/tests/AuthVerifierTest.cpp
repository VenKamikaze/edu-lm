// SPDX-License-Identifier: GPL-2.0-or-later
#include "AuthVerifier.h"

#include <QFile>
#include <QSignalSpy>
#include <QtTest>

using namespace EduLM;

// Runs the real Auth against fake sddm-helper scripts (see fakehelper/).
class AuthVerifierTest : public QObject
{
    Q_OBJECT

private slots:
    void cleanup();
    void helperCrash();
    void helperFailure();
    void helperFailsToStart();
    void helperHangs();

private:
    // Exactly one verified() must arrive, and nothing more (or a crash) shortly after.
    void expectSingleFailure(AuthVerifier &verifier, const QString &user);

    static QString helper() { return QString::fromUtf8(FAKE_HELPER); }
};

void AuthVerifierTest::cleanup()
{
    QFile::setPermissions(helper(), QFile::permissions(helper()) | QFile::ExeOwner);
}

void AuthVerifierTest::expectSingleFailure(AuthVerifier &verifier, const QString &user)
{
    QSignalSpy spy(&verifier, &PasswordVerifier::verified);
    verifier.verify(user, QStringLiteral("pw"));
    QVERIFY(spy.wait(5000));
    QTest::qWait(300);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.first().first().toBool(), false);
}

void AuthVerifierTest::helperCrash()
{
    AuthVerifier verifier;
    expectSingleFailure(verifier, QStringLiteral("crash"));
    // cleaned up: the next check is accepted
    expectSingleFailure(verifier, QStringLiteral("fail"));
}

void AuthVerifierTest::helperFailure()
{
    AuthVerifier verifier;
    expectSingleFailure(verifier, QStringLiteral("fail"));
}

void AuthVerifierTest::helperFailsToStart()
{
    QVERIFY(QFile::setPermissions(helper(), QFile::ReadOwner | QFile::WriteOwner));
    AuthVerifier verifier;
    expectSingleFailure(verifier, QStringLiteral("fail"));

    QVERIFY(QFile::setPermissions(helper(), QFile::permissions(helper()) | QFile::ExeOwner));
    expectSingleFailure(verifier, QStringLiteral("fail"));
}

void AuthVerifierTest::helperHangs()
{
    AuthVerifier verifier;
    verifier.setTimeoutMs(300);
    QElapsedTimer timer;
    timer.start();
    expectSingleFailure(verifier, QStringLiteral("hang"));
    QVERIFY(timer.elapsed() < 3000);
    // and the verifier is usable again
    expectSingleFailure(verifier, QStringLiteral("fail"));
}

QTEST_GUILESS_MAIN(AuthVerifierTest)
#include "AuthVerifierTest.moc"
