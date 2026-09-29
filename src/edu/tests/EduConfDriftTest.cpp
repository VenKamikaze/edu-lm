// SPDX-License-Identifier: GPL-2.0-or-later
#include "EduConfiguration.h"

#include <QtTest>

using namespace EduLM;

// Keeps the comments-only edu-lm.conf in step with EduConfig: every key documented, with
// the compiled-in default, and nothing documented that the code does not know.
class EduConfDriftTest : public QObject
{
    Q_OBJECT

private slots:
    void everyEntryIsDocumentedWithItsDefault();
    void settingsAreCommentedAndDescribed();
};

struct Documented {
    QString section, key, value;
};

static QList<Documented> documentedSettings(QStringList *lines = nullptr)
{
    QFile f(QStringLiteral(EDU_GENERATED_CONF));
    if (!f.open(QIODevice::ReadOnly))
        return {};
    QList<Documented> found;
    QString section;
    const QStringList all = QString::fromUtf8(f.readAll()).split(QLatin1Char('\n'));
    for (const QString &line : all) {
        if (lines)
            lines->append(line);
        if (line.startsWith(QLatin1Char('[')) && line.endsWith(QLatin1Char(']')))
            section = line.mid(1, line.size() - 2);
        else if (line.size() > 1 && line.at(0) == QLatin1Char('#') && line.at(1).isLetter() && line.contains(QLatin1Char('='))) {
            const int eq = line.indexOf(QLatin1Char('='));
            found.append({section, line.mid(1, eq - 1), line.mid(eq + 1)});
        }
    }
    return found;
}

void EduConfDriftTest::everyEntryIsDocumentedWithItsDefault()
{
    const QList<Documented> docs = documentedSettings();
    QVERIFY2(!docs.isEmpty(), "edu-lm.conf not found or has no documented settings");

    // the file as an admin would see it with every setting uncommented
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString uncommented = dir.filePath(QStringLiteral("uncommented.conf"));
    {
        QFile out(uncommented);
        QVERIFY(out.open(QIODevice::WriteOnly));
        QString section;
        for (const Documented &d : docs) {
            if (d.section != section)
                out.write(QStringLiteral("[%1]\n").arg(section = d.section).toUtf8());
            out.write(QStringLiteral("%1=%2\n").arg(d.key, d.value).toUtf8());
        }
    }

    setConfigFileOverride(QStringLiteral("/nonexistent/edu-lm.conf"));
    const EduConfig defaults;
    setConfigFileOverride(uncommented);
    const EduConfig loaded;
    setConfigFileOverride(QString());

    QVERIFY2(!loaded.hasUnused(), "edu-lm.conf documents a section or key EduConfig does not know");
    QVERIFY(loaded.entries().isEmpty()); // nothing lives outside a section

    QSet<QString> documented;
    for (const Documented &d : docs) {
        QVERIFY2(!documented.contains(d.section + d.key), qPrintable(d.section + "." + d.key + " is documented twice"));
        documented.insert(d.section + d.key);
    }

    QSet<QString> known;
    for (const SDDM::ConfigSection *section : {static_cast<const SDDM::ConfigSection *>(&loaded.Quiz), static_cast<const SDDM::ConfigSection *>(&loaded.Content)}) {
        for (const SDDM::ConfigEntryBase *entry : section->entries()) {
            const QString name = section->name() + QLatin1Char('.') + entry->name();
            known.insert(section->name() + entry->name());
            QVERIFY2(documented.contains(section->name() + entry->name()), qPrintable(name + " is missing from edu-lm.conf"));

            const SDDM::ConfigSection *base = section->name() == QLatin1String("Quiz") ? static_cast<const SDDM::ConfigSection *>(&defaults.Quiz)
                                                                                   : static_cast<const SDDM::ConfigSection *>(&defaults.Content);
            // the documented text itself, and what EduConfig makes of it
            const QString expected = base->entry(entry->name())->value();
            QCOMPARE(entry->value(), expected);
            for (const Documented &d : docs) {
                if (d.section == section->name() && d.key == entry->name())
                    QVERIFY2(d.value == expected, qPrintable(name + " is documented as '" + d.value + "', default is '" + expected + "'"));
            }
        }
    }
    QCOMPARE(documented, known);
}

void EduConfDriftTest::settingsAreCommentedAndDescribed()
{
    // A setting line that is not commented would override every drop-in; each one also
    // needs the "# " description line above it.
    QStringList lines;
    QVERIFY(!documentedSettings(&lines).isEmpty());
    for (int i = 0; i < lines.size(); ++i) {
        const QString &line = lines.at(i);
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')) || line.startsWith(QLatin1Char('[')))
            continue;
        QFAIL(qPrintable(QStringLiteral("active line in edu-lm.conf: %1").arg(line)));
    }
    for (int i = 0; i < lines.size(); ++i) {
        if (lines.at(i).size() > 1 && lines.at(i).at(0) == QLatin1Char('#') && lines.at(i).at(1).isLetter() && lines.at(i).contains(QLatin1Char('='))) {
            QVERIFY2(i > 0 && lines.at(i - 1).startsWith(QLatin1String("# ")),
                     qPrintable(QStringLiteral("no description above: %1").arg(lines.at(i))));
        }
    }
}

QTEST_APPLESS_MAIN(EduConfDriftTest)
#include "EduConfDriftTest.moc"
