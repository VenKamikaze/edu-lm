// SPDX-License-Identifier: GPL-2.0-or-later
#include "EduCheck.h"

#include "EduConfiguration.h"
#include "core/QuestionBank.h"

#include "Constants.h"
#include "ConfigReader.h"

#include <grp.h>
#include <pwd.h>
#include <shadow.h>
#include <unistd.h>

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>

namespace EduLM {

// ---------------------------------------------------------------- real system

SystemView realSystem()
{
    SystemView v;
    v.isRoot = geteuid() == 0;
    v.expectedOwner = 0;
    v.groupExists = [](const QString &group) { return getgrnam(group.toLocal8Bit().constData()) != nullptr; };
    v.groupMembers = [](const QString &group) {
        QStringList members;
        const struct group *gr = getgrnam(group.toLocal8Bit().constData());
        if (!gr)
            return members;
        for (char **m = gr->gr_mem; m && *m; ++m)
            members << QString::fromLocal8Bit(*m);
        // members whose primary group this is are not listed in gr_mem
        const gid_t gid = gr->gr_gid;
        setpwent();
        while (const passwd *pw = getpwent()) {
            if (pw->pw_gid == gid)
                members << QString::fromLocal8Bit(pw->pw_name);
        }
        endpwent();
        members.removeDuplicates();
        return members;
    };
    v.userGroups = [](const QString &user) { return EduLM::userGroups(user); };
    v.passwordState = [](const QString &user) {
        const spwd *sp = getspnam(user.toLocal8Bit().constData());
        if (!sp || !sp->sp_pwdp)
            return PasswordState::Unknown;
        if (*sp->sp_pwdp == '\0')
            return PasswordState::Empty;
        return *sp->sp_pwdp == '!' || *sp->sp_pwdp == '*' ? PasswordState::Locked : PasswordState::Set;
    };
    return v;
}

QString levelName(Level level)
{
    switch (level) {
    case Level::Ok: return QStringLiteral("OK");
    case Level::Info: return QStringLiteral("INFO");
    case Level::Warn: return QStringLiteral("WARN");
    case Level::Fail: return QStringLiteral("FAIL");
    }
    return QString();
}

// ------------------------------------------------------------------------ PAM

QString PamEntry::moduleName() const
{
    return QFileInfo(module).fileName();
}

// Whitespace-separated words; "[...]" is one word even when it holds spaces.
static QStringList pamTokens(const QString &line)
{
    QStringList tokens;
    QString word;
    bool inBracket = false;
    for (const QChar c : line) {
        if (inBracket) {
            word += c;
            inBracket = c != QLatin1Char(']');
        } else if (c.isSpace()) {
            if (!word.isEmpty())
                tokens << word;
            word.clear();
        } else {
            inBracket = c == QLatin1Char('[') && word.isEmpty();
            word += c;
        }
    }
    if (!word.isEmpty())
        tokens << word;
    return tokens;
}

static QStringList pamLines(const QString &path, bool *ok)
{
    QFile file(path);
    *ok = file.open(QIODevice::ReadOnly);
    QStringList lines;
    if (!*ok)
        return lines;

    QString joined;
    while (!file.atEnd()) {
        QString line = QString::fromUtf8(file.readLine());
        line = line.left(line.indexOf(QLatin1Char('#')) < 0 ? line.size() : line.indexOf(QLatin1Char('#'))).trimmed();
        if (line.endsWith(QLatin1Char('\\'))) { // continuation
            joined += line.chopped(1) + QLatin1Char(' ');
            continue;
        }
        lines << joined + line;
        joined.clear();
    }
    return lines;
}

static void collectPam(const QString &root, const QString &service, const QString &type,
                       QStringList *stack, PamChain *out)
{
    if (stack->contains(service) || stack->size() > 16) {
        out->problems << QStringLiteral("include loop at '%1'").arg(service);
        return;
    }

    // Linux-PAM looks in /etc/pam.d first, then /usr/lib/pam.d, file by file
    QString path = service.startsWith(QLatin1Char('/')) ? root + service
                                                        : root + QStringLiteral("/etc/pam.d/") + service;
    if (!service.startsWith(QLatin1Char('/')) && !QFileInfo::exists(path))
        path = root + QStringLiteral("/usr/lib/pam.d/") + service;
    bool ok = false;
    const QStringList lines = pamLines(path, &ok);
    if (!ok) {
        out->problems << QStringLiteral("cannot read %1").arg(path);
        return;
    }

    stack->append(service);
    for (const QString &line : lines) {
        const QStringList t = pamTokens(line);
        if (t.isEmpty())
            continue;

        if (t.at(0) == QLatin1String("@include")) {
            if (t.size() > 1)
                collectPam(root, t.at(1), type, stack, out);
            continue;
        }

        QString lineType = t.at(0);
        if (lineType.startsWith(QLatin1Char('-')))
            lineType.remove(0, 1);
        if (lineType != type || t.size() < 3)
            continue;

        if (t.at(1) == QLatin1String("include") || t.at(1) == QLatin1String("substack")) {
            collectPam(root, t.at(2), type, stack, out);
            continue;
        }
        out->entries.append({lineType, t.at(1), t.at(2), t.mid(3)});
    }
    stack->removeLast();
}

PamChain pamChain(const QString &root, const QString &service, const QString &type)
{
    PamChain chain;
    QStringList stack;
    collectPam(root, service, type, &stack, &chain);
    return chain;
}

NullokState nullokState(const PamChain &authChain)
{
    int unixEntries = 0;
    int withNullok = 0;
    for (const PamEntry &e : authChain.entries) {
        if (e.moduleName() != QLatin1String("pam_unix.so"))
            continue;
        ++unixEntries;
        const bool nullok = e.args.contains(QStringLiteral("nullok")) || e.args.contains(QStringLiteral("nullok_secure"));
        if (nullok && !e.args.contains(QStringLiteral("nonull")))
            ++withNullok;
    }
    if (unixEntries == 0)
        return NullokState::NoPamUnix;
    return withNullok == unixEntries ? NullokState::All : NullokState::Missing;
}

// ---------------------------------------------------------------- SDDM config

// The few MainConfig entries the checks need, read with the same ConfigBase::load()
// as the daemon. MainConfig has fixed paths, so a fixture root cannot be pointed at it.
// SddmViewTest compares the entry names and defaults with the real one.
namespace {
struct SddmPaths {
    QString file, dir, sysDir;
};
SddmPaths &sddmPaths()
{
    static SddmPaths paths;
    return paths;
}

Config(SddmView, sddmPaths().file, sddmPaths().dir, sddmPaths().sysDir,
    Section(Theme,
        Entry(ThemeDir, QString, _S(DATA_INSTALL_DIR "/themes"), _S(""));
        Entry(Current,  QString, _S(""),                         _S(""));
    );
    Section(Autologin,
        Entry(User,     QString, QString(),                      _S(""));
    );
);
}

// Files in the order ConfigBase::load() reads them.
static QStringList sddmFiles(const QString &root)
{
    QStringList files;
    for (const QString &d : {QStringLiteral(SYSTEM_CONFIG_DIR), QStringLiteral(CONFIG_DIR)}) {
        const auto infos = QDir(root + d).entryInfoList(QDir::Files | QDir::NoDotAndDotDot, QDir::LocaleAware);
        for (const QFileInfo &info : infos)
            files << info.absoluteFilePath();
    }
    files << root + QStringLiteral(CONFIG_FILE);
    return files;
}

// True if the file has a Current= line in its [Theme] section.
static bool setsThemeCurrent(const QString &file)
{
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    QString section;
    while (!f.atEnd()) {
        QString line = QString::fromUtf8(f.readLine());
        line = line.left(line.indexOf(QLatin1Char('#')) < 0 ? line.size() : line.indexOf(QLatin1Char('#'))).trimmed();
        if (line.startsWith(QLatin1Char('[')) && line.endsWith(QLatin1Char(']')))
            section = line.mid(1, line.size() - 2);
        else if (section == QLatin1String("Theme") && line.section(QLatin1Char('='), 0, 0).trimmed() == QLatin1String("Current")
                 && line.contains(QLatin1Char('=')))
            return true;
    }
    return false;
}

ThemeResolution resolveSddmConfig(const QString &root)
{
    sddmPaths() = {root + QStringLiteral(CONFIG_FILE), root + QStringLiteral(CONFIG_DIR),
                   root + QStringLiteral(SYSTEM_CONFIG_DIR)};
    const SddmView all;

    ThemeResolution r;
    r.current = all.Theme.Current.get();
    r.themeDir = all.Theme.ThemeDir.get();
    r.autologinUser = all.Autologin.User.get();

    // The last file that sets the key at all, whatever the value, is the one that won.
    for (const QString &file : sddmFiles(root)) {
        if (setsThemeCurrent(file))
            r.source = file;
    }
    return r;
}

// ---------------------------------------------------------------- quiz users

QList<QuizUser> quizUsers(const QuizPolicyConfig &policy, const SystemView &system)
{
    QSet<QString> candidates(policy.users.begin(), policy.users.end());
    for (const QString &g : policy.groups) {
        const QStringList members = system.groupMembers(g);
        candidates.unite(QSet<QString>(members.begin(), members.end()));
    }

    QList<QuizUser> users;
    for (const QString &name : candidates) {
        const QuizDecision d = decideQuiz(name, system.userGroups(name), policy);
        if (d.quizRequired)
            users.append({name, d.years});
    }
    std::sort(users.begin(), users.end(), [](const QuizUser &a, const QuizUser &b) { return a.name < b.name; });
    return users;
}

// ----------------------------------------------------------------- permissions

static QString modeString(QFile::Permissions p)
{
    auto digit = [p](QFile::Permission r, QFile::Permission w, QFile::Permission x) {
        return (p.testFlag(r) ? 4 : 0) + (p.testFlag(w) ? 2 : 0) + (p.testFlag(x) ? 1 : 0);
    };
    return QStringLiteral("%1%2%3")
        .arg(digit(QFile::ReadOwner, QFile::WriteOwner, QFile::ExeOwner))
        .arg(digit(QFile::ReadGroup, QFile::WriteGroup, QFile::ExeGroup))
        .arg(digit(QFile::ReadOther, QFile::WriteOther, QFile::ExeOther));
}

static void checkEntry(const QFileInfo &info, uint expectedOwner, QStringList *problems)
{
    if (info.ownerId() != expectedOwner)
        problems->append(QStringLiteral("%1 is owned by uid %2, not %3").arg(info.filePath()).arg(info.ownerId()).arg(expectedOwner));

    const QFile::Permissions open = QFile::ReadGroup | QFile::WriteGroup | QFile::ReadOther | QFile::WriteOther;
    if (info.permissions() & open)
        problems->append(QStringLiteral("%1 has mode %2 (group and others must have no access)").arg(info.filePath(), modeString(info.permissions())));
}

QStringList permissionProblems(const QStringList &dirs, uint expectedOwner)
{
    QStringList problems;
    for (const QString &dir : dirs) {
        const QFileInfo top(dir);
        if (!top.exists())
            continue;
        checkEntry(top, expectedOwner, &problems);

        QDirIterator it(dir, QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            it.next();
            checkEntry(it.fileInfo(), expectedOwner, &problems);
        }
    }
    return problems;
}

// ------------------------------------------------------------------- the checks

namespace {

class Report
{
public:
    void ok(const QString &t) { m_results.append({Level::Ok, t}); }
    void info(const QString &t) { m_results.append({Level::Info, t}); }
    void warn(const QString &t) { m_results.append({Level::Warn, t}); }
    void fail(const QString &t) { m_results.append({Level::Fail, t}); }
    QList<CheckResult> results() const { return m_results; }

private:
    QList<CheckResult> m_results;
};

QString yearsText(const QList<int> &years)
{
    QStringList parts;
    for (int y : years)
        parts << QString::number(y);
    return parts.join(QLatin1Char(','));
}

void checkDisplayManager(const SystemView &sys, Report &r)
{
    const QFileInfo link(sys.root + QStringLiteral("/etc/systemd/system/display-manager.service"));
    if (!link.isSymLink()) {
        r.fail(QStringLiteral("no display manager is enabled (%1 is not a symlink): systemctl enable sddm.service").arg(link.filePath()));
        return;
    }
    const QString target = QFileInfo(link.symLinkTarget()).fileName();
    if (target == QLatin1String("sddm.service"))
        r.ok(QStringLiteral("display manager is sddm.service"));
    else
        r.fail(QStringLiteral("display manager is %1, not sddm.service: systemctl disable %1 && systemctl enable --force sddm.service").arg(target));
}

void checkSddmConfig(const SystemView &sys, const ThemeResolution &theme, Report &r)
{
    const QString by = theme.source.isEmpty() ? QString() : QStringLiteral(" (set by %1)").arg(theme.source);
    if (theme.current != QLatin1String("edu")) {
        r.fail(QStringLiteral("SDDM theme is '%1', not 'edu'%2; quiz users cannot log in").arg(theme.current, by)
               + (theme.source.isEmpty() ? QString() : QStringLiteral(": edit that file")));
        return;
    }
    const QString dir = sys.root + theme.themeDir + QLatin1Char('/') + theme.current;
    if (QFileInfo(dir).isDir())
        r.ok(QStringLiteral("SDDM theme is edu%1").arg(by));
    else
        r.fail(QStringLiteral("SDDM theme is edu%1 but %2 does not exist").arg(by, dir));
}

// Uncommented keys in the main file override every drop-in.
QStringList activeKeys(const QString &file)
{
    QStringList keys;
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly))
        return keys;
    while (!f.atEnd()) {
        QString line = QString::fromUtf8(f.readLine());
        line = line.left(line.indexOf(QLatin1Char('#')) < 0 ? line.size() : line.indexOf(QLatin1Char('#'))).trimmed();
        const int eq = line.indexOf(QLatin1Char('='));
        if (eq > 0)
            keys << line.left(eq).trimmed();
    }
    return keys;
}

