# Adding your own questions

A guide for parents and admins. You need a terminal and `sudo`, and you should have Edu-LM installed ([INSTALL-ARCH.md](INSTALL-ARCH.md)). The design behind the format is in [PLAN.md](PLAN.md) section 6.

Questions live in JSON files called packs. Edu-LM ships packs for Years 3 to 5, and you can add your own next to them: family trivia, spelling lists, a harder set for a child working above their year, or a whole new subject.

## Where packs live

| Directory | What | Edit it? |
|---|---|---|
| `/usr/share/edu-lm/questions/` | The shipped packs. | No. A package upgrade replaces it. |
| `/etc/edu-lm/questions.d/` | Your packs. | Yes. |
| `/usr/share/edu-lm/examples/example-pack.json` | A working example with one question of each type. | Copy it, never use it in place. |

The directories that are loaded come from `QuestionDirs=` in the `[Content]` section. The default is the two directories above, in that order. To load more, set the whole list in a drop-in (it replaces the default, so list all of them):

```sh
sudo tee /etc/edu-lm/edu-lm.conf.d/10-dirs.conf <<'EOT'
[Content]
QuestionDirs=/usr/share/edu-lm/questions,/etc/edu-lm/questions.d,/srv/more-questions
EOT
```

Every non-hidden file in `edu-lm.conf.d` is read, whatever its name, so a backup such as `10-dirs.conf.bak` or `10-dirs.conf~` still counts. Keep backups elsewhere.

Never edit `/etc/edu-lm/edu-lm.conf`. It is comments only, and a key set there silently beats every drop-in (see [INSTALL-ARCH.md](INSTALL-ARCH.md) section 3).

Every directory is read for `*.json` files (other files are ignored), in the order listed, and the files in a directory in name order.

**No restart is needed.** The daemon reads all the packs again each time a child starts a quiz (`startQuiz` in `src/edu/daemon/QuizGate.cpp`), so a change applies from the next attempt. A pack file with an error is skipped or partly skipped, and the daemon logs each error as "Edu-LM: question pack problem: ..." (see `journalctl -u sddm`). It does not log warnings.

## Permissions

Packs hold the answer keys. The greeter (a separate process, run as the `sddm` user) must never be able to read them. Keep them root-owned, files `0600` and directories `0700`. `edu-lm-check` fails on anything owned by someone else or readable by group or others.

```sh
sudo install -d -m 0700 -o root -g root /etc/edu-lm/questions.d      # already there after a package install
sudo install -m 0600 -o root -g root family.json /etc/edu-lm/questions.d/family.json
```

If you edit a file in place, put the mode back afterwards (new files from an editor are usually `0644`):

```sh
sudo find /etc/edu-lm/questions.d -name '*.json' -exec chown root:root {} + -exec chmod 0600 {} +
```

The example in `/usr/share/edu-lm/examples/` is world-readable on purpose, since it is only an example. Make any copy you put in `questions.d` `0600` (the `install -m 0600` above does).

A good habit: write and test the file in your home directory, then install it.

## The format

One JSON object per file:

```json
{
  "schema": 1,
  "pack": "family",
  "subject": "maths",
  "questions": [ ... ]
}
```

### Pack fields

| Field | Required | Meaning |
|---|---|---|
| `schema` | yes | Must be `1`. Any other value (or none) rejects the whole file. |
| `questions` | yes | The array of questions. Missing or not an array rejects the whole file. |
| `pack` | no | A name for the pack. Missing gives a warning. |
| `subject` | yes, unless every question has its own | The default subject for the questions. Lower-cased when read. |

Other pack fields give a warning ("unknown pack field ... ignored") and are ignored. JSON has no comments, so use `explanation` or a note in your own files elsewhere.

### Question fields

| Field | Required | Meaning |
|---|---|---|
| `id` | yes | Unique text, for example `fam-001`. Unique across **all** loaded packs (see Gotchas). |
| `years` | yes | A non-empty array of whole numbers from 1 to 12, for example `[4]` or `[3, 4]`. A child is asked questions whose `years` include their year (see Gotchas for years other than 3 to 5). |
| `type` | yes | `multiple_choice`, `true_false`, `numeric`, `short_text` or `generator`. |
| `subject` | no | Overrides the pack's subject for this question. |
| `prompt` | yes, except for `generator` | The question text. Use `\n` in the JSON string for a line break. |
| `explanation` | no | Shown with the right answer after a missed question (and in the review after a failed attempt), if `ShowCorrectAnswers=true` (the default). |
| `curriculum` | no | A curriculum code such as `AC9M4N01`. Kept for your own records; the quiz does not use it. |
| `difficulty` | no | A whole number. Kept for your own records; the quiz does not use it. |

