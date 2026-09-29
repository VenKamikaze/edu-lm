// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "PasswordVerifier.h"

#include "Auth.h"

#include <QPointer>
#include <QTimer>

namespace EduLM {

// Verify-only PAM check: sddm-helper without a session path authenticates and exits.
// Each check uses its own Auth, separate from the Display's.
class AuthVerifier : public PasswordVerifier
{
    Q_OBJECT
public:
    explicit AuthVerifier(QObject *parent = nullptr);

    // How long a check may take before it counts as failed.
    void setTimeoutMs(int ms) { m_timeout.setInterval(ms); }

    void verify(const QString &user, const QString &password) override;

private slots:
    void onRequestChanged();
    void onAuthentication(const QString &user, bool success);
    void onError(const QString &message, SDDM::Auth::Error error);
    void onFinished(SDDM::Auth::HelperExitStatus status);
    void onTimeout();

private:
    void report(bool ok);
    void cleanup();

    QPointer<SDDM::Auth> m_auth;
    QString m_password;
    QTimer m_timeout;
    bool m_reported = false;
};

}
