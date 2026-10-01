// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "EduMessages.h"

#include <QByteArray>
#include <QDataStream>
#include <QIODevice>
#include <QList>
#include <QString>
#include <QStringList>

namespace EduLM {

// Greeter -> daemon payloads (QuizHello, QuizAbandon and QuizNext carry none).
struct QuizQueryMsg {
    QString user;
};

struct QuizAnswerMsg {
    QString questionId;
    QString answer;
};

struct QuizSwapMsg {
    QString questionId;
};

// Daemon -> greeter payloads.
struct QuizStatusMsg {
    QString user;
    bool required = false;
    bool studentPasswords = true;
    qint32 cooldownSeconds = 0;
    qint32 questionCount = 0;
    qint32 requiredCorrect = 0;
};

struct QuizStartedMsg {
    QString user;
    qint32 count = 0;
    qint32 required = 0;
    qint32 maxSwaps = 0;
};

struct QuizQuestionMsg {
    QString id;
    qint32 index = 0;
    qint32 count = 0;
    QString prompt;
    QString type;
    QStringList choices;
    qint32 swapsLeft = 0;
    qint32 attemptsLeft = 0;
};

struct QuizFeedbackMsg {
    QString id;
    bool correct = false;
    qint32 attemptsLeft = 0;
    bool questionFinished = false;
    // Set only for a finished wrong question with correct answers shown; the daemon then
    // waits for QuizNext before sending anything else.
    bool revealed = false;
    QString correctAnswer;
    QString explanation;
};

struct QuizSwapRefusedMsg {
    QString reason;
};

struct QuizReviewItem {
    QString prompt;
    QStringList choices;
    QString givenAnswer;
    QString correctAnswer;
    QString explanation;
};

struct QuizResultMsg {
    bool passed = false;
    qint32 correct = 0;
    qint32 required = 0;
    qint32 asked = 0;
    QList<QuizReviewItem> review;
    qint32 cooldownSeconds = 0;
};

struct QuizCooldownMsg {
    qint32 seconds = 0;
};

struct QuizErrorMsg {
    QString message;
};

struct QuizLoginRefusedMsg {
    QString reason;
};

struct QuizVersionMsg {
    quint32 version = 0;
};

QDataStream &operator<<(QDataStream &s, const QuizQueryMsg &m);
QDataStream &operator>>(QDataStream &s, QuizQueryMsg &m);
QDataStream &operator<<(QDataStream &s, const QuizAnswerMsg &m);
QDataStream &operator>>(QDataStream &s, QuizAnswerMsg &m);
QDataStream &operator<<(QDataStream &s, const QuizSwapMsg &m);
QDataStream &operator>>(QDataStream &s, QuizSwapMsg &m);
QDataStream &operator<<(QDataStream &s, const QuizStatusMsg &m);
QDataStream &operator>>(QDataStream &s, QuizStatusMsg &m);
QDataStream &operator<<(QDataStream &s, const QuizStartedMsg &m);
QDataStream &operator>>(QDataStream &s, QuizStartedMsg &m);
QDataStream &operator<<(QDataStream &s, const QuizQuestionMsg &m);
QDataStream &operator>>(QDataStream &s, QuizQuestionMsg &m);
QDataStream &operator<<(QDataStream &s, const QuizFeedbackMsg &m);
QDataStream &operator>>(QDataStream &s, QuizFeedbackMsg &m);
QDataStream &operator<<(QDataStream &s, const QuizSwapRefusedMsg &m);
QDataStream &operator>>(QDataStream &s, QuizSwapRefusedMsg &m);
QDataStream &operator<<(QDataStream &s, const QuizReviewItem &m);
QDataStream &operator>>(QDataStream &s, QuizReviewItem &m);
QDataStream &operator<<(QDataStream &s, const QuizResultMsg &m);
QDataStream &operator>>(QDataStream &s, QuizResultMsg &m);
QDataStream &operator<<(QDataStream &s, const QuizCooldownMsg &m);
QDataStream &operator>>(QDataStream &s, QuizCooldownMsg &m);
QDataStream &operator<<(QDataStream &s, const QuizErrorMsg &m);
QDataStream &operator>>(QDataStream &s, QuizErrorMsg &m);
QDataStream &operator<<(QDataStream &s, const QuizLoginRefusedMsg &m);
QDataStream &operator>>(QDataStream &s, QuizLoginRefusedMsg &m);
QDataStream &operator<<(QDataStream &s, const QuizVersionMsg &m);
QDataStream &operator>>(QDataStream &s, QuizVersionMsg &m);

// A message on the wire is its quint32 id followed by the payload, if any.
template<typename Id>
QByteArray encodeMessage(Id id)
{
    QByteArray bytes;
    QDataStream stream(&bytes, QIODevice::WriteOnly);
    stream << static_cast<quint32>(id);
    return bytes;
}

template<typename Id, typename Payload>
QByteArray encodeMessage(Id id, const Payload &payload)
{
    QByteArray bytes;
    QDataStream stream(&bytes, QIODevice::WriteOnly);
    stream << static_cast<quint32>(id) << payload;
    return bytes;
}

}
