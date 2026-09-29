# Where spec.json comes from

## Descriptor codes

Every code in `spec.json` (Years 3 to 5; maths, science, HASS, English) was checked against ACARA's
official curriculum workbook for Australian Curriculum v9, the "Download entire curriculum" Excel
file linked from the Downloads page:

- Downloads page: https://www.australiancurriculum.edu.au/downloads
- Workbook: https://www.australiancurriculum.edu.au/content/dam/en/curriculum/ac-version-9/downloads/curriculum-workbook.xlsx
  (fetched 2026-09-30; `Learning areas` sheet, column `Code`)
- Learning area overviews: https://www.australiancurriculum.edu.au/curriculum-information/understand-this-learning-area/mathematics

`check_spec_codes.py <workbook.xlsx>` re-runs the check (stdlib only). On 2026-09-30 it reported 231
codes, none missing, and no workbook code for our subjects and years left out of a cell. Codes that
are in a cell's `not_quizzable` list (activities, speaking, writing tasks, things that need an
image) are still real codes; they are listed so the gap is deliberate.

The descriptions in `spec.json` are our own paraphrases written from reading the workbook. They are
shorter than ACARA's text and are not copies: `check_spec_codes.py` fails if a description shares
more than 6 consecutive words with the workbook's content description (on 2026-09-30 the longest
shared run across all 193 descriptions was 6 words, after rewording the ones that exceeded it and most that reached it).
The question packs contain only the codes and our own questions.

The Australian Curriculum website's own year pages could not be read by the fetch tool used
(the pages are script-driven), so the workbook is the only source consulted. A secondary source
(QCAA's Year 3 alignment PDF) returned 403 and was not used.

## Licence, as found

From https://www.australiancurriculum.edu.au/copyright-and-terms-of-use (read through a summarising
fetch tool on 2026-09-30, not verbatim; read the page itself before publishing):

- Most ACARA material is CC BY 4.0: sharing and adapting are allowed, including commercially, with
  attribution to ACARA. Suggested wording: "(c) Australian Curriculum, Assessment and Reporting
  Authority (ACARA) 2010 to present, unless otherwise indicated. This material was downloaded from
  the Australian Curriculum website", with the access date and a note of any modification.
- Excluded from CC BY: logos, photographs, videos, teacher resources and third-party material.
- The National Literacy Learning Progressions are CC BY-NC 4.0.
- Indigenous cultural and intellectual property needs the consent of the First Peoples concerned.
- ACARA asks to be contacted before anyone charges for access to its material.

We store codes and our own paraphrases only. If the repository is published (PLAN phase 6), add
the attribution line to the README.

## Generator ranges

`generator_ranges` in `spec.json` are our own idea of what suits each year (for example Year 3
addition within 10 to 999, following the "two- and three-digit numbers" descriptor). They are
not ACARA's, and are the only intended-difficulty limits `validate` enforces.

## Targets

Cell `target` counts are starting points (about 540 in all, PLAN.md aims for 300 to 500); trim or
raise them per cell in `spec.json`. Maths `number` and `algebra` cells set `generator_share` 0.5,
the other maths strands 0, because the generators only do arithmetic and place value.