The type-specific fields:

| Type | Fields | Rules |
|---|---|---|
| `multiple_choice` | `choices`, `answer` | `choices`: at least 2 strings, none empty, none repeated. `answer`: the **position** of the right choice, counting from 0. |
| `true_false` | `answer` | `true` or `false` (JSON booleans, no quotes). |
| `numeric` | `answer`, `tolerance` | `answer`: a number. `tolerance`: optional, a number of 0 or more (default 0). |
| `short_text` | `accept` | A non-empty array of accepted answers, none blank. The first is the one shown as the right answer. |
| `generator` | `generator`, `params` | See Generators below. |

Choices are shown in the order you write them and are not shuffled, so vary where the right one sits.

Any other field gives a warning ("unknown field ... ignored") and is ignored. A question with an error is skipped and the rest of the file still loads. The messages are listed below.

### How answers are checked

These rules come from `src/edu/core/Answer.cpp`.

- **Multiple choice:** the child picks a button; the answer is its position. Nothing to type.
- **True or false:** the child picks True or False.
- **Numeric:** the child types a number. Leading and trailing spaces are ignored. A decimal point must be `.` (`2.5`; `2,5` is wrong). A minus sign (`-` or the real minus `−`) is fine. Thousands may be grouped with one consistent separator, either a space or a comma, in groups of three (`12 345` or `12,345`). Anything else is wrong, including units (`5 cm`), fractions (`3/4`), words (`five`) and a leading `+`. The answer is right if it is within `tolerance` of your `answer` (the difference is compared as an absolute value; there is a tiny allowance of 0.000000001 for rounding).
- **Short text:** the child's answer and each accepted answer are cleaned up the same way, then compared. Cleaning removes leading and trailing spaces, squeezes runs of spaces into one, removes punctuation at the start and end, and ignores upper and lower case. So `"  Children! "` matches `children`. Punctuation inside is kept: `dont` does not match `don't`, so list both if you want both. An empty answer is always wrong.
- **Generator:** the daemon makes a numeric question on the spot (see below). Exact integer answers, the same typing rules as numeric.

## One example per type

This is `/usr/share/edu-lm/examples/example-pack.json` (a test in the source tree keeps it loadable with no errors or warnings):

```json
{
  "schema": 1,
  "pack": "example",
  "subject": "maths",
  "questions": [
    {
      "id": "example-001",
      "years": [4],
      "type": "multiple_choice",
      "prompt": "Which shape has 4 equal sides and 4 square corners?",
      "choices": ["Square", "Triangle", "Circle", "Rectangle"],
      "answer": 0,
      "difficulty": 1,
      "explanation": "A square has 4 equal sides and 4 square corners. A rectangle has square corners, but its sides are not all equal."
    },
    {
      "id": "example-002",
      "years": [3, 4],
      "type": "true_false",
      "prompt": "An even number can be shared into 2 equal groups with none left over.",
      "answer": true,
      "explanation": "Even numbers like 8 make 2 groups of 4."
    },
    {
      "id": "example-003",
      "years": [4],
      "type": "numeric",
      "prompt": "A bike ride took 1 hour and 55 minutes. About how many hours is that? Type a number.",
      "answer": 2,
      "tolerance": 0.25,
      "explanation": "55 minutes is almost a whole hour, so the ride took about 2 hours."
    },
    {
      "id": "example-004",
      "years": [3, 4, 5],
      "subject": "english",
      "type": "short_text",
      "prompt": "What is the plural of the word 'child'?",
      "accept": ["children"],
      "explanation": "The plural of child is children."
    },
    {
      "id": "example-005",
      "years": [4],
      "type": "generator",
      "generator": "multiply",
      "params": { "a": [2, 9], "b": [2, 9] }
    }
  ]
}
```

Notes: `example-004` sets its own `subject`, which overrides the pack's `maths`. `example-003` has a `tolerance`, so any answer from 1.75 to 2.25 is right (an estimate is fair here).

### Generators

A generator entry is not one question but a recipe: each time it is drawn, the daemon makes a fresh numeric question (for example `What is 7 × 6?`). Use them for practice sums. The question text is fixed and written by the generator, so `prompt`, `answer` and `explanation` on a generator entry have no effect. `years` is still required.

`params` is optional. Each param is a range `[min, max]` of whole numbers, both ends included. Leave a param out and it takes its default. An unknown param name is an error. All numbers must be 0 to 100000 (a divisor 1 to 100000, and `digits` 2 to 7), and `min` must not exceed `max`.

| `generator` | Params (default) | Asks | Notes |
|---|---|---|---|
| `add` | `a` [1, 20], `b` [1, 20] | `What is a + b?` | |
| `subtract` | `a` [1, 20], `b` [1, 20] | `What is a − b?` | The larger of the two numbers drawn is put first, so the answer is never negative. |
| `multiply` | `a` [2, 10], `b` [2, 10] | `What is a × b?` | |
| `divide` | `quotient` [2, 10], `divisor` [2, 10] | `What is x ÷ y?` | The sum is made from the quotient and divisor, so the answer is always a whole number. |
| `place_value` | `digits` [3, 4] | `What is the value of the 4 in 4527?` | `digits` is how many digits the number has. The asked digit appears only once in the number and is not 0. The answer is the digit times its place, e.g. 4000. |

Numbers of 5 digits or more are shown in groups of three with a space (`14 527`). The child can type them with or without it.

An example, subtraction within 1000:

```json
{ "id": "fam-gen-001", "years": [4], "type": "generator", "generator": "subtract", "params": { "a": [100, 999], "b": [10, 99] } }
```

There is no fractions generator; write fractions questions by hand.

## Checking your files

```sh
edu-lm-validate ~/family.json            # one file
edu-lm-validate ~/packs/                 # every *.json in a directory
edu-lm-validate --sample 3 ~/family.json # also print 3 sample questions per generator entry (--sample N must come first)
```

It prints warnings and errors to the screen (standard error) then a summary, and exits with status 1 if there were any errors. It needs no root unless the files do.

Here is the real output for a pack with some typical mistakes (`family.json` also holds `example-001`, whose id is already used by the example pack in the same directory):

```
warning: broken/family.json: unknown pack field 'difficulty' ignored
warning: broken/family.json: question fam-002: unknown field 'hint' ignored
error: broken/family.json: question fam-001: answer must be an index 0-3 (question skipped)
error: broken/family.json: question example-001: duplicate id (question skipped)
error: broken/family.json: question fam-004: duplicate choice '3' (question skipped)
error: broken/family.json: question fam-005: unknown type 'multiply' (question skipped)

