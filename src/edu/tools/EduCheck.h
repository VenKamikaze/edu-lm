// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "core/QuizPolicy.h"

#include <QList>
#include <QString>
#include <QStringList>

#include <functional>

namespace EduLM {

enum class Level { Ok, Info, Warn, Fail };

struct CheckResult {
    Level level;
    QString text;
};

enum class PasswordState { Unknown, Empty, Set, Locked };

// Everything the checks read from the system that is not a plain file under `root`.
// realSystem() uses libc; tests substitute fixtures.
struct SystemView {
    QString root; // prefix for every system path; empty on a real system
    bool isRoot = true;
    uint expectedOwner = 0; // uid that must own the question packs
    std::function<bool(const QString &group)> groupExists;
    std::function<QStringList(const QString &group)> groupMembers; // primary and supplementary
    std::function<QStringList(const QString &user)> userGroups;
    std::function<PasswordState(const QString &user)> passwordState;
};

SystemView realSystem();

// --- PAM ---

struct PamEntry {
    QString type; // auth, account, password or session, without a "-" prefix
    QString control;
    QString module; // as written, possibly a full path
    QStringList args;

    QString moduleName() const; // file name only
};

struct PamChain {
    QList<PamEntry> entries;
    QStringList problems; // unreadable or missing included files
};

// The entries of one type that a service under <root>/etc/pam.d runs, with include,
// substack and "@include" followed.
PamChain pamChain(const QString &root, const QString &service, const QString &type);

enum class NullokState { NoPamUnix, All, Missing };
NullokState nullokState(const PamChain &authChain);

// --- pieces with logic of their own ---

struct ThemeResolution {
    QString current;
    QString source; // last file that set Theme/Current; empty if none did
    QString themeDir;
    QString autologinUser;
};

// Reads SDDM's config the way the daemon does: /usr/lib/sddm/sddm.conf.d, then
// /etc/sddm.conf.d, then /etc/sddm.conf, each later one overriding.
ThemeResolution resolveSddmConfig(const QString &root);

struct QuizUser {
    QString name;
    QList<int> years;
};

// Members of the quiz groups plus Users, as decided by the daemon's own decideQuiz().
QList<QuizUser> quizUsers(const QuizPolicyConfig &policy, const SystemView &system);

// Ownership and mode problems under the question directories; empty when all is well.
QStringList permissionProblems(const QStringList &dirs, uint expectedOwner);

// All checks, in display order.
QList<CheckResult> runChecks(const SystemView &system);

QString levelName(Level level);

}
