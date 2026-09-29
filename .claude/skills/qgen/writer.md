You are writing quiz questions for an Australian primary-school quiz app. A child must pass a short quiz before they can log in to the family computer. You write ONE file, and nothing else.

## The cell

- Cell: {{cell_id}} ({{subject_label}}, {{year_label}}, {{strand_title}})
- Child: {{year_label}}, age {{age}}.
- Reading level: {{reading_level}}
- Write {{target}} entries in total: {{written_count}} written questions and {{generator_count}} generator entries.

## Curriculum descriptors

Every entry must be written to ONE of these Australian Curriculum v9 descriptors and carry its code in `curriculum`. Spread the entries across all of them (no descriptor should get less than about a third of an even share).

{{descriptors}}

The questions must be ORIGINAL. Write them from the description above and your own knowledge of the topic. Do not reproduce or closely paraphrase curriculum documents, textbooks, worksheets or other published question sets.

## Australian conventions

- Australian spelling (colour, metre, organise, centre, neighbour).
- Metric units only (mm, cm, m, km, g, kg, mL, L, degrees Celsius). Money is Australian dollars and cents (write $4.50, 85c is fine).
- Australian contexts: Australian places, animals, seasons (summer is December to February), school life, sport, foods and holidays. For HASS, Australian and local-community contexts; treat First Nations peoples and cultures with respect and be accurate. Do not assume a northern-hemisphere setting.
- Dates as day, month, year.
- Plain text only. No markdown, no LaTeX, no HTML. Use the characters × ÷ − where they help. Write fractions like 3/4. Write numbers of five or more digits with a space between groups of three (14 527), and four-digit numbers without one (4527).
- Text only: never refer to a picture, diagram, table or graph the child cannot see. If a question needs a small table, passage or list of data, put it in the prompt as plain text.

## File format

Write a JSON file that is an object with a `questions` list. Do NOT write `id`, `years` or `subject`: they are added afterwards. Each entry is one of these types.

multiple_choice (the main type; use it for most written questions):
  {"type": "multiple_choice", "curriculum": "<code>", "prompt": "...", "choices": ["...", "...", "...", "..."], "answer": 1, "difficulty": 2, "explanation": "..."}
  - exactly 4 choices; `answer` is the 0-based index of the one correct choice; vary which position is correct.
  - choices must be different in meaning, not just in case or spacing.

true_false (use sparingly, at most one in ten):
  {"type": "true_false", "curriculum": "<code>", "prompt": "...", "answer": true, "difficulty": 1, "explanation": "..."}

numeric (the child types a number):
  {"type": "numeric", "curriculum": "<code>", "prompt": "...", "answer": 42, "difficulty": 2, "explanation": "..."}
  - `answer` is a JSON number. The child types only the number, so the prompt must say what is asked ("How many centimetres...") and never expect a unit in the answer.
  - Leave `tolerance` out for counting and arithmetic (the answer is exact). Add it only for a measurement or a rounded value where a small error is fine, for example reading a scale: "answer": 2.5, "tolerance": 0.1.

short_text (the child types a word or short phrase; use rarely):
  {"type": "short_text", "curriculum": "<code>", "prompt": "...", "accept": ["colour"], "difficulty": 2, "explanation": "..."}
  - `accept` lists every spelling or form that is right. Checking ignores case, spaces at the ends and punctuation at the ends. Use it only when the answer is one word, a short number word or a short fixed phrase, and list sensible variants.
{{generator_format}}
Rules for every entry:
- Exactly one unambiguous correct answer. Check the arithmetic and the facts yourself. If a question depends on opinion or on what a particular class was taught, do not write it.
- Wrong choices must be plausible: the mistakes a child of this age really makes (an off-by-one place value, adding instead of multiplying, a near-miss spelling), not silly or obviously absurd.
- `explanation` is required on every written question: one or two short sentences a child can read, saying why the answer is right. Not needed on generator entries.
- `difficulty` is 1, 2 or 3 (1 easy for the year, 2 typical, 3 a stretch). Aim for roughly a third at each level.
- Do not repeat a question or write near-copies that differ only in the numbers or names.
- The prompt must make sense read on its own, in one screen.
{{generator_section}}{{retry_section}}
## Output

Write the draft as JSON to exactly this path, using the Write tool:

{{output_path}}

Write only that file. Do not create any other file, do not run any commands, and do not read any question files in the project, except the earlier draft if this prompt names one. When the file is written, reply with one line: how many entries it holds.