6 questions loaded
english: year 3=1 year 4=1 year 5=1
maths: year 3=1 year 4=5
4 errors, 2 warnings
```

Here `fam-001` has four choices but `"answer": 4` (counting starts at 0, so the last choice is 3). Whole-file problems look like `invalid JSON: ... (offset N)` (a missing comma or quote near that byte) and `unsupported or missing schema (expected 1)`.

To see your pack the way the daemon will, with the shipped packs and so a clash is caught, validate both together (the shipped directory is root-only, so this needs sudo):

```sh
sudo edu-lm-validate /usr/share/edu-lm/questions /etc/edu-lm/questions.d
```

When the first-listed pack has an id, a later pack with the same id loses.

Then check the whole machine:

```sh
sudo edu-lm-check
```

It prints one line per check (`OK`, `INFO`, `WARN`, `FAIL`). The ones about questions: how many questions loaded; per quiz user's year, how many questions they can be asked (it warns if there are fewer than `QuestionCount` plus `MaxSwaps`, and fails if fewer than `QuestionCount`); and the permissions. Real lines (for a copy of a pack left at mode 644, run against a test copy of the system files so the owner lines differ):

```
OK    5 questions loaded
FAIL  permissions: .../etc/edu-lm/questions.d/family.json has mode 644 (group and others must have no access)
```

Without root it cannot read the `0700` directories and says so.

## Gotchas

- **Ids must be unique across every loaded pack**, shipped ones included. A clashing question is skipped with `duplicate id`. Shipped packs load first, so you cannot replace a shipped question; give yours a new id. A prefix per pack (`fam-001`) avoids trouble. The shipped ids start with `m3-`, `s4-`, `h5-`, `e3-`, `sm-` and so on, and the example's `example-`.
- **Backups in a pack directory are loaded.** A copy such as `family-old.json` in `questions.d` is read too. It sorts first, so it wins every id, and the questions you edited in `family.json` are skipped as duplicates. Keep backups outside the pack directories (a name that does not end in `.json` also works).
- **A subject that is not in `Subjects=` is ignored.** The default is `maths,science,hass,english`. A pack with `"subject": "spelling"` loads without a complaint and is never asked, until you add it (below). This is silent on purpose, so use `edu-lm-validate` to see the counts per subject and the walk-through (b) to enable it.
- **Subjects have equal weight.** Each question slot picks a subject first, then a question in it. A subject with 3 questions is asked as often as one with 150, and each question is asked at most once per attempt. Keep small packs in a small number of subjects, and make sure a subject has at least a few questions per swap.
- **Years other than 3 to 5 need a group.** See walk-through (c).
- **`Subjects=` and `YearGroups=` replace the whole default list**, so repeat the entries you want to keep.
- **Unknown fields are warnings, not errors.** They are ignored, so a typo in an optional field (`explaination`, `tolerence`) means the field is silently not used. A typo in a required field is an error.
- **A bad question or file is skipped, the rest still load.** A file with invalid JSON is skipped entirely. A single bad question is skipped alone.
- **Too few questions.** If a child's pool (their years and the enabled subjects) is smaller than `QuestionCount`, the quiz cannot start and they cannot log in. Run `sudo edu-lm-check` after changes.
- **The answer is the position, counting from 0**, for multiple choice. Easy to get off by one.

## Walk-throughs

### (a) A family pack for an existing subject

Questions about your own family, filed under `hass` (one of the shipped subjects, so no config is needed).

1. Write `~/family.json`:

```json
{
  "schema": 1,
  "pack": "family",
  "subject": "hass",
  "questions": [
    {
      "id": "fam-001",
      "years": [3, 4, 5],
      "type": "short_text",
      "prompt": "What is the name of our dog?",
      "accept": ["Biscuit"]
    },
    {
      "id": "fam-002",
      "years": [4],
      "type": "multiple_choice",
      "prompt": "Which street do we live on?",
      "choices": ["Wattle Street", "Banksia Road", "Gum Tree Lane"],
      "answer": 1,
      "explanation": "We live on Banksia Road."
    },
    {
      "id": "fam-003",
      "years": [4, 5],
      "type": "numeric",
      "prompt": "How many people live in our house? Type a number.",
      "answer": 4
    }
  ]
}
```

2. Check it: `edu-lm-validate ~/family.json` should end with `0 errors, 0 warnings`.
3. Install it, root-only: `sudo install -m 0600 -o root -g root ~/family.json /etc/edu-lm/questions.d/family.json`.
4. Check the whole system: `sudo edu-lm-check`. The next quiz picks the questions up, with no restart.

With the shipped bank, only a few of a child's questions will be family ones, since they are mixed in with all the others of the subject.

### (b) A new subject

Spelling, for Year 4. Questions as in (a) with `"subject": "spelling"`, for example:

```json
{
  "schema": 1,
  "pack": "spelling-y4",
  "subject": "spelling",
  "questions": [
    {
      "id": "spell-001",
      "years": [4],
      "type": "multiple_choice",
      "prompt": "Which word is spelled correctly?",
      "choices": ["freind", "friend", "frend"],
      "answer": 1,
      "explanation": "Friend: the i comes before the e."
    },
    {
      "id": "spell-002",
      "years": [4],
      "type": "short_text",
      "prompt": "Type the whole word: nec_ss_ry",
      "accept": ["necessary"],
      "explanation": "The missing letters are e and a: necessary."
    },
    {
      "id": "spell-003",
      "years": [4],
      "type": "true_false",
      "prompt": "The word 'seperate' is spelled correctly.",
      "answer": false,
      "explanation": "The correct spelling is separate."
    }
  ]
}
```

Install it as in (a) (`/etc/edu-lm/questions.d/spelling-y4.json`). Then add the subject to `Subjects=` in a drop-in. The list replaces the default, so keep the four shipped subjects:

```sh
sudo tee /etc/edu-lm/edu-lm.conf.d/20-subjects.conf <<'EOT'
[Content]
Subjects=maths,science,hass,english,spelling
EOT
sudo edu-lm-check
```

With an equal chance per subject, about one question in five now comes from spelling, so a child will see most of these three over a few attempts. Add more questions than the 3 here, or the same few repeat.

### (c) A Year 6 child

The shipped groups cover Years 3 to 5 (`edu-year3`, `edu-year4`, `edu-year5`). For Year 6 you need three things: Year 6 questions, a group for the year, and a `YearGroups=` entry.

1. Questions with `"years": [6]` in a pack (the shipped bank has none). A pack like this one is the format, but you need at least `QuestionCount` of them (and `QuestionCount` plus `MaxSwaps` for full swaps), or the child also joins `edu-year5` (below) so they get Year 5 questions too:

```json
{
  "schema": 1,
  "pack": "maths-y6",
  "subject": "maths",
  "questions": [
    {
      "id": "y6-001",
      "years": [6],
      "type": "multiple_choice",
      "prompt": "Which of these is a prime number?",
      "choices": ["21", "27", "29", "33"],
      "answer": 2,
      "explanation": "29 can only be divided by 1 and itself."
    },
    {
      "id": "y6-002",
      "years": [6],
      "type": "generator",
      "generator": "multiply",
      "params": { "a": [11, 25], "b": [2, 12] }
    }
  ]
}
```

2. Make the group, put the child in it, and map it to a year in a drop-in. `YearGroups=` replaces the default, so keep the Year 3 to 5 entries:

```sh
sudo groupadd edu-year6
sudo usermod -aG edu-students,edu-year6 dana
sudo tee /etc/edu-lm/edu-lm.conf.d/30-years.conf <<'EOT'
[Content]
YearGroups=edu-year3:3,edu-year4:4,edu-year5:5,edu-year6:6
EOT
sudo edu-lm-check
```

`edu-lm-check` should list `dana (year 6)` and the number of questions she can get. A child who is in several year groups gets the union, so `usermod -aG edu-students,edu-year5,edu-year6 dana` gives Year 5 and Year 6 questions together.

A child in `edu-students` but no year group gets `DefaultYear` (4 by default). The group name (`edu-year6`) is yours to choose; only the `group:year` pair in `YearGroups=` matters.

## Writing good questions for children

The problems below came up in the shipped bank. They were found by a second reader or by review, not by the author.

- **Short, concrete sentences.** One idea per question. If it needs a second read, a Year 3 child will guess. Avoid negatives ("which is NOT...") and long true/false statements.
- **One clear right answer.** Check the wrong choices are really wrong (a question asking which word, with choices that are sentences, confuses; so does a "which is an example of X" where two are). Do not let the answer repeat in the question ("categories, like colours").
- **Do not make the right choice the longest or the most detailed one.** Children learn that quickly. Vary the position too.
- **Numeric answers as a child would type them.** Ask in the unit you want and say so: "How many cents? Type a number". Avoid a question whose answer could be typed as 3.30 or 330 or "$3.30". Remember units, fractions and words are not accepted. Use `tolerance` for measuring or estimating.
- **Make short-text answers easy to spell and complete.** List alternatives (`"accept": ["grey", "gray"]`). Leave out answers with several correct wordings.
- **Explanations in plain words**, saying why the right answer is right, not just restating it. Children read them right after a miss. Avoid words the child has not met yet, such as "regroup" or "integer", unless the question teaches them.
- **Be exact with facts and dates.** The reviewed packs had a few wrong ones (a colony that was not yet a colony, an overreaching "first to succeed"). If you are not sure, leave it out.
- **Do not copy an existing question** with changed numbers: the child sees the same idea twice in a week.
- **Choices should not differ only in capital letters**, and should be short enough to fit on a button.
- **Check it on a child** or read it aloud first, and always run `edu-lm-validate` before installing.
