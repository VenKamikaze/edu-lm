// SPDX-License-Identifier: GPL-2.0-or-later
#include "core/QuizSession.h"

#include <QtTest>

using namespace EduLM;
using Refusal = QuizSession::Refusal;
using State = QuizSession::State;

class QuizSessionTest : public QObject
{
    Q_OBJECT

private slots:
    void passesAtBoundary();
    void failsBelowBoundary();
    void alwaysAsksEverything();
    void invalidWhenPoolTooSmall();
    void invalidRules();
    void noRepeatedEntries();
    void sameSeedSameQuiz();
    void spreadsSubjects();
    void generatorEntriesInstantiated();
    void wrongQuestionId();
    void multipleAttempts();
    void swapReplacesQuestion();
    void swapBudgetIsShared();
    void swapRefusedAfterAttempt();
    void swapRefusedWhenPoolExhausted();
    void actionsAfterEnd();
    void reviewContents();
    void reviewHiddenWhenPassedOrDisabled();
    void endEarlyNoChangesNothing();
    void endEarlyPass();
    void endEarlyFail();
    void endEarlyBoth();
    void endEarlyFailReview();
    void endEarlyWaitsForQuestionToFinish();
    void endEarlyParseAndValidate();
};

// Choice 0 is always the right answer.
static Question mc(const QString &id, const QString &subject = "maths")
{
    Question q;
    q.id = id;
    q.subject = subject;
    q.type = QuestionType::MultipleChoice;
    q.prompt = "Prompt " + id;
    q.choices = {"right " + id, "wrong " + id};
    q.answerIndex = 0;
    q.explanation = "Because " + id;
    return q;
}

static QList<Question> pool(int count, const QString &subject = "maths")
{
    QList<Question> p;
    for (int i = 0; i < count; ++i)
        p << mc(QString("%1-%2").arg(subject).arg(i), subject);
    return p;
}

static QuizRules rules(int n, int k, int swaps = 0, int attempts = 1, bool show = true)
{
    QuizRules r;
    r.questionCount = n;
    r.requiredCorrect = k;
    r.maxSwaps = swaps;
    r.answerAttempts = attempts;
    r.showCorrectAnswers = show;
    return r;
}

static QuizRules earlyRules(EndEarly mode, int n = 5, int k = 4, int attempts = 1)
{
    QuizRules r = rules(n, k, 0, attempts);
    r.endEarly = mode;
    return r;
}

static QuizSession::AnswerResult respond(QuizSession &s, bool right)
{
    return s.answer(s.current()->id, right ? "0" : "1");
}

// Answers n questions, the first `right` of them correctly.
static void play(QuizSession &s, int total, int right)
{
    for (int i = 0; i < total; ++i)
        respond(s, i < right);
}

void QuizSessionTest::passesAtBoundary()
{
    QRandomGenerator rng(1);
    QuizSession s(rules(5, 4), pool(10), rng);
    QCOMPARE(s.state(), State::InProgress);
    play(s, 5, 4);
    QCOMPARE(s.state(), State::Passed);
    QCOMPARE(s.correctCount(), 4);
}

void QuizSessionTest::failsBelowBoundary()
{
    QRandomGenerator rng(1);
    QuizSession s(rules(5, 4), pool(10), rng);
    play(s, 5, 3);
    QCOMPARE(s.state(), State::Failed);
    QCOMPARE(s.correctCount(), 3);
}

void QuizSessionTest::alwaysAsksEverything()
{
    QRandomGenerator rng(1);
    QuizSession pass(rules(5, 2), pool(10), rng);
    play(pass, 2, 2);
    QCOMPARE(pass.state(), State::InProgress);
    QCOMPARE(pass.currentIndex(), 2);

    // K is unreachable after two misses, but the quiz still continues
    QuizSession fail(rules(5, 4), pool(10), rng);
    play(fail, 2, 0);
    QCOMPARE(fail.state(), State::InProgress);
    QCOMPARE(fail.currentIndex(), 2);
    play(fail, 3, 3);
    QCOMPARE(fail.state(), State::Failed);
}

void QuizSessionTest::invalidWhenPoolTooSmall()
{
    QRandomGenerator rng(1);
    QuizSession s(rules(5, 4), pool(4), rng);
    QCOMPARE(s.state(), State::Invalid);
    QVERIFY(s.errors().join(' ').contains("4 entries but 5"));
    QVERIFY(!s.current());
    QVERIFY(!s.answer("maths-0", "0").accepted());

    QuizSession ok(rules(5, 4), pool(5), rng);
    QCOMPARE(ok.state(), State::InProgress);
}

