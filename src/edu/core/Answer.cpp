// SPDX-License-Identifier: GPL-2.0-or-later
#include "Answer.h"

#include <QRegularExpression>
#include <cmath>

namespace EduLM {

QString formatNumber(qint64 value)
{
    const bool negative = value < 0;
    QString digits = QString::number(negative ? -value : value);
    if (digits.size() >= 5) {
        for (int i = digits.size() - 3; i > 0; i -= 3)
            digits.insert(i, QChar(0x00A0));
    }
    return negative ? QLatin1Char('-') + digits : digits;
}

bool parseNumber(const QString &text, double *value)
{
    static const QRegularExpression plain(QStringLiteral("^-?(\\d+(\\.\\d+)?|\\.\\d+)$"));
    // one consistent separator between groups of three digits
    static const QRegularExpression grouped(QStringLiteral("^-?\\d{1,3}(?<sep>[ ,])\\d{3}(\\k<sep>\\d{3})*(\\.\\d+)?$"));

    QString s = text.trimmed();
    s.replace(QChar(0x2212), QLatin1Char('-'));
    s.replace(QChar(0x00A0), QLatin1Char(' '));

    if (!plain.match(s).hasMatch()) {
        if (!grouped.match(s).hasMatch())
            return false;
        s.remove(QLatin1Char(' '));
        s.remove(QLatin1Char(','));
    }

    bool ok = false;
    const double v = s.toDouble(&ok);
    if (!ok || !std::isfinite(v))
        return false;
    *value = v;
    return true;
}

static QString normaliseText(const QString &text)
{
    QString s = text.simplified();
    while (!s.isEmpty() && s.front().isPunct())
        s.remove(0, 1);
    while (!s.isEmpty() && s.back().isPunct())
        s.chop(1);
    return s.simplified().toCaseFolded();
}

static bool parseIndex(const QString &text, int *index)
{
    bool ok = false;
    const int v = text.trimmed().toInt(&ok);
    if (ok)
        *index = v;
    return ok;
}

static bool parseBool(const QString &text, bool *value)
{
    const QString s = text.trimmed().toCaseFolded();
    if (s == QLatin1String("true"))
        *value = true;
    else if (s == QLatin1String("false"))
        *value = false;
    else
        return false;
    return true;
}

bool isCorrect(const Question &q, const QString &answer)
{
    switch (q.type) {
    case QuestionType::MultipleChoice: {
        int index;
        return parseIndex(answer, &index) && index >= 0 && index == q.answerIndex;
    }
    case QuestionType::TrueFalse: {
        bool value;
        return parseBool(answer, &value) && value == q.answerBool;
    }
    case QuestionType::Numeric: {
        double value;
        return parseNumber(answer, &value) && std::fabs(value - q.answerNumber) <= q.tolerance + 1e-9;
    }
    case QuestionType::ShortText: {
        const QString given = normaliseText(answer);
        if (given.isEmpty())
            return false;
        for (const QString &accepted : q.accept) {
            if (normaliseText(accepted) == given)
                return true;
        }
        return false;
    }
    case QuestionType::Generator:
        break;
    }
    return false;
}

static QString numberText(double value)
{
    if (value == std::floor(value) && std::fabs(value) < 1e15)
        return formatNumber(static_cast<qint64>(value));
    return QString::number(value, 'g', 10);
}

QString correctAnswerText(const Question &q)
{
    switch (q.type) {
    case QuestionType::MultipleChoice:
        return q.choices.value(q.answerIndex);
    case QuestionType::TrueFalse:
        return q.answerBool ? QStringLiteral("True") : QStringLiteral("False");
    case QuestionType::Numeric:
        return numberText(q.answerNumber);
    case QuestionType::ShortText:
        return q.accept.value(0);
    case QuestionType::Generator:
        break;
    }
    return QString();
}

QString displayAnswer(const Question &q, const QString &answer)
{
    int index;
    bool value;
    if (q.type == QuestionType::MultipleChoice && parseIndex(answer, &index)
        && index >= 0 && index < q.choices.size())
        return q.choices.at(index);
    if (q.type == QuestionType::TrueFalse && parseBool(answer, &value))
        return value ? QStringLiteral("True") : QStringLiteral("False");
    return answer.simplified();
}

}
