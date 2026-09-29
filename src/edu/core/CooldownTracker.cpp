// SPDX-License-Identifier: GPL-2.0-or-later
#include "CooldownTracker.h"

#include <chrono>

namespace EduLM {

CooldownTracker::CooldownTracker(Clock clock)
    : m_clock(std::move(clock))
{
}

CooldownTracker::Clock CooldownTracker::defaultClock()
{
    return [] {
        using namespace std::chrono;
        return static_cast<qint64>(duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
    };
}

void CooldownTracker::recordFailure(const QString &user, int cooldownSeconds)
{
    if (cooldownSeconds > 0)
        m_deadlines.insert(user, m_clock() + qint64(cooldownSeconds) * 1000);
}

int CooldownTracker::secondsLeft(const QString &user)
{
    const auto it = m_deadlines.constFind(user);
    if (it == m_deadlines.constEnd())
        return 0;
    const qint64 left = it.value() - m_clock();
    if (left <= 0) {
        m_deadlines.remove(user);
        return 0;
    }
    return static_cast<int>((left + 999) / 1000);
}

void CooldownTracker::clear(const QString &user)
{
    m_deadlines.remove(user);
}

}