void QuizSessionTest::invalidRules()
{
    QRandomGenerator rng(1);
    QuizSession s(rules(3, 4), pool(10), rng);
    QCOMPARE(s.state(), State::Invalid);
    QVERIFY(s.errors().join(' ').contains("greater than"));
    QVERIFY(!rules(0, 0).validate().isEmpty());
    QVERIFY(!rules(3, 2, -1).validate().isEmpty());
    QVERIFY(!rules(3, 2, 0, 0).validate().isEmpty());
    QVERIFY(rules(5, 4, 3, 1).validate().isEmpty());
}

void QuizSessionTest::noRepeatedEntries()
{
    // With swaps in play, no entry may appear twice in one attempt
    for (int seed = 0; seed < 50; ++seed) {
        QRandomGenerator rng(seed);
        QuizSession s(rules(5, 4, 5), pool(10, "a") + pool(10, "b"), rng);
        QSet<QString> seen;
        for (int i = 0; i < 5; ++i) {
            // swapped-out entries count as used too
            if (i % 2 == 0) {
                seen.insert(s.current()->id);
                QVERIFY(s.swap(s.current()->id).accepted());
            }
            QVERIFY2(!seen.contains(s.current()->id), qPrintable(s.current()->id));
            seen.insert(s.current()->id);
            respond(s, true);
        }
        QCOMPARE(s.state(), State::Passed);
    }
}

void QuizSessionTest::sameSeedSameQuiz()
{
    const auto ids = [](quint32 seed) {
        QRandomGenerator rng(seed);
        QuizSession s(rules(5, 4), pool(10, "a") + pool(10, "b"), rng);
        QStringList out;
        while (s.state() == State::InProgress) {
            out << s.current()->id;
            respond(s, true);
        }
        return out;
    };
    QCOMPARE(ids(5), ids(5));
    QVERIFY(ids(5) != ids(6));
}

void QuizSessionTest::spreadsSubjects()
{
    // A tiny subject must still show up far more often than its share of the pool
    const QList<Question> skewed = pool(50, "big") + pool(2, "small");
    int withSmall = 0;
    for (int seed = 0; seed < 200; ++seed) {
        QRandomGenerator rng(seed);
        QuizSession s(rules(3, 2), skewed, rng);
        while (s.state() == State::InProgress) {
            if (s.current()->subject == "small") {
                ++withSmall;
                break;
            }
            respond(s, true);
        }
    }
    QVERIFY2(withSmall > 120, qPrintable(QString::number(withSmall)));
}

void QuizSessionTest::generatorEntriesInstantiated()
{
    Question gen;
    gen.id = "gen-add";
    gen.subject = "maths";
    gen.type = QuestionType::Generator;
    gen.generator = "add";
    gen.params = {{"a", {1, 9}}, {"b", {1, 9}}};

    QRandomGenerator rng(2);
    QuizSession s(rules(1, 1), {gen}, rng);
    QVERIFY(s.current());
    QCOMPARE(s.current()->type, QuestionType::Numeric);
    QVERIFY(s.current()->id.startsWith("gen-add#"));
    QVERIFY(!s.current()->prompt.isEmpty());
    const QString answer = QString::number(s.current()->answerNumber);
    QVERIFY(s.answer(s.current()->id, answer).correct);
    QCOMPARE(s.state(), State::Passed);

    // Generator entries are used once per attempt, like any other entry
    QRandomGenerator rng2(2);
    QuizSession two(rules(2, 1, 1), {gen, mc("x")}, rng2);
    QCOMPARE(two.swap(two.current()->id).refusal, Refusal::NoUnusedEntry);
}

void QuizSessionTest::wrongQuestionId()
{
    QRandomGenerator rng(1);
    QuizSession s(rules(3, 2, 2, 2), pool(10), rng);
    const QString id = s.current()->id;
    const auto a = s.answer("nope", "0");
    QCOMPARE(a.refusal, Refusal::WrongQuestion);
    QVERIFY(!a.accepted());
    QCOMPARE(s.swap("nope").refusal, Refusal::WrongQuestion);
    QCOMPARE(s.swapsLeft(), 2);
    QCOMPARE(s.attemptsLeft(), 2);
    QCOMPARE(s.currentIndex(), 0);
    QCOMPARE(s.current()->id, id);
}

