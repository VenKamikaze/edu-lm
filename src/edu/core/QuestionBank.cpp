// SPDX-License-Identifier: GPL-2.0-or-later
#include "QuestionBank.h"

#include "Generators.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <cmath>

namespace EduLM {

constexpr int MinYear = 1;
constexpr int MaxYear = 12;

QString LoadIssue::toString() const
{
    QString s = file;
    if (!question.isEmpty())
        s += QStringLiteral(": question %1").arg(question);
    return s + QStringLiteral(": ") + message;
}

void QuestionBank::error(const QString &file, const QString &question, const QString &message)
{
    m_errors.append({file, question, message});
}

void QuestionBank::warning(const QString &file, const QString &question, const QString &message)
{
    m_warnings.append({file, question, message});
}

void QuestionBank::load(const QStringList &paths)
{
    for (const QString &path : paths) {
        const QFileInfo info(path);
        if (info.isDir())
            loadDirectory(path);
        else if (info.isFile())
            loadFile(path);
        else
            warning(path, QString(), QStringLiteral("no such file or directory"));
    }
}

void QuestionBank::loadDirectory(const QString &dirPath)
{
    const QStringList files = QDir(dirPath).entryList({QStringLiteral("*.json")}, QDir::Files, QDir::Name);
    if (files.isEmpty())
        warning(dirPath, QString(), QStringLiteral("no .json files found"));
    for (const QString &name : files)
        loadFile(QDir(dirPath).filePath(name));
}

static bool readInt(const QJsonValue &v, int *out)
{
    if (!v.isDouble())
        return false;
    const double d = v.toDouble();
    if (d != std::floor(d) || std::fabs(d) > 1e9)
        return false;
    *out = static_cast<int>(d);
    return true;
}

static bool readStringList(const QJsonValue &v, QStringList *out)
{
    if (!v.isArray())
        return false;
    for (const QJsonValue &item : v.toArray()) {
        if (!item.isString())
            return false;
        out->append(item.toString());
    }
    return true;
}

void QuestionBank::loadFile(const QString &file)
{
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly)) {
        error(file, QString(), QStringLiteral("cannot open: %1").arg(f.errorString()));
        return;
    }
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &parseError);
    if (doc.isNull()) {
        error(file, QString(), QStringLiteral("invalid JSON: %1 (offset %2)").arg(parseError.errorString()).arg(parseError.offset));
        return;
    }
    if (!doc.isObject()) {
        error(file, QString(), QStringLiteral("top level must be an object"));
        return;
    }

    const QJsonObject pack = doc.object();
    int schema = 0;
    if (!readInt(pack.value(QStringLiteral("schema")), &schema) || schema != 1) {
        error(file, QString(), QStringLiteral("unsupported or missing schema (expected 1)"));
        return;
    }
    if (!pack.value(QStringLiteral("questions")).isArray()) {
        error(file, QString(), QStringLiteral("missing 'questions' array"));
        return;
    }

    static const QSet<QString> packKeys = {
        QStringLiteral("schema"), QStringLiteral("pack"), QStringLiteral("subject"), QStringLiteral("questions"),
    };
    for (const QString &key : pack.keys()) {
        if (!packKeys.contains(key))
            warning(file, QString(), QStringLiteral("unknown pack field '%1' ignored").arg(key));
    }

    const QString packName = pack.value(QStringLiteral("pack")).toString().trimmed();
    const QString packSubject = pack.value(QStringLiteral("subject")).toString().trimmed().toLower();
    if (packName.isEmpty())
        warning(file, QString(), QStringLiteral("missing 'pack' name"));

    static const QSet<QString> knownKeys = {
        QStringLiteral("id"), QStringLiteral("years"), QStringLiteral("subject"), QStringLiteral("curriculum"),
        QStringLiteral("type"), QStringLiteral("prompt"), QStringLiteral("choices"), QStringLiteral("answer"),
        QStringLiteral("tolerance"), QStringLiteral("accept"), QStringLiteral("generator"),
        QStringLiteral("params"), QStringLiteral("difficulty"), QStringLiteral("explanation"),
    };

    const QJsonArray entries = pack.value(QStringLiteral("questions")).toArray();
    for (int index = 0; index < entries.size(); ++index) {
        const QJsonObject o = entries.at(index).toObject();
        Question q;
        q.id = o.value(QStringLiteral("id")).toString().trimmed();
        const QString label = q.id.isEmpty() ? QStringLiteral("#%1").arg(index) : q.id;
        QStringList problems;
        const auto fail = [&](const QString &message) { problems.append(message); };

        if (!entries.at(index).isObject())
            fail(QStringLiteral("entry is not an object"));
        if (q.id.isEmpty())
            fail(QStringLiteral("missing id"));
        else if (m_ids.contains(q.id))
            fail(QStringLiteral("duplicate id"));

        q.pack = packName;
        q.subject = o.contains(QStringLiteral("subject")) ? o.value(QStringLiteral("subject")).toString().trimmed().toLower() : packSubject;
        if (q.subject.isEmpty())
            fail(QStringLiteral("no subject (set it on the question or the pack)"));

        const QJsonValue years = o.value(QStringLiteral("years"));
        if (!years.isArray() || years.toArray().isEmpty())
            fail(QStringLiteral("'years' must be a non-empty array of integers"));
        else {
            for (const QJsonValue &y : years.toArray()) {
                int year;
                if (!readInt(y, &year) || year < MinYear || year > MaxYear)
                    fail(QStringLiteral("bad year (must be an integer %1-%2)").arg(MinYear).arg(MaxYear));
                else if (!q.years.contains(year))
                    q.years.append(year);
            }
        }

        const QString type = o.value(QStringLiteral("type")).toString();
        q.prompt = o.value(QStringLiteral("prompt")).toString().trimmed();
        q.explanation = o.value(QStringLiteral("explanation")).toString().trimmed();
        q.curriculum = o.value(QStringLiteral("curriculum")).toString().trimmed();
        if (o.contains(QStringLiteral("difficulty")) && !readInt(o.value(QStringLiteral("difficulty")), &q.difficulty))
            fail(QStringLiteral("'difficulty' must be an integer"));

        const QJsonValue answer = o.value(QStringLiteral("answer"));

        if (type == QLatin1String("multiple_choice")) {
            q.type = QuestionType::MultipleChoice;
            if (q.prompt.isEmpty())
                fail(QStringLiteral("empty prompt"));
            if (!readStringList(o.value(QStringLiteral("choices")), &q.choices))
                fail(QStringLiteral("'choices' must be an array of strings"));
            else if (q.choices.size() < 2)
                fail(QStringLiteral("multiple_choice needs at least 2 choices"));
            else {
                QSet<QString> seen;
                for (const QString &c : std::as_const(q.choices)) {
                    const QString t = c.trimmed();
                    if (t.isEmpty())
                        fail(QStringLiteral("empty choice"));
                    else if (seen.contains(t))
                        fail(QStringLiteral("duplicate choice '%1'").arg(t));
                    seen.insert(t);
                }
                if (!readInt(answer, &q.answerIndex) || q.answerIndex < 0 || q.answerIndex >= q.choices.size())
                    fail(QStringLiteral("answer must be an index 0-%1").arg(q.choices.size() - 1));
            }
        } else if (type == QLatin1String("true_false")) {
            q.type = QuestionType::TrueFalse;
            if (q.prompt.isEmpty())
                fail(QStringLiteral("empty prompt"));
            if (!answer.isBool())
                fail(QStringLiteral("answer must be true or false"));
            q.answerBool = answer.toBool();
        } else if (type == QLatin1String("numeric")) {
            q.type = QuestionType::Numeric;
            if (q.prompt.isEmpty())
                fail(QStringLiteral("empty prompt"));
            if (!answer.isDouble())
                fail(QStringLiteral("answer must be a number"));
            q.answerNumber = answer.toDouble();
            if (o.contains(QStringLiteral("tolerance"))) {
                const QJsonValue t = o.value(QStringLiteral("tolerance"));
                if (!t.isDouble() || t.toDouble() < 0)
                    fail(QStringLiteral("'tolerance' must be a number >= 0"));
                q.tolerance = t.toDouble();
            }
        } else if (type == QLatin1String("short_text")) {
            q.type = QuestionType::ShortText;
            if (q.prompt.isEmpty())
                fail(QStringLiteral("empty prompt"));
            if (!readStringList(o.value(QStringLiteral("accept")), &q.accept) || q.accept.isEmpty())
                fail(QStringLiteral("'accept' must be a non-empty array of strings"));
            for (const QString &a : std::as_const(q.accept)) {
                if (a.simplified().isEmpty())
                    fail(QStringLiteral("empty accepted answer"));
            }
        } else if (type == QLatin1String("generator")) {
            q.type = QuestionType::Generator;
            q.generator = o.value(QStringLiteral("generator")).toString();
            if (!isKnownGenerator(q.generator))
                fail(QStringLiteral("unknown generator '%1'").arg(q.generator));
            else {
                const QJsonValue params = o.value(QStringLiteral("params"));
                if (o.contains(QStringLiteral("params")) && !params.isObject())
                    fail(QStringLiteral("'params' must be an object"));
                const QJsonObject po = params.toObject();
                for (auto p = po.begin(); p != po.end(); ++p) {
                    const QJsonArray range = p.value().toArray();
                    IntRange r;
                    if (range.size() != 2 || !readInt(range.at(0), &r.lo) || !readInt(range.at(1), &r.hi))
                        fail(QStringLiteral("param '%1' must be [min, max] integers").arg(p.key()));
                    q.params.insert(p.key(), r);
                }
                if (problems.isEmpty()) {
                    const QString e = resolveGeneratorParams(q.generator, &q.params);
                    if (!e.isEmpty())
                        fail(e);
                }
            }
        } else {
            fail(type.isEmpty() ? QStringLiteral("missing type") : QStringLiteral("unknown type '%1'").arg(type));
        }

        for (const QString &key : o.keys()) {
            if (!knownKeys.contains(key))
                warning(file, label, QStringLiteral("unknown field '%1' ignored").arg(key));
        }

        if (!problems.isEmpty()) {
            error(file, label, problems.join(QStringLiteral("; ")) + QStringLiteral(" (question skipped)"));
            continue;
        }
        m_ids.insert(q.id);
        m_questions.append(q);
    }
}

QList<Question> QuestionBank::filter(int year, const QStringList &subjects) const
{
    QStringList wanted;
    for (const QString &s : subjects)
        wanted.append(s.trimmed().toLower());

    QList<Question> result;
    for (const Question &q : m_questions) {
        if (q.years.contains(year) && wanted.contains(q.subject))
            result.append(q);
    }
    return result;
}

QMap<QString, QMap<int, int>> QuestionBank::counts() const
{
    QMap<QString, QMap<int, int>> result;
    for (const Question &q : m_questions) {
        for (int year : q.years)
            result[q.subject][year]++;
    }
    return result;
}

}
