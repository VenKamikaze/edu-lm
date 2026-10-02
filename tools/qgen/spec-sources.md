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

Read verbatim on 2026-10-02 (fetched with curl, HTML text read directly, not through a summarising
tool) from https://www.australiancurriculum.edu.au/copyright-and-terms-of-use (page "Last updated:
April 2021"):

- Section 2: unless indicated otherwise, material on the site is licensed under CC BY 4.0
  (share and adapt, for any purpose including commercial, with attribution to ACARA).
- Section 3: the licence does not cover logos, other trade marks, the website design, material owned
  by third parties, or the Excluded Materials (all photographs, videos and teacher-support
  resources). The National Literacy Learning Progressions are CC BY-NC 4.0.
- Section 4: Indigenous Cultural and Intellectual Property needs the free, prior and informed
  consent of the First Peoples concerned.
- Section 5, the required citation: "(c) Australian Curriculum, Assessment and Reporting Authority
  (ACARA) 2010 to present, unless otherwise indicated. This material was downloaded from the
  Australian Curriculum website (Website) (accessed [insert date]) and [was][was not] modified. The
  material is licensed under CC BY 4.0 Attribution 4.0 International Deed. Version updates are
  tracked in the 'Curriculum version history' section on the 'About the Australian Curriculum'
  page of the Australian Curriculum website." The citation also includes a second paragraph, a no-endorsement
  notice (any product using the material is not affiliated with or approved by ACARA, and alignment
  should be checked against the content descriptions and achievement standards, not the elaborations).
- Section 14: material not under CC BY (or whose terms do not allow modification) needs ACARA's
  permission before it is charged for or republished modified. It does not apply to CC BY material
  such as the codes.

We use the codes (ACARA's, CC BY 4.0) and our own paraphrased descriptions and questions. The
README's licence section carries the full citation (both paragraphs), marked as modified, with the access date.
Not used: logos, photographs, the Literacy Progressions, and anything under section 4.

## Generator ranges

`generator_ranges` in `spec.json` are our own idea of what suits each year (for example Year 3
addition within 10 to 999, following the "two- and three-digit numbers" descriptor). They are
not ACARA's, and are the only intended-difficulty limits `validate` enforces.

## Targets

Cell `target` counts are starting points (about 540 in all, PLAN.md aims for 300 to 500); trim or
raise them per cell in `spec.json`. Maths `number` and `algebra` cells set `generator_share` 0.5,
the other maths strands 0, because the generators only do arithmetic and place value.