void QuizSessionTest::multipleAttempts()
{
    QRandomGenerator rng(1);
    QuizSession s(rules(3, 2, 0, 3), pool(10), rng);
    const QString first = s.current()->id;

    auto r = respond(s, false);
    QVERIFY(r.accepted() && !r.correct && !r.questionFinished);
    QCOMPARE(r.attemptsLeft, 2);
    QCOMPARE(s.current()->id, first);
    r = respond(s, false);
    QCOMPARE(r.attemptsLeft, 1);
    QVERIFY(!r.questionFinished);

    // right on the last try still counts
    r = respond(s, true);
    QVERIFY(r.correct && r.questionFinished);
    QCOMPARE(s.correctCount(), 1);
    QCOMPARE(s.currentIndex(), 1);
    QCOMPARE(s.attemptsLeft(), 3);

    // three misses finish the question as wrong and move on
    respond(s, false);
    respond(s, false);
    r = respond(s, false);
    QVERIFY(!r.correct && r.questionFinished);
    QCOMPARE(r.attemptsLeft, 0);
    QCOMPARE(s.correctCount(), 1);
    QCOMPARE(s.currentIndex(), 2);
}

void QuizSessionTest::swapReplacesQuestion()
{
    QRandomGenerator rng(1);
    QuizSession s(rules(3, 2, 1), pool(10), rng);
    const QString before = s.current()->id;
    const auto r = s.swap(before);
    QVERIFY(r.accepted());
    QCOMPARE(s.swapsLeft(), 0);
    QVERIFY(s.current()->id != before);
    QCOMPARE(s.currentIndex(), 0);
    // the old id is no longer current
    QCOMPARE(s.answer(before, "0").refusal, Refusal::WrongQuestion);
    QVERIFY(respond(s, true).correct);
}

void QuizSessionTest::swapBudgetIsShared()
{
    QRandomGenerator rng(1);
    QuizSession s(rules(4, 3, 2), pool(20), rng);
    QVERIFY(s.swap(s.current()->id).accepted());
    QCOMPARE(s.swapsLeft(), 1);
    respond(s, true);
    QVERIFY(s.swap(s.current()->id).accepted());
    QCOMPARE(s.swapsLeft(), 0);
    respond(s, true);

    const QString id = s.current()->id;
    QCOMPARE(s.swap(id).refusal, Refusal::NoSwapsLeft);
    QCOMPARE(s.swapsLeft(), 0);
    QCOMPARE(s.current()->id, id);
}

void QuizSessionTest::swapRefusedAfterAttempt()
{
    QRandomGenerator rng(1);
    QuizSession s(rules(3, 2, 3, 2), pool(20), rng);
    const QString id = s.current()->id;
    respond(s, false);
    QCOMPARE(s.swap(id).refusal, Refusal::AlreadyAttempted);
    QCOMPARE(s.swapsLeft(), 3);
    QCOMPARE(s.current()->id, id);
    QCOMPARE(s.attemptsLeft(), 1);

    // the next question is fresh, so swapping works again
    respond(s, true);
    QVERIFY(s.swap(s.current()->id).accepted());
    QCOMPARE(s.swapsLeft(), 2);
}

void QuizSessionTest::swapRefusedWhenPoolExhausted()
{
    QRandomGenerator rng(1);
    QuizSession s(rules(3, 2, 3), pool(3), rng);
    const QString id = s.current()->id;
    QCOMPARE(s.swap(id).refusal, Refusal::NoUnusedEntry);
    QCOMPARE(s.swapsLeft(), 3);
    QCOMPARE(s.current()->id, id);

    // one spare entry allows exactly one swap
    QuizSession spare(rules(3, 2, 3), pool(4), rng);
    QVERIFY(spare.swap(spare.current()->id).accepted());
    QCOMPARE(spare.swapsLeft(), 2);
    QCOMPARE(spare.swap(spare.current()->id).refusal, Refusal::NoUnusedEntry);
    QCOMPARE(spare.swapsLeft(), 2);
}

