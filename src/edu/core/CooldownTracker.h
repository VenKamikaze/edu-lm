// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <QHash>
#include <QString>

#include <functional>

namespace EduLM {

class CooldownTracker
{
public:
    // Monotonic milliseconds.
    using Clock = std::function<qint64()>;

    explicit CooldownTracker(int cooldownSeconds, Clock clock = defaultClock());

    void recordFailure(const QString &user);
    // Whole seconds remaining, rounded up; 0 when there is no cooldown.
    int secondsLeft(const QString &user);
    void clear(const QString &user);

    static Clock defaultClock();

private:
    qint64 m_cooldownMs;
    Clock m_clock;
    QHash<QString, qint64> m_deadlines;
};

}
