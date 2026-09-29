// SPDX-License-Identifier: GPL-2.0-or-later
#include "core/Answer.h"
#include "core/Generators.h"
#include "core/QuestionBank.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QTextStream>

using namespace EduLM;

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QTextStream out(stdout);
    QTextStream err(stderr);

    QStringList paths = app.arguments().mid(1);
    int samples = 0;
    if (paths.value(0) == QLatin1String("--sample")) {
        bool ok = false;
        samples = paths.value(1).toInt(&ok);
        if (!ok || samples < 1) {
            err << "error: --sample needs a positive number\n";
            return 2;
        }
        paths = paths.mid(2);
    }
    if (paths.isEmpty()) {
        err << "usage: edu-lm-validate [--sample N] <dir-or-file>...\n";
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

    // Fixed seed: the same samples every run. One JSON object per line, for tools to read.
    QRandomGenerator rng(1);
    for (const Question &q : bank.questions()) {
        if (q.type != QuestionType::Generator)
            continue;
        for (int i = 0; i < samples; ++i) {
            const Question instance = instantiateGenerator(q, rng);
            const QJsonObject o{{QStringLiteral("id"), q.id},
                                {QStringLiteral("prompt"), instance.prompt},
                                {QStringLiteral("answer"), correctAnswerText(instance)}};
            out << "sample " << QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact)) << "\n";
        }
    }

    const int errorCount = bank.errors().size() + missing;
    out << errorCount << " errors, " << bank.warnings().size() << " warnings\n";
    return errorCount == 0 ? 0 : 1;
}
