// SPDX-License-Identifier: GPL-2.0-or-later
#include "EduCheck.h"
#include "EduConfiguration.h"

#include "Configuration.h"

#include <QtTest>
#include <grp.h>
#include <pwd.h>
#include <unistd.h>

using namespace EduLM;

class EduCheckTest : public QObject
{
    Q_OBJECT

private slots:
    void init();

    void pamChainFollowsIncludes();
    void pamChainProblems();
    void nullok_data();
    void nullok();

    void permissions();
    void quizUserList();
    void themeResolution();
    void sddmViewMatchesMainConfig();

    void healthySystem();
    void wrongDisplayManager();
    void themeOverriddenByKdeSettings();
    void missingNullokIsFatal();
    void emptyPasswordsWarn();
    void notRootSkipsPasswordChecks();
    void poolTooSmall();
    void badPermissionsFail();
    void mainFileKeysWarn();
    void autologinQuizUserWarns();
    void consoleLock();
    void missingGroupsAndUsers();
    void themeBlamesEmptyOverride();
    void lockedPasswordWarns();
    void pamFallsBackToUsrLib();
    void realGroupMembersIncludePrimaryGroup();
    void consoleLockNeverClaimsOk();

private:
    void write(const QString &relPath, const QByteArray &content, QFile::Permissions perms = {});
    void writeHealthySystem();
    SystemView view() const;
    static QString messages(const QList<CheckResult> &results, Level level);
    static bool has(const QList<CheckResult> &results, Level level, const QString &part);

    QTemporaryDir m_dir;
    QString root() const { return m_dir.path(); }
};

static const QString kMainConf = QStringLiteral(EDU_CONFIG_FILE);
static const QString kDropIns = QStringLiteral(EDU_CONFIG_DIR);

void EduCheckTest::init()
{
    QVERIFY(m_dir.isValid());
    QDir(m_dir.path()).removeRecursively();
    QVERIFY(QDir().mkpath(m_dir.path()));
}

void EduCheckTest::write(const QString &relPath, const QByteArray &content, QFile::Permissions perms)
{
    const QString path = root() + relPath;
    QVERIFY(QDir().mkpath(QFileInfo(path).absolutePath()));
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
    f.write(content);
    f.close();
    if (perms)
        QVERIFY(f.setPermissions(perms));
}

// Fake groups: alice (year 3 and 5) and bob are students; dad is not.
SystemView EduCheckTest::view() const
{
    SystemView v;
    v.root = m_dir.path();
    v.isRoot = true;
    v.expectedOwner = getuid();
    const QMap<QString, QStringList> groups{
        {"alice", {"alice", "edu-students", "edu-year3", "edu-year5"}},
        {"bob", {"bob", "edu-students"}},
        {"dad", {"dad", "wheel"}},
    };
    v.groupExists = [](const QString &g) { return QStringList{"edu-students", "edu-year3", "edu-year4", "edu-year5", "wheel"}.contains(g); };
    v.groupMembers = [](const QString &g) { return g == "edu-students" ? QStringList{"alice", "bob"} : QStringList(); };
    v.userGroups = [groups](const QString &u) { return groups.value(u); };
    v.passwordState = [](const QString &) { return PasswordState::Empty; };
    return v;
}

