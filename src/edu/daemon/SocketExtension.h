// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <QtGlobal>

class QDataStream;
class QLocalSocket;

namespace EduLM {

// Lets SocketServer hand messages it doesn't know to someone else.
class SocketExtension
{
public:
    virtual ~SocketExtension() = default;

    // Returns true if the message was consumed. Must read exactly that message's payload,
    // since SocketServer keeps reading further messages from the same stream.
    virtual bool handleMessage(QLocalSocket *socket, quint32 message, QDataStream &in) = 0;
};

}
