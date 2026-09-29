"""Python mirror of src/edu/core/Answer.cpp: the rules the daemon uses to mark an answer.

tests/test_answer.py holds cases checked against the C++ build. Keep the two in step.
"""
import math
import re
import unicodedata

NBSP = " "
MINUS = "−"

_PLAIN = re.compile(r"-?([0-9]+(\.[0-9]+)?|\.[0-9]+)")
_GROUPED = re.compile(r"-?[0-9]{1,3}(?P<sep>[ ,])[0-9]{3}((?P=sep)[0-9]{3})*(\.[0-9]+)?")
_INDEX = re.compile(r"[+-]?[0-9]+")


def _is_space(c):
    # QChar::isSpace: ASCII/Latin-1 whitespace, U+0085, and Unicode separators
    return c in "\t\n\v\f\r \x85" or unicodedata.category(c).startswith("Z")


def _is_punct(c):
    return unicodedata.category(c).startswith("P")


def _trimmed(text):
    i, j = 0, len(text)
    while i < j and _is_space(text[i]):
        i += 1
    while j > i and _is_space(text[j - 1]):
        j -= 1
    return text[i:j]


def _simplified(text):
    words, cur = [], []
    for c in text:
        if _is_space(c):
            if cur:
                words.append("".join(cur))
                cur = []
        else:
            cur.append(c)
    if cur:
        words.append("".join(cur))
    return " ".join(words)


def _case_fold(text):
    # Qt's toCaseFolded is simple case folding: keep a character whose full fold is longer
    out = []
    for c in text:
        f = c.casefold()
        out.append(f if len(f) == 1 else c)
    return "".join(out)


def format_number(value):
    """formatNumber: groups of three separated by NBSP once there are 5+ digits."""
    negative = value < 0
    digits = str(-value if negative else value)
    if len(digits) >= 5:
        i = len(digits) - 3
        while i > 0:
            digits = digits[:i] + NBSP + digits[i:]
            i -= 3
    return "-" + digits if negative else digits


def parse_number(text):
    """parseNumber: returns a float, or None when the text is not a number."""
    s = _trimmed(text).replace(MINUS, "-").replace(NBSP, " ")
    if not _PLAIN.fullmatch(s):
        if not _GROUPED.fullmatch(s):
            return None
        s = s.replace(" ", "").replace(",", "")
    try:
        v = float(s)
    except ValueError:
        return None
    return v if math.isfinite(v) else None


def normalise_text(text):
    s = _simplified(text)
    while s and _is_punct(s[0]):
        s = s[1:]
    while s and _is_punct(s[-1]):
        s = s[:-1]
    return _case_fold(_simplified(s))


def parse_index(text):
    s = _trimmed(text)
    if not _INDEX.fullmatch(s):
        return None
    v = int(s)
    return v if -(2**31) <= v < 2**31 else None


def parse_bool(text):
    s = _case_fold(_trimmed(text))
    return {"true": True, "false": False}.get(s)


def correct_answer_text(q):
    """correctAnswerText for a pack entry (dict). Empty for generator entries."""
    t = q.get("type")
    if t == "multiple_choice":
        choices = q.get("choices", [])
        i = q.get("answer")
        return choices[i] if isinstance(i, int) and 0 <= i < len(choices) else ""
    if t == "true_false":
        return "True" if q.get("answer") else "False"
    if t == "numeric":
        v = q.get("answer")
        if v == math.floor(v) and abs(v) < 1e15:
            return format_number(int(v))
        return _g10(v)
    if t == "short_text":
        accept = q.get("accept", [])
        return accept[0] if accept else ""
    return ""


def _g10(v):
    # QString::number(v, 'g', 10)
    return format(v, ".10g")


def is_correct(q, given):
    """isCorrect for a pack entry (dict) and the answer text a child would type."""
    t = q.get("type")
    if t == "multiple_choice":
        i = parse_index(given)
        return i is not None and i >= 0 and i == q.get("answer")
    if t == "true_false":
        b = parse_bool(given)
        return b is not None and b == q.get("answer")
    if t == "numeric":
        v = parse_number(given)
        return v is not None and abs(v - q["answer"]) <= q.get("tolerance", 0) + 1e-9
    if t == "short_text":
        g = normalise_text(given)
        return bool(g) and any(normalise_text(a) == g for a in q.get("accept", []))
    return False
