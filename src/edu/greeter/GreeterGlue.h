// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <QString>

class QObject;

namespace SDDM { class GreeterProxy; }

namespace EduLM {

// The greeter's QuizClient, as a QObject for the context property: on the daemon's socket
// normally, on an in-process mock daemon in test mode.
QObject *createQuizClient(SDDM::GreeterProxy *proxy, const QString &socketName, bool testMode, QObject *parent);

}
