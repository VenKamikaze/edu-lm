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

    explicit CooldownTracker(Clock clock = defaultClock());

    // The cooldown length comes with each failure, so config changes apply to the next one.
    void recordFailure(const QString &user, int cooldownSeconds);
    // Whole seconds remaining, rounded up; 0 when there is no cooldown.
    int secondsLeft(const QString &user);
    void clear(const QString &user);

    static Clock defaultClock();

private:
    Clock m_clock;
    QHash<QString, qint64> m_deadlines;
};

}
