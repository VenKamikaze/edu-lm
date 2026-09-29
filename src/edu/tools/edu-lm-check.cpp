// SPDX-License-Identifier: GPL-2.0-or-later
#include "EduCheck.h"

#include <QCoreApplication>
#include <QTextStream>

using namespace EduLM;

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QTextStream out(stdout);

    // EDU_LM_CHECK_ROOT lets the checks run against a copy of the system files
    SystemView system = realSystem();
    system.root = qEnvironmentVariable("EDU_LM_CHECK_ROOT");

    if (app.arguments().size() > 1) {
        QTextStream(stderr) << "usage: edu-lm-check\n";
        return 2;
    }

    int fails = 0;
    int warns = 0;
    for (const CheckResult &r : runChecks(system)) {
        out << levelName(r.level).leftJustified(4) << "  " << r.text << "\n";
        fails += r.level == Level::Fail;
        warns += r.level == Level::Warn;
    }
    out << "\n" << fails << " failed, " << warns << " warnings\n";
    return fails == 0 ? 0 : 1;
}
