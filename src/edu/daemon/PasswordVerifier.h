// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <QObject>
#include <QString>

namespace EduLM {

// Checks a user's password without opening a session.
class PasswordVerifier : public QObject
{
    Q_OBJECT
public:
    using QObject::QObject;

    // Reports through verified() exactly once per call.
    virtual void verify(const QString &user, const QString &password) = 0;

signals:
    void verified(bool ok);
};

}
