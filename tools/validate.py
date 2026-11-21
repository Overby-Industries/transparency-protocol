#!/usr/bin/env python3
"""Validate OTB-1 records against schema/otb-1.schema.json.

Usage:
    python tools/validate.py [--expect-invalid] [file_or_dir ...]

With no arguments, validates every *.json file under examples/.

A *.json file holds one record. A *.jsonl file holds one record per line,
which is the natural shape for a published ledger file:

    python tools/validate.py ledger-2025.jsonl

--expect-invalid flips the check: every record given must FAIL validation.
That is how tests/invalid/ is run, so a schema change that quietly starts
accepting a malformed record is caught the same way one that breaks a good
example is.

Timestamps are checked as real RFC 3339 instants, not just as strings of
the right shape: 2025-02-30T00:00:00Z is rejected. JSON Schema leaves
"format" optional for validators to enforce; this one enforces it. It also
reads "pattern" the way JSON Schema defines it, as an ECMA-262 regular
expression, where "$" means the very end of the string.
"""
import json
import re
import sys
from pathlib import Path

try:
    import jsonschema
except ImportError:
    print("Missing dependency: pip install jsonschema", file=sys.stderr)
    sys.exit(2)

REPO_ROOT = Path(__file__).resolve().parent.parent
SCHEMA_PATH = REPO_ROOT / "schema" / "otb-1.schema.json"

_RFC3339 = re.compile(
    r"([0-9]{4})-([0-9]{2})-([0-9]{2})T([0-9]{2}):([0-9]{2}):([0-9]{2})"
    r"(?:\.([0-9]+))?(?:Z|([+-])([0-9]{2}):([0-9]{2}))"
)
_DAYS_IN_MONTH = (31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31)


def _days_from_civil(year, month, day):
    """Days from 1970-01-01 to a proleptic-Gregorian date (any year)."""
    year -= month <= 2
    era = (year if year >= 0 else year - 399) // 400
    year_of_era = year - era * 400
    day_of_year = (153 * (month - 3 if month > 2 else month + 9) + 2) // 5 + day - 1
    day_of_era = year_of_era * 365 + year_of_era // 4 - year_of_era // 100 + day_of_year
    return era * 146097 + day_of_era - 719468


def parse_timestamp_ms(text):
    """An OTB-1 timestamp as milliseconds since the Unix epoch, or None.

    The same rules as libotb1's parseTimestamp, so the schema validator and
    the C++ decoder agree on what a timestamp is: uppercase T, 'Z' or a
    numeric offset, optional fraction (truncated to milliseconds), and a
    date and time that actually exist. A seconds value of 60 is allowed,
    for a leap second.
    """
    match = _RFC3339.fullmatch(text)
    if not match:
        return None
    year, month, day, hour, minute, second = (int(g) for g in match.groups()[:6])
    fraction, sign, offset_hour, offset_minute = match.groups()[6:]
    leap = (year % 4 == 0 and year % 100 != 0) or year % 400 == 0
    if not 1 <= month <= 12:
        return None
    days_in_month = 29 if month == 2 and leap else _DAYS_IN_MONTH[month - 1]
    if not 1 <= day <= days_in_month or hour > 23 or minute > 59 or second > 60:
        return None
    offset = 0
    if sign is not None:
        if int(offset_hour) > 23 or int(offset_minute) > 59:
            return None
        offset = int(offset_hour) * 60 + int(offset_minute)
        if sign == "-":
            offset = -offset
    ms = int((fraction or "0")[:3].ljust(3, "0"))
    seconds = hour * 3600 + minute * 60 + second - offset * 60
    return _days_from_civil(year, month, day) * 86400000 + seconds * 1000 + ms


def load_schema():
    with open(SCHEMA_PATH, encoding="utf-8") as f:
        return json.load(f)