void QuizSessionTest::actionsAfterEnd()
{
    QRandomGenerator rng(1);
    QuizSession s(rules(2, 1, 2), pool(10), rng);
    const QString last = s.current()->id;
    play(s, 2, 2);
    QCOMPARE(s.state(), State::Passed);
    QVERIFY(!s.current());
    QCOMPARE(s.answer(last, "0").refusal, Refusal::NotInProgress);
    QCOMPARE(s.swap(last).refusal, Refusal::NotInProgress);
    QCOMPARE(s.correctCount(), 2);
    QCOMPARE(s.swapsLeft(), 2);
    QCOMPARE(s.state(), State::Passed);

    QuizSession f(rules(2, 2), pool(10), rng);
    play(f, 2, 0);
    QCOMPARE(f.state(), State::Failed);
    QCOMPARE(f.answer("x", "0").refusal, Refusal::NotInProgress);
}

void QuizSessionTest::reviewContents()
{
    QRandomGenerator rng(1);
    QuizSession s(rules(3, 3, 0, 2), pool(10), rng);
    // Q1 right, Q2 wrong twice, Q3 answered with garbage twice
    QStringList ids;
    ids << s.current()->id;
    respond(s, true);
    ids << s.current()->id;
    respond(s, false);
    respond(s, false);
    ids << s.current()->id;
    s.answer(ids[2], "banana");
    s.answer(ids[2], "  seven ");
    QCOMPARE(s.state(), State::Failed);

    const QList<ReviewItem> review = s.review();
    QCOMPARE(review.size(), 2);
    QCOMPARE(review[0].prompt, "Prompt " + ids[1]);
    QCOMPARE(review[0].choices, (QStringList{"right " + ids[1], "wrong " + ids[1]}));
    QCOMPARE(review[0].yourAnswer, "wrong " + ids[1]);
    QCOMPARE(review[0].correctAnswer, "right " + ids[1]);
    QCOMPARE(review[0].explanation, "Because " + ids[1]);
    QCOMPARE(review[1].prompt, "Prompt " + ids[2]);
    QCOMPARE(review[1].yourAnswer, QString("seven"));
    QCOMPARE(review[1].correctAnswer, "right " + ids[2]);
}

void QuizSessionTest::reviewHiddenWhenPassedOrDisabled()
{
    QRandomGenerator rng(1);
    QuizSession passed(rules(3, 2), pool(10), rng);
    play(passed, 3, 2);
    QCOMPARE(passed.state(), State::Passed);
    QVERIFY(passed.review().isEmpty());

    QuizSession hidden(rules(3, 3, 0, 1, false), pool(10), rng);
    play(hidden, 3, 0);
    QCOMPARE(hidden.state(), State::Failed);
    QVERIFY(hidden.review().isEmpty());

    QuizSession running(rules(3, 3), pool(10), rng);
    respond(running, false);
    QVERIFY(running.review().isEmpty());
}

void QuizSessionTest::endEarlyNoChangesNothing()
{
    QRandomGenerator rng(1);
    QuizSession s(earlyRules(EndEarly::No), pool(10), rng);
    play(s, 4, 4);
    QCOMPARE(s.state(), State::InProgress);
    QuizSession f(earlyRules(EndEarly::No), pool(10), rng);
    play(f, 2, 0);
    QCOMPARE(f.state(), State::InProgress);
}

void QuizSessionTest::endEarlyPass()
{
    QRandomGenerator rng(1);
    QuizSession s(earlyRules(EndEarly::Pass), pool(10), rng);
    play(s, 3, 3);
    QCOMPARE(s.state(), State::InProgress);
    respond(s, true);
    QCOMPARE(s.state(), State::Passed);
    QCOMPARE(s.correctCount(), 4);
    QVERIFY(!s.current());
    QCOMPARE(s.answer("x", "0").refusal, Refusal::NotInProgress);

    // misses along the way do not stop it, and passing is still possible on the last question
    QuizSession late(earlyRules(EndEarly::Pass), pool(10), rng);
    respond(late, false);
    play(late, 3, 3);
    QCOMPARE(late.state(), State::InProgress);
    respond(late, true);
    QCOMPARE(late.state(), State::Passed);

    // pass mode does not fail early
    QuizSession f(earlyRules(EndEarly::Pass), pool(10), rng);
    play(f, 2, 0);
    QCOMPARE(f.state(), State::InProgress);
}

