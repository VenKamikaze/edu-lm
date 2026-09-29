// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "Question.h"

#include <QSet>

namespace EduLM {

struct LoadIssue {
    QString file;
    QString question; // id, or "#<index>" when the id is missing; empty for file-level issues
    QString message;

    QString toString() const;
};

class QuestionBank
{
public:
    // Each path is a directory (all *.json inside) or a single file.
    // Bad files and questions are skipped and reported; loading never aborts.
    void load(const QStringList &paths);
    void loadFile(const QString &file);

    const QList<Question> &questions() const { return m_questions; }
    const QList<LoadIssue> &errors() const { return m_errors; }
    const QList<LoadIssue> &warnings() const { return m_warnings; }

    // A question qualifies if any of its years is in `years`.
    QList<Question> filter(const QList<int> &years, const QStringList &subjects) const;

    // subject -> year -> number of questions
    QMap<QString, QMap<int, int>> counts() const;

private:
    void loadDirectory(const QString &dir);
    void error(const QString &file, const QString &question, const QString &message);
    void warning(const QString &file, const QString &question, const QString &message);

    QList<Question> m_questions;
    QSet<QString> m_ids;
    QList<LoadIssue> m_errors;
    QList<LoadIssue> m_warnings;
};

}