// Splits the way pam_access does: on spaces, tabs and commas.
QStringList accessTokens(const QString &field)
{
    return field.split(QRegularExpression(QStringLiteral("[\\s,]+")), Qt::SkipEmptyParts);
}

bool isTtyOrigin(const QString &origin)
{
    static const QRegularExpression tty(QStringLiteral("^tty[0-9]+$"), QRegularExpression::CaseInsensitiveOption);
    return tty.match(origin).hasMatch();
}

// Reads the access rules in pam_access order and reports what it can see. It never says
// the consoles are locked: that is only proven by a real login (PHASE4-TEST).
void checkConsoleLock(const SystemView &sys, const EduConfig &config, Report &r)
{
    const QString secDir = sys.root + QStringLiteral("/etc/security");
    QStringList files{secDir + QStringLiteral("/access.conf")};
    const auto infos = QDir(secDir + QStringLiteral("/access.d")).entryInfoList({QStringLiteral("*.conf")}, QDir::Files, QDir::Name);
    for (const QFileInfo &i : infos)
        files << i.absoluteFilePath();

    const QStringList groups = config.Quiz.Groups.get();
    auto namesQuizGroup = [&groups](const QString &usersField) {
        for (const QString &token : accessTokens(usersField)) {
            QString name = token;
            if (name.startsWith(QLatin1Char('(')) && name.endsWith(QLatin1Char(')')))
                name = name.mid(1, name.size() - 2);
            if (groups.contains(name))
                return true;
        }
        return false;
    };

    QString ruleFile;
    QStringList warnings;
    bool shadowed = false; // an earlier allow rule could match first
    for (const QString &file : std::as_const(files)) {
        QFile rulesFile(file);
        if (!rulesFile.open(QIODevice::ReadOnly))
            continue;
        while (!rulesFile.atEnd()) {
            const QString line = QString::fromUtf8(rulesFile.readLine()).trimmed();
            if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
                continue;
            const QStringList fields = line.split(QLatin1Char(':'));
            if (fields.size() < 3)
                continue;
            const QString perm = fields.at(0).trimmed();
            const QString users = fields.at(1);
            const QStringList origins = accessTokens(fields.mid(2).join(QLatin1Char(':')));
            const bool hasTty = std::any_of(origins.begin(), origins.end(), isTtyOrigin);
            const bool broad = std::any_of(origins.begin(), origins.end(), [](const QString &o) {
                return o.compare(QLatin1String("ALL"), Qt::CaseInsensitive) == 0 || o.compare(QLatin1String("LOCAL"), Qt::CaseInsensitive) == 0;
            });

            if (perm == QLatin1String("+")) {
                const bool coversQuiz = accessTokens(users).contains(QStringLiteral("ALL"), Qt::CaseInsensitive) || namesQuizGroup(users);
                shadowed = shadowed || (coversQuiz && (hasTty || broad));
                continue;
            }
            if (perm != QLatin1String("-") || !namesQuizGroup(users))
                continue;

            const QString where = QStringLiteral("%1: %2").arg(file, line);
            ruleFile = file;
            if (accessTokens(users).contains(QStringLiteral("EXCEPT"), Qt::CaseInsensitive))
                warnings << QStringLiteral("%1 uses EXCEPT in the users field; the rule may not cover the quiz group").arg(where);
            if (broad || origins.contains(QStringLiteral("sddm")) || origins.contains(QStringLiteral("sddm-autologin")))
                warnings << QStringLiteral("%1 has an origin (ALL, LOCAL or sddm) that would also block the SDDM login; list ttyN only").arg(where);
            else if (!hasTty)
                warnings << QStringLiteral("%1 names no ttyN origin, so it does not lock the text consoles").arg(where);
            if (shadowed)
                warnings << QStringLiteral("%1 comes after an allow (+) rule that may match first").arg(where);
        }
    }

    const PamChain login = pamChain(sys.root, QStringLiteral("login"), QStringLiteral("account"));
    const bool hasAccess = std::any_of(login.entries.begin(), login.entries.end(),
        [](const PamEntry &e) { return e.moduleName() == QLatin1String("pam_access.so"); });

    if (ruleFile.isEmpty()) {
        r.info(QStringLiteral("no console rule for the quiz group; text consoles are not locked (optional: see docs/INSTALL-ARCH.md, pam_access)"));
        return;
    }
    for (const QString &w : std::as_const(warnings))
        r.warn(w);
    if (!hasAccess)
        r.warn(QStringLiteral("%1 has a console rule but pam_access.so is not in the 'login' account chain, so it has no effect").arg(ruleFile));
    if (warnings.isEmpty() && hasAccess)
        r.info(QStringLiteral("console rule for the quiz group found in %1 (only a real console login proves it works: see PHASE4-TEST)").arg(ruleFile));
}

} // namespace

