// SPDX-License-Identifier: GPL-2.0-or-later
#include "EduConfiguration.h"

namespace EduLM {

QuizRules quizRules(const EduConfig &config)
{
    QuizRules rules;
    rules.questionCount = config.Quiz.QuestionCount.get();
    rules.requiredCorrect = config.Quiz.RequiredCorrect.get();
    rules.maxSwaps = config.Quiz.MaxSwaps.get();
    rules.answerAttempts = config.Quiz.AnswerAttempts.get();
    rules.showCorrectAnswers = config.Quiz.ShowCorrectAnswers.get();
    return rules;
}

QuizPolicyConfig quizPolicyConfig(const EduConfig &config)
{
    QuizPolicyConfig policy;
    policy.enabled = config.Quiz.Enabled.get();
    policy.groups = config.Quiz.Groups.get();
    policy.users = config.Quiz.Users.get();
    policy.exemptUsers = config.Quiz.ExemptUsers.get();
    policy.yearGroups = config.Content.YearGroups.get();
    policy.defaultYear = config.Content.DefaultYear.get();
    return policy;
}

}
