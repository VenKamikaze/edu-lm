// SPDX-License-Identifier: GPL-2.0-or-later
#include "AuthVerifier.h"

#include <QDebug>

namespace EduLM {

AuthVerifier::AuthVerifier(QObject *parent)
    : PasswordVerifier(parent)
{
    m_timeout.setSingleShot(true);
    m_timeout.setInterval(60000);
    connect(&m_timeout, &QTimer::timeout, this, &AuthVerifier::onTimeout);
}

void AuthVerifier::verify(const QString &user, const QString &password)
{
    if (m_auth) {
        emit verified(false);
        return;
    }

    m_password = password;
    m_reported = false;
    m_auth = new SDDM::Auth(this);
    connect(m_auth, &SDDM::Auth::requestChanged, this, &AuthVerifier::onRequestChanged);
    connect(m_auth, &SDDM::Auth::authentication, this, &AuthVerifier::onAuthentication);
    connect(m_auth, &SDDM::Auth::error, this, &AuthVerifier::onError);
    connect(m_auth, &SDDM::Auth::finished, this, &AuthVerifier::onFinished);
    m_auth->setUser(user);
    m_timeout.start();
    m_auth->start();
}

// Same prompt handling as Display::slotRequestChanged.
void AuthVerifier::onRequestChanged()
{
    if (!m_auth)
        return;
    if (m_auth->request()->prompts().length() == 1) {
        m_auth->request()->prompts()[0]->setResponse(qPrintable(m_password));
        m_auth->request()->done();
    } else if (m_auth->request()->prompts().length() == 2) {
        m_auth->request()->prompts()[0]->setResponse(qPrintable(m_auth->user()));
        m_auth->request()->prompts()[1]->setResponse(qPrintable(m_password));
        m_auth->request()->done();
    }
}

void AuthVerifier::onAuthentication(const QString &user, bool success)
{
    Q_UNUSED(user);
    report(success);
}

void AuthVerifier::onError(const QString &message, SDDM::Auth::Error error)
{
    qWarning() << "Edu-LM: password check error:" << error << message;
    if (!m_auth)
        return;
    if (error != SDDM::Auth::ERROR_AUTHENTICATION && error != SDDM::Auth::ERROR_INTERNAL)
        return;
    report(false);
    // a helper that failed to start never emits finished()
    if (!m_auth->isActive())
        cleanup();
}

// The helper is gone: anything not reported yet (crash, failed start) counts as a failure.
void AuthVerifier::onFinished(SDDM::Auth::HelperExitStatus status)
{
    Q_UNUSED(status);
    if (!m_auth)
        return;
    report(false);
    cleanup();
}

// A crashed helper emits error() twice and then finished(); nothing may reach us after this.
void AuthVerifier::cleanup()
{
    if (!m_auth)
        return;
    m_auth->disconnect(this);
    m_auth->deleteLater(); // its destructor stops a helper that is still running
    m_auth = nullptr;
}

void AuthVerifier::onTimeout()
{
    qWarning() << "Edu-LM: password check timed out";
    report(false);
    cleanup();
}

void AuthVerifier::report(bool ok)
{
    if (m_reported)
        return;
    m_reported = true;
    m_timeout.stop();
    // best effort: copies of the password held elsewhere are not wiped
    m_password.fill(QChar(0));
    m_password.clear();
    emit verified(ok);
}

}
