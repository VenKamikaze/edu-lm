// SPDX-License-Identifier: GPL-2.0-or-later
#include "Generators.h"

#include "Answer.h"

#include <algorithm>

namespace EduLM {

namespace {

constexpr int MaxOperand = 100000;
constexpr int MaxDigits = 7;

// Param names and default ranges per generator. Divide uses quotient x divisor.
const QMap<QString, QMap<QString, IntRange>> &defaults()
{
    static const QMap<QString, QMap<QString, IntRange>> d = {
        {QStringLiteral("add"), {{QStringLiteral("a"), {1, 20}}, {QStringLiteral("b"), {1, 20}}}},
        {QStringLiteral("subtract"), {{QStringLiteral("a"), {1, 20}}, {QStringLiteral("b"), {1, 20}}}},
        {QStringLiteral("multiply"), {{QStringLiteral("a"), {2, 10}}, {QStringLiteral("b"), {2, 10}}}},
        {QStringLiteral("divide"), {{QStringLiteral("quotient"), {2, 10}}, {QStringLiteral("divisor"), {2, 10}}}},
        {QStringLiteral("place_value"), {{QStringLiteral("digits"), {3, 4}}}},
    };
    return d;
}

int draw(QRandomGenerator &rng, IntRange range)
{
    return range.lo + static_cast<int>(rng.bounded(range.hi - range.lo + 1));
}

Question makeInstance(const Question &entry, const QString &operands, const QString &prompt, qint64 answer)
{
    Question q;
    q.id = entry.id + QLatin1Char('#') + operands;
    q.pack = entry.pack;
    q.subject = entry.subject;
    q.type = QuestionType::Numeric;
    q.years = entry.years;
    q.curriculum = entry.curriculum;
    q.difficulty = entry.difficulty;
    q.prompt = prompt;
    q.answerNumber = static_cast<double>(answer);
    return q;
}

Question placeValue(const Question &entry, QRandomGenerator &rng)
{
    // an unresolved entry has no params; never loop on fewer than 2 digits
    const int digits = qBound(2, draw(rng, entry.params.value(QStringLiteral("digits"), {2, 2})), MaxDigits);
    // The chosen digit must appear once, or "the value of the 4" is ambiguous.
    for (;;) {
        QList<int> d;
        for (int i = 0; i < digits; ++i)
            d.append(i == 0 ? 1 + static_cast<int>(rng.bounded(9)) : static_cast<int>(rng.bounded(10)));

        QList<int> candidates;
        for (int i = 0; i < digits; ++i) {
            if (d[i] != 0 && std::count(d.begin(), d.end(), d[i]) == 1)
                candidates.append(i);
        }
        if (candidates.isEmpty())
            continue;

        const int pos = candidates.at(static_cast<int>(rng.bounded(static_cast<int>(candidates.size()))));
        qint64 number = 0;
        for (int v : d)
            number = number * 10 + v;
        qint64 place = 1;
        for (int i = pos + 1; i < digits; ++i)
            place *= 10;

        return makeInstance(entry, QStringLiteral("%1:%2").arg(number).arg(d[pos]),
                            QStringLiteral("What is the value of the %1 in %2?").arg(d[pos]).arg(formatNumber(number)),
                            d[pos] * place);
    }
}

}

bool isKnownGenerator(const QString &name)
{
    return defaults().contains(name);
}

QString resolveGeneratorParams(const QString &generator, QMap<QString, IntRange> *params)
{
    const auto it = defaults().constFind(generator);
    if (it == defaults().constEnd())
        return QStringLiteral("unknown generator '%1'").arg(generator);

    for (auto p = params->constBegin(); p != params->constEnd(); ++p) {
        if (!it->contains(p.key()))
            return QStringLiteral("unknown param '%1' for generator '%2'").arg(p.key(), generator);
    }

    for (auto d = it->constBegin(); d != it->constEnd(); ++d) {
        if (!params->contains(d.key()))
            params->insert(d.key(), d.value());
    }

    const bool isDigits = generator == QLatin1String("place_value");
    const int upper = isDigits ? MaxDigits : MaxOperand;
    for (auto p = params->constBegin(); p != params->constEnd(); ++p) {
        const IntRange r = p.value();
        const int lower = isDigits ? 2 : (p.key() == QLatin1String("divisor") ? 1 : 0);
        if (r.lo > r.hi)
            return QStringLiteral("param '%1' has min %2 greater than max %3").arg(p.key()).arg(r.lo).arg(r.hi);
        if (r.lo < lower || r.hi > upper)
            return QStringLiteral("param '%1' must be within %2..%3").arg(p.key()).arg(lower).arg(upper);
    }
    return QString();
}

Question instantiateGenerator(const Question &entry, QRandomGenerator &rng)
{
    const auto param = [&](const char *name) { return entry.params.value(QLatin1String(name)); };

    if (entry.generator == QLatin1String("place_value"))
        return placeValue(entry, rng);

    if (entry.generator == QLatin1String("divide")) {
        const int quotient = draw(rng, param("quotient"));
        const int divisor = draw(rng, param("divisor"));
        const qint64 dividend = static_cast<qint64>(quotient) * divisor;
        return makeInstance(entry, QStringLiteral("%1/%2").arg(dividend).arg(divisor),
                            QStringLiteral("What is %1 ÷ %2?").arg(formatNumber(dividend), formatNumber(divisor)), quotient);
    }

    int a = draw(rng, param("a"));
    int b = draw(rng, param("b"));
    if (entry.generator == QLatin1String("add"))
        return makeInstance(entry, QStringLiteral("%1+%2").arg(a).arg(b),
                            QStringLiteral("What is %1 + %2?").arg(formatNumber(a), formatNumber(b)), static_cast<qint64>(a) + b);
    if (entry.generator == QLatin1String("subtract")) {
        if (a < b)
            std::swap(a, b);
        return makeInstance(entry, QStringLiteral("%1-%2").arg(a).arg(b),
                            QStringLiteral("What is %1 \u2212 %2?").arg(formatNumber(a), formatNumber(b)), static_cast<qint64>(a) - b);
    }
    return makeInstance(entry, QStringLiteral("%1x%2").arg(a).arg(b),
                        QStringLiteral("What is %1 × %2?").arg(formatNumber(a), formatNumber(b)), static_cast<qint64>(a) * b);
}

}