void QuizSessionTest::endEarlyFail()
{
    QRandomGenerator rng(1);
    // N=5, K=4: one miss leaves a pass possible, the second does not
    QuizSession s(earlyRules(EndEarly::Fail), pool(10), rng);
    respond(s, false);
    QCOMPARE(s.state(), State::InProgress);
    respond(s, false);
    QCOMPARE(s.state(), State::Failed);
    QCOMPARE(s.currentIndex(), 2);
    QVERIFY(!s.current());

    // fail mode does not pass early
    QuizSession p(earlyRules(EndEarly::Fail), pool(10), rng);
    play(p, 4, 4);
    QCOMPARE(p.state(), State::InProgress);
    respond(p, true);
    QCOMPARE(p.state(), State::Passed);

    // K == N: the first miss is already fatal
    QuizSession all(earlyRules(EndEarly::Fail, 3, 3), pool(10), rng);
    respond(all, false);
    QCOMPARE(all.state(), State::Failed);
}

void QuizSessionTest::endEarlyBoth()
{
    QRandomGenerator rng(1);
    QuizSession p(earlyRules(EndEarly::Both), pool(10), rng);
    play(p, 4, 4);
    QCOMPARE(p.state(), State::Passed);

    QuizSession f(earlyRules(EndEarly::Both), pool(10), rng);
    play(f, 2, 0);
    QCOMPARE(f.state(), State::Failed);

    // one miss then correct answers: neither rule fires until the pass at 4 correct
    QuizSession m(earlyRules(EndEarly::Both), pool(10), rng);
    respond(m, false);
    play(m, 3, 3);
    QCOMPARE(m.state(), State::InProgress);
    respond(m, true);
    QCOMPARE(m.state(), State::Passed);
}

void QuizSessionTest::endEarlyFailReview()
{
    QRandomGenerator rng(1);
    QuizSession s(earlyRules(EndEarly::Fail), pool(10), rng);
    const QString first = s.current()->id;
    respond(s, true);
    const QString second = s.current()->id;
    respond(s, false);
    const QString third = s.current()->id;
    respond(s, false);
    QCOMPARE(s.state(), State::Failed);

    // only the two misses actually asked; the unreached questions are not listed
    const QList<ReviewItem> review = s.review();
    QCOMPARE(review.size(), 2);
    QCOMPARE(review[0].prompt, "Prompt " + second);
    QCOMPARE(review[1].prompt, "Prompt " + third);
    QVERIFY(review[0].prompt != "Prompt " + first);
}

void QuizSessionTest::endEarlyWaitsForQuestionToFinish()
{
    QRandomGenerator rng(1);
    // N=3, K=3, 3 tries each: the first wrong try makes a pass impossible only once the question is lost
    QuizSession f(earlyRules(EndEarly::Fail, 3, 3, 3), pool(10), rng);
    QVERIFY(!respond(f, false).questionFinished);
    QVERIFY(!respond(f, false).questionFinished);
    QCOMPARE(f.state(), State::InProgress);
    QVERIFY(respond(f, false).questionFinished);
    QCOMPARE(f.state(), State::Failed);

    // a correct last try rescues the question, so nothing ends
    QuizSession r(earlyRules(EndEarly::Fail, 3, 3, 3), pool(10), rng);
    respond(r, false);
    respond(r, false);
    respond(r, true);
    QCOMPARE(r.state(), State::InProgress);
    QCOMPARE(r.currentIndex(), 1);

    // early pass fires on the finishing correct answer, not before
    QuizSession p(earlyRules(EndEarly::Pass, 3, 1, 2), pool(10), rng);
    respond(p, false);
    QCOMPARE(p.state(), State::InProgress);
    respond(p, true);
    QCOMPARE(p.state(), State::Passed);
}

void QuizSessionTest::endEarlyParseAndValidate()
{
    QCOMPARE(parseEndEarly("no"), EndEarly::No);
    QCOMPARE(parseEndEarly(" Pass "), EndEarly::Pass);
    QCOMPARE(parseEndEarly("FAIL"), EndEarly::Fail);
    QCOMPARE(parseEndEarly("both"), EndEarly::Both);
    QCOMPARE(parseEndEarly(""), EndEarly::Invalid);
    QCOMPARE(parseEndEarly("yes"), EndEarly::Invalid);

    QuizRules bad = rules(5, 4);
    bad.endEarly = EndEarly::Invalid;
    QVERIFY(bad.validate().join(' ').contains("EndEarly"));
    QRandomGenerator rng(1);
    QuizSession s(bad, pool(10), rng);
    QCOMPARE(s.state(), State::Invalid);
}

QTEST_APPLESS_MAIN(QuizSessionTest)
#include "QuizSessionTest.moc"