// A system that passes every check.
void EduCheckTest::writeHealthySystem()
{
    QVERIFY(QDir().mkpath(root() + "/etc/systemd/system"));
    QVERIFY(QFile::link("/usr/lib/systemd/system/sddm.service", root() + "/etc/systemd/system/display-manager.service"));

    QVERIFY(QDir().mkpath(root() + QStringLiteral(DATA_INSTALL_DIR) + "/themes/edu"));
    write(QStringLiteral(SYSTEM_CONFIG_DIR "/default.conf"), "[Theme]\nCurrent=\n");
    write(QStringLiteral(SYSTEM_CONFIG_DIR "/edu-lm.conf"), "[Theme]\nCurrent=edu\n");

    // comments only, like the installed file
    write(kMainConf, "# nothing here\n#Enabled=true\n");
    write(kDropIns + "/10-site.conf", "[Quiz]\nStudentPasswords=false\n[Content]\nQuestionDirs=/questions\n");

    QVERIFY(QDir().mkpath(root() + "/questions"));
    QVERIFY(QFile::setPermissions(root() + "/questions", QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
    const auto packs = QDir(QStringLiteral(EDU_SAMPLE_DIR)).entryList({"*.json"});
    for (const QString &p : packs) {
        QFile in(QStringLiteral(EDU_SAMPLE_DIR "/") + p);
        QVERIFY(in.open(QIODevice::ReadOnly));
        write("/questions/" + p, in.readAll(), QFile::ReadOwner | QFile::WriteOwner);
    }

    write("/etc/pam.d/sddm", "#%PAM-1.0\nauth include system-login\n");
    write("/etc/pam.d/system-login", "auth required pam_shells.so\nauth include system-auth\naccount required pam_access.so\n");
    write("/etc/pam.d/system-auth",
          "auth required pam_faillock.so preauth\n"
          "-auth [success=2 default=ignore] pam_systemd_home.so\n"
          "auth [success=1 default=bad] pam_unix.so try_first_pass nullok\n");
    write("/etc/pam.d/login", "auth include system-login\naccount include system-login\n");
}

QString EduCheckTest::messages(const QList<CheckResult> &results, Level level)
{
    QStringList out;
    for (const CheckResult &r : results) {
        if (r.level == level)
            out << r.text;
    }
    return out.join(QLatin1Char('\n'));
}

bool EduCheckTest::has(const QList<CheckResult> &results, Level level, const QString &part)
{
    return messages(results, level).contains(part);
}

// ------------------------------------------------------------------------ PAM

void EduCheckTest::pamChainFollowsIncludes()
{
    write("/etc/pam.d/svc",
          "#%PAM-1.0\n"
          "auth      include     level1   # trailing comment\n"
          "-auth     optional    pam_kwallet5.so\n"
          "account   include     level1\n"
          "auth      substack    level2\n"
          "@include  level3\n");
    write("/etc/pam.d/level1", "auth  required  pam_shells.so\naccount  required  pam_access.so\n");
    write("/etc/pam.d/level2", "auth  [success=1 default=bad]  /usr/lib/security/pam_unix.so  nullok \\\n    try_first_pass\n");
    write("/etc/pam.d/level3", "auth  required  pam_env.so\n");

    const PamChain auth = pamChain(root(), "svc", "auth");
    QStringList modules;
    for (const PamEntry &e : auth.entries)
        modules << e.moduleName();
    QCOMPARE(modules, (QStringList{"pam_shells.so", "pam_kwallet5.so", "pam_unix.so", "pam_env.so"}));
    QVERIFY(auth.problems.isEmpty());
    QCOMPARE(auth.entries.at(2).control, QStringLiteral("[success=1 default=bad]"));
    QCOMPARE(auth.entries.at(2).args, (QStringList{"nullok", "try_first_pass"}));

    // other types stay separate
    const PamChain account = pamChain(root(), "svc", "account");
    QCOMPARE(account.entries.size(), 1);
    QCOMPARE(account.entries.at(0).moduleName(), QStringLiteral("pam_access.so"));
}

void EduCheckTest::pamChainProblems()
{
    write("/etc/pam.d/missing", "auth include nowhere\n");
    QVERIFY(pamChain(root(), "missing", "auth").problems.join(' ').contains("nowhere"));
    QVERIFY(!pamChain(root(), "nosuchservice", "auth").problems.isEmpty());

    write("/etc/pam.d/loop-a", "auth include loop-b\n");
    write("/etc/pam.d/loop-b", "auth include loop-a\nauth required pam_deny.so\n");
    const PamChain loop = pamChain(root(), "loop-a", "auth");
    QVERIFY(loop.problems.join(' ').contains("loop"));
    QCOMPARE(loop.entries.size(), 1);

    // the same file reached twice by different routes is not a loop
    write("/etc/pam.d/diamond", "auth include left\nauth include right\n");
    write("/etc/pam.d/left", "auth include common\n");
    write("/etc/pam.d/right", "auth include common\n");
    write("/etc/pam.d/common", "auth required pam_deny.so\n");
    const PamChain diamond = pamChain(root(), "diamond", "auth");
    QVERIFY2(diamond.problems.isEmpty(), qPrintable(diamond.problems.join(' ')));
    QCOMPARE(diamond.entries.size(), 2);
}

void EduCheckTest::nullok_data()
{
    QTest::addColumn<QString>("unixLine");
    QTest::addColumn<int>("state");

    const int all = static_cast<int>(NullokState::All);
    const int missing = static_cast<int>(NullokState::Missing);
    QTest::newRow("nullok") << "auth required pam_unix.so try_first_pass nullok" << all;
    QTest::newRow("nullok_secure") << "auth required pam_unix.so nullok_secure" << all;
    QTest::newRow("full path") << "auth required /lib/security/pam_unix.so nullok" << all;
    QTest::newRow("optional prefix") << "-auth required pam_unix.so nullok" << all;
    QTest::newRow("absent") << "auth required pam_unix.so try_first_pass" << missing;
    QTest::newRow("substring is not nullok") << "auth required pam_unix.so nullokay" << missing;
    QTest::newRow("nonull wins") << "auth required pam_unix.so nullok nonull" << missing;
    QTest::newRow("two pam_unix, one without nullok") << "auth required pam_unix.so nullok\nauth required pam_unix.so try_first_pass" << missing;
    QTest::newRow("two pam_unix, both nullok") << "auth required pam_unix.so nullok\nauth required pam_unix.so nullok" << all;
    QTest::newRow("other module") << "auth required pam_deny.so nullok" << static_cast<int>(NullokState::NoPamUnix);
}

void EduCheckTest::nullok()
{
    QFETCH(QString, unixLine);
    QFETCH(int, state);
    write("/etc/pam.d/svc", QStringLiteral("auth include inner\n").toUtf8());
    write("/etc/pam.d/inner", (unixLine + "\n").toUtf8());
    QCOMPARE(static_cast<int>(nullokState(pamChain(root(), "svc", "auth"))), state);
}

// ---------------------------------------------------------------- permissions

void EduCheckTest::permissions()
{
    const QString dir = root() + "/q";
    write("/q/a.json", "{}", QFile::ReadOwner | QFile::WriteOwner);
    write("/q/sub/b.json", "{}", QFile::ReadOwner | QFile::WriteOwner);
    QVERIFY(QFile::setPermissions(dir, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
    QVERIFY(QFile::setPermissions(dir + "/sub", QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));

    QVERIFY(permissionProblems({dir, root() + "/does-not-exist"}, getuid()).isEmpty());

    // wrong owner, for the directory and both files
    QCOMPARE(permissionProblems({dir}, getuid() + 1).size(), 4);

    QVERIFY(QFile::setPermissions(dir + "/sub/b.json", QFile::ReadOwner | QFile::WriteOwner | QFile::ReadGroup));
    QStringList p = permissionProblems({dir}, getuid());
    QCOMPARE(p.size(), 1);
    QVERIFY2(p.at(0).contains("b.json") && p.at(0).contains("640"), qPrintable(p.at(0)));

    QVERIFY(QFile::setPermissions(dir + "/sub/b.json", QFile::ReadOwner | QFile::WriteOwner));
    QVERIFY(QFile::setPermissions(dir + "/a.json", QFile::ReadOwner | QFile::WriteOwner | QFile::WriteOther));
    QVERIFY(QFile::setPermissions(dir + "/sub", QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner | QFile::ReadGroup | QFile::ExeGroup));
    QCOMPARE(permissionProblems({dir}, getuid()).size(), 2);
}

// ----------------------------------------------------------------- quiz users

static QuizPolicyConfig policy()
{
    QuizPolicyConfig c;
    c.groups = {"edu-students"};
    c.users = {"carol"};
    c.exemptUsers = {"bob"};
    c.yearGroups = {"edu-year3:3", "edu-year5:5"};
    c.defaultYear = 4;
    return c;
}

void EduCheckTest::quizUserList()
{
    SystemView v = view();
    const auto base = v.userGroups;
    v.userGroups = [base](const QString &u) { return u == "carol" ? QStringList{"carol"} : base(u); };

    const QList<QuizUser> users = quizUsers(policy(), v);
    // bob is exempt, dad is in no quiz group, carol comes from Users
    QCOMPARE(users.size(), 2);
    QCOMPARE(users.at(0).name, QStringLiteral("alice"));
    QCOMPARE(users.at(0).years, (QList<int>{3, 5}));
    QCOMPARE(users.at(1).name, QStringLiteral("carol"));
    QCOMPARE(users.at(1).years, (QList<int>{4}));

    QuizPolicyConfig off = policy();
    off.enabled = false;
    QVERIFY(quizUsers(off, v).isEmpty());
}

// --------------------------------------------------------------------- theme

void EduCheckTest::themeResolution()
{
    write(QStringLiteral(SYSTEM_CONFIG_DIR "/default.conf"), "[Theme]\nCurrent=\n[Autologin]\nUser=\n");
    write(QStringLiteral(SYSTEM_CONFIG_DIR "/edu-lm.conf"), "[Theme]\nCurrent=edu\n");

    ThemeResolution r = resolveSddmConfig(root());
    QCOMPARE(r.current, QStringLiteral("edu"));
    QVERIFY2(r.source.endsWith("/edu-lm.conf"), qPrintable(r.source));
    QVERIFY(r.autologinUser.isEmpty());

    // /etc/sddm.conf.d beats the system directory, /etc/sddm.conf beats both
    write(QStringLiteral(CONFIG_DIR "/kde_settings.conf"), "[Theme]\nCurrent=breeze\n[Autologin]\nUser=kid\n");
    r = resolveSddmConfig(root());
    QCOMPARE(r.current, QStringLiteral("breeze"));
    QVERIFY2(r.source.endsWith("/kde_settings.conf"), qPrintable(r.source));
    QCOMPARE(r.autologinUser, QStringLiteral("kid"));

    write(QStringLiteral(CONFIG_FILE), "[Theme]\nCurrent=maya\n");
    r = resolveSddmConfig(root());
    QCOMPARE(r.current, QStringLiteral("maya"));
    QVERIFY2(r.source.endsWith("/sddm.conf"), qPrintable(r.source));

    // nothing sets it
    QDir(root()).removeRecursively();
    r = resolveSddmConfig(root());
    QVERIFY(r.current.isEmpty());
    QVERIFY(r.source.isEmpty());
}

// The check tool reads a few keys through its own config class. They must exist in the
// real MainConfig, with the same defaults.
void EduCheckTest::sddmViewMatchesMainConfig()
{
    SDDM::MainConfig real;
    real.wipe();
    QVERIFY(real.Theme.entry("Current"));
    QVERIFY(real.Theme.entry("ThemeDir"));
    QVERIFY(real.Autologin.entry("User"));

    const ThemeResolution r = resolveSddmConfig(root());
    QCOMPARE(r.current, real.Theme.Current.get());
    QCOMPARE(r.themeDir, real.Theme.ThemeDir.get());
    QCOMPARE(r.autologinUser, real.Autologin.User.get());
}

// ------------------------------------------------------------------ full run

void EduCheckTest::healthySystem()
{
    writeHealthySystem();
    const auto results = runChecks(view());
    QVERIFY2(messages(results, Level::Fail).isEmpty(), qPrintable(messages(results, Level::Fail)));
    QVERIFY2(messages(results, Level::Warn).isEmpty(), qPrintable(messages(results, Level::Warn)));
    QVERIFY(has(results, Level::Ok, "display manager is sddm.service"));
    QVERIFY(has(results, Level::Ok, "SDDM theme is edu"));
    QVERIFY(has(results, Level::Ok, "2 quiz user(s): alice (year 3,5), bob (year 4)"));
    QVERIFY(has(results, Level::Ok, "pam_unix in the sddm auth chain has nullok"));
    QVERIFY(has(results, Level::Info, "text consoles are not locked"));
}

void EduCheckTest::wrongDisplayManager()
{
    writeHealthySystem();
    QVERIFY(QFile::remove(root() + "/etc/systemd/system/display-manager.service"));
    QVERIFY(QFile::link("/usr/lib/systemd/system/lightdm.service", root() + "/etc/systemd/system/display-manager.service"));
    QVERIFY(has(runChecks(view()), Level::Fail, "display manager is lightdm.service"));

    QVERIFY(QFile::remove(root() + "/etc/systemd/system/display-manager.service"));
    QVERIFY(has(runChecks(view()), Level::Fail, "no display manager is enabled"));
}

void EduCheckTest::themeOverriddenByKdeSettings()
{
    writeHealthySystem();
    write(QStringLiteral(CONFIG_DIR "/kde_settings.conf"), "[Theme]\nCurrent=breeze\n");
    const auto results = runChecks(view());
    QVERIFY(has(results, Level::Fail, "SDDM theme is 'breeze'"));
    QVERIFY(has(results, Level::Fail, "kde_settings.conf"));

    QFile::remove(root() + QStringLiteral(CONFIG_DIR "/kde_settings.conf"));
    QVERIFY(QDir(root() + QStringLiteral(DATA_INSTALL_DIR "/themes/edu")).removeRecursively());
    QVERIFY(has(runChecks(view()), Level::Fail, "does not exist"));
}

void EduCheckTest::missingNullokIsFatal()
{
    writeHealthySystem();
    write("/etc/pam.d/system-auth", "auth [success=1 default=bad] pam_unix.so try_first_pass\n");
    QVERIFY(has(runChecks(view()), Level::Fail, "no nullok"));

    // with passwords on, nullok is not needed
    write(kDropIns + "/10-site.conf", "[Quiz]\nStudentPasswords=true\n[Content]\nQuestionDirs=/questions\n");
    QVERIFY(!has(runChecks(view()), Level::Fail, "nullok"));
}

void EduCheckTest::emptyPasswordsWarn()
{
    writeHealthySystem();
    SystemView v = view();
    v.passwordState = [](const QString &u) { return u == "alice" ? PasswordState::Set : PasswordState::Empty; };
    auto results = runChecks(v);
    QVERIFY(has(results, Level::Warn, "alice has a password but StudentPasswords=false"));
    QVERIFY(!has(results, Level::Warn, "bob"));

    write(kDropIns + "/10-site.conf", "[Quiz]\nStudentPasswords=true\n[Content]\nQuestionDirs=/questions\n");
    results = runChecks(v);
    QVERIFY(has(results, Level::Warn, "bob has an empty password but StudentPasswords=true"));
    QVERIFY(!has(results, Level::Warn, "alice"));
}

void EduCheckTest::notRootSkipsPasswordChecks()
{
    writeHealthySystem();
    SystemView v = view();
    v.isRoot = false;
    v.passwordState = [](const QString &) { return PasswordState::Set; };
    const auto results = runChecks(v);
    QVERIFY(has(results, Level::Info, "run as root to check the quiz users' passwords"));
    QVERIFY(messages(results, Level::Warn).isEmpty());
}

void EduCheckTest::poolTooSmall()
{
    writeHealthySystem();
    // year 4 has 22 sample questions, years 3 and 5 together 29
    write(kDropIns + "/20-rules.conf", "[Quiz]\nQuestionCount=25\nRequiredCorrect=20\nMaxSwaps=5\n");
    auto results = runChecks(view());
    QVERIFY2(has(results, Level::Fail, "year 4: 22 questions, but 25 are asked"), qPrintable(messages(results, Level::Fail)));
    QVERIFY(has(results, Level::Warn, "year 3,5: 29 questions; 30 are needed"));

    write(kDropIns + "/20-rules.conf", "[Quiz]\nQuestionCount=3\nRequiredCorrect=5\n");
    QVERIFY(has(runChecks(view()), Level::Fail, "RequiredCorrect (5) is greater than QuestionCount (3)"));
}

void EduCheckTest::badPermissionsFail()
{
    writeHealthySystem();
    QVERIFY(QFile::setPermissions(root() + "/questions/sample-maths.json", QFile::ReadOwner | QFile::WriteOwner | QFile::ReadOther));
    QVERIFY(has(runChecks(view()), Level::Fail, "sample-maths.json has mode 604"));

    SystemView v = view();
    v.expectedOwner = getuid() + 1;
    QVERIFY(has(runChecks(v), Level::Fail, "is owned by uid"));
}

void EduCheckTest::mainFileKeysWarn()
{
    writeHealthySystem();
    write(kMainConf, "[Quiz]\nMaxSwaps=1   # would override drop-ins\nBogusKey=1\n");
    const auto results = runChecks(view());
    QVERIFY(has(results, Level::Warn, "sets keys (MaxSwaps,BogusKey)"));
    QVERIFY(has(results, Level::Warn, "unknown sections or keys"));
}

void EduCheckTest::autologinQuizUserWarns()
{
    writeHealthySystem();
    write(QStringLiteral(CONFIG_DIR "/autologin.conf"), "[Autologin]\nUser=alice\n");
    QVERIFY(has(runChecks(view()), Level::Warn, "autologin user 'alice' is a quiz user"));

    write(QStringLiteral(CONFIG_DIR "/autologin.conf"), "[Autologin]\nUser=dad\n");
    QVERIFY(has(runChecks(view()), Level::Ok, "autologin user 'dad' is not a quiz user"));
}

void EduCheckTest::consoleLock()
{
    writeHealthySystem();
    write("/etc/security/access.d/50-edu-lm.conf", "-:(edu-students):tty1 tty2 tty3\n");
    auto results = runChecks(view());
    QVERIFY2(has(results, Level::Info, "console rule for the quiz group found"), qPrintable(messages(results, Level::Info)));
    QVERIFY(messages(results, Level::Warn).isEmpty());

    // comma-separated users
    write("/etc/security/access.d/50-edu-lm.conf", "-:(wheel),(edu-students):tty1\n");
    QVERIFY(has(runChecks(view()), Level::Info, "console rule for the quiz group found"));

    // the rule is dead without pam_access in the login service
    write("/etc/pam.d/login", "auth include system-login\n");
    write("/etc/pam.d/system-login", "auth required pam_shells.so\nauth include system-auth\n");
    QVERIFY(has(runChecks(view()), Level::Warn, "pam_access.so is not in the 'login' account chain"));
    write("/etc/pam.d/system-login", "account required pam_access.so\n");

    // a rule for some other group is not ours
    write("/etc/security/access.d/50-edu-lm.conf", "-:(wheel):tty1\n");
    QVERIFY(has(runChecks(view()), Level::Info, "no console rule for the quiz group"));
}

// Origins that also match SDDM would lock the children out of the login screen.
void EduCheckTest::consoleLockNeverClaimsOk()
{
    writeHealthySystem();
    const struct { const char *rule; const char *warning; } bad[] = {
        {"-:edu-students:ALL\n", "would also block the SDDM login"},
        {"-:(edu-students):LOCAL\n", "would also block the SDDM login"},
        {"-:(edu-students):sddm\n", "would also block the SDDM login"},
        {"-:(edu-students):sddm-autologin\n", "would also block the SDDM login"},
        {"-:ALL EXCEPT (edu-students):tty1\n", "uses EXCEPT"},
        {"-:(edu-students):cron\n", "names no ttyN origin"},
        {"-:(edu-students):\n", "names no ttyN origin"},
    };
    for (const auto &c : bad) {
        write("/etc/security/access.d/50-edu-lm.conf", c.rule);
        const auto results = runChecks(view());
        QVERIFY2(has(results, Level::Warn, c.warning), c.rule);
        QVERIFY2(!has(results, Level::Ok, "console"), c.rule);
        QVERIFY2(!has(results, Level::Info, "console rule for the quiz group found"), c.rule);
    }

    // a correct rule that an earlier allow rule in access.conf shadows
    write("/etc/security/access.d/50-edu-lm.conf", "-:(edu-students):tty1 tty2\n");
    write("/etc/security/access.conf", "+:ALL:ALL\n");
    QVERIFY(has(runChecks(view()), Level::Warn, "after an allow (+) rule"));
    write("/etc/security/access.conf", "+:(wheel):tty1\n");
    QVERIFY(!has(runChecks(view()), Level::Warn, "after an allow (+) rule"));
    // an earlier deny rule is no shadow
    write("/etc/security/access.conf", "-:(wheel):tty1\n");
    QVERIFY(!has(runChecks(view()), Level::Warn, "after an allow (+) rule"));
}

void EduCheckTest::themeBlamesEmptyOverride()
{
    // /etc/sddm.conf sets an empty theme after our drop-in: it is the one to blame
    write(QStringLiteral(SYSTEM_CONFIG_DIR "/default.conf"), "[Theme]\nCurrent=\n");
    write(QStringLiteral(SYSTEM_CONFIG_DIR "/edu-lm.conf"), "[Theme]\nCurrent=edu\n");
    write(QStringLiteral(CONFIG_FILE), "[Theme]\nCurrent=\n");
    const ThemeResolution r = resolveSddmConfig(root());
    QVERIFY(r.current.isEmpty());
    QVERIFY2(r.source.endsWith("/sddm.conf"), qPrintable(r.source));

    // Current= in another section is not the theme
    write(QStringLiteral(CONFIG_FILE), "[Other]\nCurrent=x\n");
    QVERIFY2(resolveSddmConfig(root()).source.endsWith("/edu-lm.conf"), "wrong section blamed");
}

void EduCheckTest::lockedPasswordWarns()
{
    writeHealthySystem();
    SystemView v = view();
    v.passwordState = [](const QString &u) { return u == "alice" ? PasswordState::Locked : PasswordState::Empty; };
    // whatever StudentPasswords says
    QVERIFY(has(runChecks(v), Level::Warn, "alice has a locked password"));
    write(kDropIns + "/10-site.conf", "[Quiz]\nStudentPasswords=true\n[Content]\nQuestionDirs=/questions\n");
    QVERIFY(has(runChecks(v), Level::Warn, "alice has a locked password"));
}

void EduCheckTest::pamFallsBackToUsrLib()
{
    write("/etc/pam.d/svc", "auth include vendor\nauth include local\n");
    write("/usr/lib/pam.d/vendor", "auth required pam_shells.so\n");
    write("/usr/lib/pam.d/local", "auth required pam_deny.so\n");
    write("/etc/pam.d/local", "auth required pam_permit.so\n"); // /etc wins, per file
    const PamChain c = pamChain(root(), "svc", "auth");
    QVERIFY(c.problems.isEmpty());
    QCOMPARE(c.entries.size(), 2);
    QCOMPARE(c.entries.at(0).moduleName(), QStringLiteral("pam_shells.so"));
    QCOMPARE(c.entries.at(1).moduleName(), QStringLiteral("pam_permit.so"));
    // a service in /usr/lib/pam.d only
    QCOMPARE(pamChain(root(), "vendor", "auth").entries.size(), 1);
}

// The primary-group lookup goes through getpwent, so test the real function with the
// account running the test.
void EduCheckTest::realGroupMembersIncludePrimaryGroup()
{
    const struct passwd *pw = getpwuid(getuid());
    QVERIFY(pw);
    const struct group *gr = getgrgid(pw->pw_gid);
    QVERIFY(gr);
    const QStringList members = realSystem().groupMembers(QString::fromLocal8Bit(gr->gr_name));
    QVERIFY2(members.contains(QString::fromLocal8Bit(pw->pw_name)), qPrintable(members.join(',')));
    QVERIFY(realSystem().groupMembers("no-such-group-edu-lm").isEmpty());
}

void EduCheckTest::missingGroupsAndUsers()
{
    writeHealthySystem();
    write(kDropIns + "/30-users.conf", "[Quiz]\nGroups=edu-students,kids\nUsers=ghost\n[Content]\nYearGroups=edu-year3:3,edu-year9:9,broken\n");
    const auto results = runChecks(view());
    QVERIFY(has(results, Level::Fail, "do not exist: kids"));
    QVERIFY(has(results, Level::Warn, "YearGroups do not exist: edu-year9"));
    QVERIFY(has(results, Level::Warn, "Users lists 'ghost' but there is no such account"));
    QVERIFY(has(results, Level::Fail, "bad YearGroups entry 'broken'"));
}

QTEST_GUILESS_MAIN(EduCheckTest)
#include "EduCheckTest.moc"
