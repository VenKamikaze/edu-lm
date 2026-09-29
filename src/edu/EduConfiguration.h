// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include "EduConstants.h"

#include "ConfigReader.h"
#include "core/QuizPolicy.h"
#include "core/QuizRules.h"

namespace EduLM {

    // Points EduConfig at another file for later instances; the daemon never calls this.
    // Without a dropInDir there is no drop-in directory. An empty path restores the
    // installed locations.
    void setConfigFileOverride(const QString &path, const QString &dropInDir = QString());
    QString configFilePath();
    QString configDirPath();

    Config(EduConfig, configFilePath(), configDirPath(), QString(),
        Section(Quiz,
            Entry(Enabled,             bool,        true,                                       _S("Enable the quiz gate"));
            Entry(Groups,              QStringList, QStringList{_S("edu-students")},            _S("Comma-separated groups whose members get the quiz"));
            Entry(Users,               QStringList, QStringList(),                              _S("Comma-separated users who get the quiz"));
            Entry(ExemptUsers,         QStringList, QStringList(),                              _S("Comma-separated users who never get the quiz"));
            Entry(StudentPasswords,    bool,        true,                                       _S("false = quiz users log in with no password"));
            Entry(QuestionCount,       int,         5,                                          _S("Questions asked per attempt (N)"));
            Entry(RequiredCorrect,     int,         4,                                          _S("Correct answers needed to pass (K <= N)"));
            Entry(MaxSwaps,            int,         3,                                          _S("Question swaps shared by the whole attempt"));
            Entry(ShowCorrectAnswers,  bool,        true,                                       _S("Show the right answer after a missed question, and the missed ones after a failed attempt"));
            Entry(FailCooldownSeconds, int,         60,                                         _S("Forced wait after a failed attempt"));
            Entry(EndEarly,            QString,     _S("no"),                                   _S("End the attempt early: no, pass, fail or both"));
            Entry(AnswerAttempts,      int,         1,                                          _S("Tries per question before it counts as wrong"));
        );
        Section(Content,
            Entry(QuestionDirs,        QStringList, QStringList{_S(EDU_QUESTIONS_DIR), _S(EDU_EXTRA_QUESTIONS_DIR)}, _S("Comma-separated directories of question packs"));
            Entry(Subjects,            QStringList, QStringList{_S("maths"), _S("science"), _S("hass"), _S("english")}, _S("Comma-separated subjects to ask about"));
            Entry(YearGroups,          QStringList, QStringList{_S("edu-year3:3"), _S("edu-year4:4"), _S("edu-year5:5")}, _S("Comma-separated group:year entries; a user gets the union of every group they are in"));
            Entry(DefaultYear,         int,         4,                                          _S("Year level for users in no year group"));
        );
    );

    QuizRules quizRules(const EduConfig &config);
    QuizPolicyConfig quizPolicyConfig(const EduConfig &config);
}