QList<CheckResult> runChecks(const SystemView &sys)
{
    Report r;

    checkDisplayManager(sys, r);
    const ThemeResolution theme = resolveSddmConfig(sys.root);
    checkSddmConfig(sys, theme, r);

    // EduConfig, exactly as the daemon loads it, but under the fixture root if there is one
    setConfigFileOverride(QString());
    const QString mainFile = sys.root + configFilePath();
    const QString dropInDir = sys.root + configDirPath();
    if (!sys.root.isEmpty())
        setConfigFileOverride(mainFile, dropInDir);
    const EduConfig config;

    const int dropIns = QDir(dropInDir).entryList(QDir::Files).size();
    if (!QFileInfo::exists(mainFile) && dropIns == 0)
        r.warn(QStringLiteral("no edu-lm configuration found (%1); using built-in defaults").arg(mainFile));
    else
        r.ok(QStringLiteral("configuration loaded (%1 and %2 drop-in files)").arg(mainFile).arg(dropIns));
    if (config.hasUnused())
        r.warn(QStringLiteral("the configuration has unknown sections or keys (typo?)"));
    const QStringList active = activeKeys(mainFile);
    if (!active.isEmpty())
        r.warn(QStringLiteral("%1 sets keys (%2); they override every drop-in. Move them to %3")
                   .arg(mainFile, active.join(QLatin1Char(',')), dropInDir));

    if (config.Quiz.Enabled.get())
        r.ok(QStringLiteral("quiz is enabled"));
    else
        r.warn(QStringLiteral("quiz is disabled (Enabled=false): nobody gets a quiz"));

    const QuizRules rules = quizRules(config);
    const QStringList problems = rules.validate();
    for (const QString &p : problems)
        r.fail(QStringLiteral("rules: %1").arg(p));
    if (problems.isEmpty())
        r.ok(QStringLiteral("rules are valid (%1 questions, %2 correct to pass, %3 swaps)")
                 .arg(rules.questionCount).arg(rules.requiredCorrect).arg(rules.maxSwaps));

    const QuizPolicyConfig policy = quizPolicyConfig(config);
    QStringList yearErrors;
    const auto yearGroups = parseYearGroups(policy.yearGroups, &yearErrors);
    for (const QString &e : std::as_const(yearErrors))
        r.fail(e);

    QStringList missing;
    for (const QString &g : policy.groups) {
        if (!sys.groupExists(g))
            missing << g;
    }
    if (!missing.isEmpty())
        r.fail(QStringLiteral("group(s) in Groups do not exist: %1 (groupadd)").arg(missing.join(QStringLiteral(", "))));
    QStringList missingYears;
    for (const auto &yg : yearGroups) {
        if (!sys.groupExists(yg.first))
            missingYears << yg.first;
    }
    if (!missingYears.isEmpty())
        r.warn(QStringLiteral("group(s) in YearGroups do not exist: %1").arg(missingYears.join(QStringLiteral(", "))));
    if (missing.isEmpty() && missingYears.isEmpty())
        r.ok(QStringLiteral("all configured groups exist"));

    for (const QString &u : policy.users) {
        if (sys.userGroups(u).isEmpty())
            r.warn(QStringLiteral("Users lists '%1' but there is no such account").arg(u));
    }

    const QList<QuizUser> users = quizUsers(policy, sys);
    if (users.isEmpty()) {
        r.warn(QStringLiteral("no user gets the quiz"));
    } else {
        QStringList described;
        for (const QuizUser &u : users)
            described << QStringLiteral("%1 (year %2)").arg(u.name, yearsText(u.years));
        r.ok(QStringLiteral("%1 quiz user(s): %2").arg(users.size()).arg(described.join(QStringLiteral(", "))));
    }

    // question bank
    QStringList dirs;
    for (const QString &d : config.Content.QuestionDirs.get())
        dirs << sys.root + d;
    QStringList readable;
    for (const QString &d : std::as_const(dirs)) {
        const QFileInfo info(d);
        if (!info.exists())
            r.warn(QStringLiteral("question directory %1 does not exist").arg(d));
        else if (!info.isReadable() || !info.isExecutable())
            r.info(QStringLiteral("run as root to read question directory %1").arg(d));
        else
            readable << d;
    }
    QuestionBank bank;
    bank.load(readable);
    if (!bank.errors().isEmpty())
        r.warn(QStringLiteral("%1 question(s) or pack(s) have errors: run edu-lm-validate").arg(bank.errors().size()));
    if (!readable.isEmpty() && bank.questions().isEmpty()) {
        r.fail(QStringLiteral("no questions loaded from %1").arg(readable.join(QLatin1Char(','))));
    } else if (!readable.isEmpty()) {
        r.ok(QStringLiteral("%1 questions loaded").arg(bank.questions().size()));
        QSet<QString> seen;
        for (const QuizUser &u : users) {
            const QString key = yearsText(u.years);
            if (seen.contains(key))
                continue;
            seen.insert(key);
            const int pool = static_cast<int>(bank.filter(u.years, config.Content.Subjects.get()).size());
            const QString what = QStringLiteral("year %1: %2 questions").arg(key).arg(pool);
            if (pool < rules.questionCount)
                r.fail(QStringLiteral("%1, but %2 are asked per attempt").arg(what).arg(rules.questionCount));
            else if (pool < rules.questionCount + rules.maxSwaps)
                r.warn(QStringLiteral("%1; %2 are needed for a full attempt with %3 swaps").arg(what).arg(rules.questionCount + rules.maxSwaps).arg(rules.maxSwaps));
            else
                r.ok(QStringLiteral("%1 (need %2 with swaps)").arg(what).arg(rules.questionCount + rules.maxSwaps));
        }
    }

    const QStringList perms = permissionProblems(dirs, sys.expectedOwner);
    for (const QString &p : perms)
        r.fail(QStringLiteral("permissions: %1").arg(p));
    if (perms.isEmpty())
        r.ok(QStringLiteral("question directories and files are root-only"));

    // passwords
    if (!config.Quiz.StudentPasswords.get()) {
        const PamChain chain = pamChain(sys.root, QStringLiteral("sddm"), QStringLiteral("auth"));
        for (const QString &p : chain.problems)
            r.warn(QStringLiteral("sddm PAM service: %1").arg(p));
        switch (nullokState(chain)) {
        case NullokState::All:
            r.ok(QStringLiteral("pam_unix in the sddm auth chain has nullok"));
            break;
        case NullokState::Missing:
            r.fail(QStringLiteral("StudentPasswords=false but pam_unix in the sddm auth chain has no nullok: empty passwords are refused"));
            break;
        case NullokState::NoPamUnix:
            r.warn(QStringLiteral("StudentPasswords=false but no pam_unix.so found in the sddm auth chain; cannot tell whether empty passwords work"));
            break;
        }
    }
    if (users.isEmpty()) {
        // nothing to check
    } else if (!sys.isRoot) {
        r.info(QStringLiteral("run as root to check the quiz users' passwords"));
    } else {
        const bool wantEmpty = !config.Quiz.StudentPasswords.get();
        int bad = 0;
        for (const QuizUser &u : users) {
            const PasswordState s = sys.passwordState(u.name);
            if (s == PasswordState::Unknown) {
                r.warn(QStringLiteral("cannot read the password of %1").arg(u.name));
                ++bad;
            } else if (s == PasswordState::Locked) {
                r.warn(QStringLiteral("%1 has a locked password (! or *); cannot log in").arg(u.name));
                ++bad;
            } else if (wantEmpty && s == PasswordState::Set) {
                r.warn(QStringLiteral("%1 has a password but StudentPasswords=false: passwd -d %1").arg(u.name));
                ++bad;
            } else if (!wantEmpty && s == PasswordState::Empty) {
                r.warn(QStringLiteral("%1 has an empty password but StudentPasswords=true: anyone can pass the first step").arg(u.name));
                ++bad;
            }
        }
        if (bad == 0)
            r.ok(QStringLiteral("quiz users' passwords match StudentPasswords=%1").arg(wantEmpty ? QStringLiteral("false") : QStringLiteral("true")));
    }

    // autologin
    if (theme.autologinUser.isEmpty()) {
        r.ok(QStringLiteral("no SDDM autologin"));
    } else if (decideQuiz(theme.autologinUser, sys.userGroups(theme.autologinUser), policy).quizRequired) {
        r.warn(QStringLiteral("SDDM autologin user '%1' is a quiz user; the daemon refuses that autologin").arg(theme.autologinUser));
    } else {
        r.ok(QStringLiteral("SDDM autologin user '%1' is not a quiz user").arg(theme.autologinUser));
    }

    checkConsoleLock(sys, config, r);
    return r.results();
}

}