def _ecma_pattern(validator, pattern, instance, schema):
    """The "pattern" keyword, read as JSON Schema defines it.

    Patterns are ECMA-262 regular expressions, where "$" matches only at
    the very end of the string. Python's "$" also matches just before a
    trailing newline, which would let the id "INV-2025-0001\n" through.
    """
    if not validator.is_type(instance, "string"):
        return
    anchored = pattern
    if pattern.endswith("$") and not pattern.endswith("\\$"):
        anchored = pattern[:-1] + "\\Z"
    if not re.search(anchored, instance):
        yield jsonschema.ValidationError(f"{instance!r} does not match {pattern!r}")


_Validator = jsonschema.validators.extend(
    jsonschema.Draft202012Validator, {"pattern": _ecma_pattern})


def make_validator(schema=None):
    """The OTB-1 validator: the schema, with timestamps actually checked."""
    schema = schema or load_schema()
    jsonschema.Draft202012Validator.check_schema(schema)
    formats = jsonschema.FormatChecker(formats=())

    @formats.checks("date-time")
    def _is_timestamp(value):
        # "format" constrains strings only; other types are the business of
        # the "type" keyword.
        return not isinstance(value, str) or parse_timestamp_ms(value) is not None

    return _Validator(schema, format_checker=formats)


def problems(validator, record):
    """Human-readable validation errors for one record; empty if it is valid."""
    errors = sorted(validator.iter_errors(record), key=lambda e: list(map(str, e.path)))
    return [
        f"  {'/'.join(str(p) for p in err.path) or '<root>'}: {err.message}"
        for err in errors
    ]


def collect_targets(args):
    if not args:
        return sorted((REPO_ROOT / "examples").glob("*.json"))
    targets = []
    for arg in args:
        p = Path(arg)
        if p.is_dir():
            targets.extend(sorted(p.glob("*.json")))
            targets.extend(sorted(p.glob("*.jsonl")))
        else:
            targets.append(p)
    return targets


def load_records(path):
    """Yield (label, record, parse_error) for every record in a file."""
    with open(path, encoding="utf-8") as f:
        if path.suffix == ".jsonl":
            for number, line in enumerate(f, start=1):
                if not line.strip():
                    continue
                label = f"{path}:{number}"
                try:
                    yield label, json.loads(line), None
                except json.JSONDecodeError as exc:
                    yield label, None, f"not valid JSON: {exc}"
        else:
            try:
                yield str(path), json.load(f), None
            except json.JSONDecodeError as exc:
                yield str(path), None, f"not valid JSON: {exc}"


def main():
    args = sys.argv[1:]
    expect_invalid = "--expect-invalid" in args
    args = [a for a in args if a != "--expect-invalid"]

    validator = make_validator()

    targets = collect_targets(args)
    if not targets:
        print("No files to validate.")
        return 0

    checked = 0
    failures = 0
    for path in targets:
        # A ledger file can hold thousands of records; print one line per file
        # for those and one line per record only when something is wrong.
        quiet = path.suffix == ".jsonl"
        file_checked = 0
        file_failures = 0
        for label, record, parse_error in load_records(path):
            checked += 1
            file_checked += 1
            if parse_error:
                messages = [f"  <root>: {parse_error}"]
            else:
                messages = problems(validator, record)
            is_invalid = bool(messages)

            if expect_invalid:
                if is_invalid:
                    if not quiet:
                        print(f"OK   {label}  (rejected, as it should be)")
                else:
                    failures += 1
                    file_failures += 1
                    print(f"FAIL {label}  (accepted, but must be rejected)")
            elif is_invalid:
                failures += 1
                file_failures += 1
                print(f"FAIL {label}")
                for message in messages:
                    print(message)
            elif not quiet:
                print(f"OK   {label}")

        if quiet and not file_failures:
            print(f"OK   {path}  ({file_checked} records)")

    if failures:
        print(f"\n{failures} of {checked} record(s) failed the check.")
        return 1
    if expect_invalid:
        print(f"\nAll {checked} record(s) rejected by OTB-1, as expected.")
    else:
        print(f"\nAll {checked} record(s) valid against OTB-1.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
