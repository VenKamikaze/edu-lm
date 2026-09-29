// SPDX-License-Identifier: GPL-2.0-or-later
#include "core/QuestionBank.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QTextStream>

using namespace EduLM;

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QTextStream out(stdout);
    QTextStream err(stderr);

    const QStringList paths = app.arguments().mid(1);
    if (paths.isEmpty()) {
        err << "usage: edu-lm-validate <dir-or-file>...\n";
        return 2;
    }

    // A path given on the command line that doesn't exist is a mistake, not a skippable warning.
    int missing = 0;
    QStringList existing;
    for (const QString &path : paths) {
        if (QFileInfo::exists(path)) {
            existing << path;
        } else {
            err << "error: " << path << ": no such file or directory\n";
            ++missing;
        }
    }

    QuestionBank bank;
    bank.load(existing);

    for (const LoadIssue &w : bank.warnings())
        err << "warning: " << w.toString() << "\n";
    for (const LoadIssue &e : bank.errors())
        err << "error: " << e.toString() << "\n";

    out << "\n" << bank.questions().size() << " questions loaded\n";
    const auto counts = bank.counts();
    for (auto s = counts.begin(); s != counts.end(); ++s) {
        out << s.key() << ":";
        for (auto y = s.value().begin(); y != s.value().end(); ++y)
            out << " year " << y.key() << "=" << y.value();
        out << "\n";
    }

    const int errorCount = bank.errors().size() + missing;
    out << errorCount << " errors, " << bank.warnings().size() << " warnings\n";
    return errorCount == 0 ? 0 : 1;
}
