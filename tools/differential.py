#!/usr/bin/env python3
"""Hold the C++ decoder and the JSON Schema to the same verdict.

OTB-1 has two implementations in this repository: the JSON Schema, run by
tools/validate.py, and libotb1's C++ decoder, run by otb1_check. Each is
tested on its own against examples/ and tests/invalid/. This tool tests
them against each other, on far more records than anyone would write by
hand, and fails if they ever disagree.

Usage:
    cmake -S . -B build && cmake --build build
    python tools/differential.py [--cli build/otb1_check] [--seed N] [--only CHECK]

Three checks. All are deterministic: the same seed produces the same
records, so a failure can be reproduced exactly.

  records   Mutate the example records structurally -- drop a field, add
            one, swap a value for another type, another enum value, a
            boundary number, a malformed id or timestamp -- and require the
            schema and the C++ decoder to give the same accept-or-reject
            verdict on every one.

  lossless  For every record both accept, require the C++ re-encoding to be
            valid OTB-1 that says exactly what the input said. Nothing may
            be dropped, renamed or altered on the way through. For a
            transparency record that matters more than anywhere: a cost
            that came back rounded would be a different claim.

  syntax    Mutate JSON text at the byte level, and generate random valid
            documents, and require the C++ JSON parser to accept exactly
            what a strict RFC 8259 reader accepts, with the same value.

Exit status is 0 when the implementations agree everywhere, 1 otherwise.
"""
import argparse
import copy
import json
import math
import random
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import validate  # noqa: E402  (tools/validate.py)

REPO_ROOT = validate.REPO_ROOT
MAX_DEPTH = 32  # libotb1's nesting bound; see cpp/src/json.cpp
TIMESTAMP_FIELDS = ("timestamp",)

# --------------------------------------------------------------------------
# Running the C++ side


def run_cli(cli, mode, inputs):
    """Feed byte strings to otb1_check; return [(accepted, payload_bytes_or_reason)]."""
    stdin = "".join(data.hex() + "\n" for data in inputs)
    done = subprocess.run([str(cli), mode], input=stdin, capture_output=True, text=True)
    if done.returncode != 0:
        sys.exit(f"{cli} {mode} exited with {done.returncode}:\n{done.stderr}")
    lines = done.stdout.split("\n")
    if lines and lines[-1] == "":
        lines.pop()
    if len(lines) != len(inputs):
        sys.exit(f"{cli} {mode}: sent {len(inputs)} inputs, got {len(lines)} verdicts")
    verdicts = []
    for line in lines:
        if line.startswith("ok "):
            verdicts.append((True, bytes.fromhex(line[3:])))
        elif line.startswith("fail"):
            verdicts.append((False, line[5:]))
        else:
            sys.exit(f"{cli} {mode}: unreadable verdict line {line!r}")
    return verdicts


# --------------------------------------------------------------------------
# Structural mutation of records


def paths_of(doc, prefix=()):
    """Every location in a document, as a tuple of keys and indices."""
    if isinstance(doc, dict):
        for key, value in doc.items():
            yield prefix + (key,)
            yield from paths_of(value, prefix + (key,))
    elif isinstance(doc, list):
        for index, value in enumerate(doc):
            yield prefix + (index,)
            yield from paths_of(value, prefix + (index,))


def get_at(doc, path):
    for step in path:
        doc = doc[step]
    return doc


def with_value(doc, path, value):
    out = copy.deepcopy(doc)
    if not path:
        return copy.deepcopy(value)
    get_at(out, path[:-1])[path[-1]] = copy.deepcopy(value)
    return out


def without(doc, path):
    out = copy.deepcopy(doc)
    del get_at(out, path[:-1])[path[-1]]
    return out


def strings_in_schema(node, found):
    """Every enum value and const in the schema: the words OTB-1 knows."""
    if isinstance(node, dict):
        for key, value in node.items():
            if key == "enum":
                found.update(v for v in value if isinstance(v, str))
            elif key == "const" and isinstance(value, str):
                found.add(value)
            else:
                strings_in_schema(value, found)
    elif isinstance(node, list):
        for item in node:
            strings_in_schema(item, found)


WRONG_TYPES = [None, True, False, 0, 1, -1, 1.5, "", "x", [], {}, ["land"], {"site": "Houston"}]

NUMBERS = [-180.5, -180, -90.0001, -90, -1, -0.5, -0.0, 0, 0.01, 0.5, 1, 1.0000001, 90, 90.0001,
           180, 180.5, 1234567.89, 2**53, 2**53 + 2, 1e16, 1e300]

INVESTMENT_IDS = [
    "INV-2025-0001",
    "INV-2031-123456",
    "INV-0000-0000",
    "INV-2025-001",         # serial too short
    "INV-25-0001",          # year too short
    "INV-20250-0001",       # year too long
    "INV-20250001",
    "inv-2025-0001",
    "XINV-2025-0001",
    "INV-2O25-0001",        # a letter O
    "INV-2025-0001 ",
    " INV-2025-0001",
    "INV-2025-0001\n",      # what a careless "$" lets through
    "INV-2025-00-01",
    "INV-\uff12\uff10\uff12\uff15-0001",  # full-width digits are not digits here
]

TIMESTAMPS = [
    "2025-02-18T14:05:22Z",
    "2025-02-18T14:05:22.5Z",
    "2025-02-18T14:05:22.123456Z",
    "2025-02-18T15:05:22+01:00",
    "2025-02-18T09:35:22-04:30",
    "2024-02-29T00:00:00Z",        # a real leap day
    "2016-12-31T23:59:60Z",        # a leap second
    "0000-01-01T00:00:00Z",
    "9999-12-31T23:59:59Z",
    "2025-02-29T00:00:00Z",        # not a leap year
    "2025-02-30T00:00:00Z",
    "2025-13-01T00:00:00Z",
    "2025-00-10T00:00:00Z",
    "2025-02-18T24:00:00Z",
    "2025-02-18T14:60:00Z",
    "2025-02-18T14:05:61Z",
    "2025-02-18T14:05:22",         # no zone
    "2025-02-18T14:05:22z",
    "2025-02-18t14:05:22Z",
    "2025-02-18 14:05:22Z",
    "2025-02-18T14:05:22.Z",
    "2025-02-18T14:05:22+0100",
    "2025-02-18T14:05:22+24:00",
    "2025-02-18T14:05:22Z ",
    "2025-02-18T14:05:22Z\n",
    "2025-02-18",
    "last tuesday",
]


def single_mutations(seed, vocabulary, field_bank):
    """Every one-step mutation of a record."""
    yield from ([], "text", 7, None)  # not an object at all

    for path in paths_of(seed):
        value = get_at(seed, path)
        yield without(seed, path)
        for wrong in WRONG_TYPES:
            yield with_value(seed, path, wrong)
        if isinstance(value, str):
            for word in vocabulary:
                yield with_value(seed, path, word)
        if isinstance(value, (int, float)) and not isinstance(value, bool):
            for number in NUMBERS:
                yield with_value(seed, path, number)
        if isinstance(value, list):
            if value:
                yield with_value(seed, path, value + [value[0]])  # a repeat
            yield with_value(seed, path, value + [5])
            yield with_value(seed, path, [])

    # Fields that do not belong: an unknown one, and every real OTB-1 field
    # placed where this record does not already have it.
    containers = [()] + [p for p in paths_of(seed) if isinstance(get_at(seed, p), dict)]
    for path in containers:
        here = get_at(seed, path)
        yield with_value(seed, path + ("x-unknown",), 1)
        for name, value in field_bank.items():
            if name not in here:
                yield with_value(seed, path + (name,), value)


def random_mutation(doc, rng, vocabulary, field_bank):
    """One random step, for compounding several mutations in one record."""
    paths = list(paths_of(doc)) if isinstance(doc, (dict, list)) else []
    if not paths:
        return doc
    path = rng.choice(paths)
    value = get_at(doc, path)
    roll = rng.random()
    if roll < 0.20:
        return without(doc, path)
    if roll < 0.40:
        return with_value(doc, path, rng.choice(WRONG_TYPES))
    if roll < 0.60 and isinstance(value, str):
        return with_value(doc, path, rng.choice(vocabulary))
    if roll < 0.75 and isinstance(value, (int, float)) and not isinstance(value, bool):
        return with_value(doc, path, rng.choice(NUMBERS))
    if isinstance(value, dict):
        name = rng.choice(list(field_bank))
        return with_value(doc, path + (name,), field_bank[name])
    return with_value(doc, path, rng.choice(WRONG_TYPES))


def normalised(doc):
    """A record reduced to what it says, so two spellings can be compared."""
    if isinstance(doc, dict):
        out = {}
        for key, value in doc.items():
            if key in TIMESTAMP_FIELDS and isinstance(value, str):
                out[key] = ("instant", validate.parse_timestamp_ms(value))
            else:
                out[key] = normalised(value)
        return out
    if isinstance(doc, list):
        return [normalised(item) for item in doc]
    if isinstance(doc, bool):
        return doc
    if isinstance(doc, (int, float)):
        return float(doc)
    return doc


def check_records(cli, seed_value, quiet):
    schema = validate.load_schema()
    validator = validate.make_validator(schema)

    seeds = [json.loads(p.read_text(encoding="utf-8"))
             for p in sorted((REPO_ROOT / "examples").glob("*.json"))]
    if not seeds:
        sys.exit("no example records to mutate")

    words = set()
    strings_in_schema(schema, words)
    vocabulary = sorted(words) + TIMESTAMPS + INVESTMENT_IDS + ["", " ", "otb-1", "OTB-2"]

    # One plausible value for every field name that appears in any example.
    field_bank = {}
    for seed in seeds:
        for path in paths_of(seed):
            if isinstance(path[-1], str):
                field_bank.setdefault(path[-1], get_at(seed, path))

    mutants = list(seeds)
    for seed in seeds:
        mutants.extend(single_mutations(seed, vocabulary, field_bank))

    rng = random.Random(seed_value)
    for _ in range(6000):
        doc = copy.deepcopy(rng.choice(seeds))
        for _ in range(rng.choice((2, 2, 3, 4))):
            doc = random_mutation(doc, rng, vocabulary, field_bank)
        mutants.append(doc)

    texts = [json.dumps(m, ensure_ascii=False).encode("utf-8") for m in mutants]
    verdicts = run_cli(cli, "--stream", texts)

    disagreements = []
    lossy = []
    both_accept = both_reject = 0
    for mutant, text, (cpp_ok, payload) in zip(mutants, texts, verdicts):
        schema_ok = validator.is_valid(mutant)
        if schema_ok != cpp_ok:
            disagreements.append((text, schema_ok, cpp_ok, payload))
            continue
        if not schema_ok:
            both_reject += 1
            continue
        both_accept += 1
        try:
            echoed = json.loads(payload.decode("utf-8"))
        except (UnicodeDecodeError, json.JSONDecodeError) as exc:
            lossy.append((text, payload, f"re-encoding is not JSON: {exc}"))
            continue
        if not validator.is_valid(echoed):
            lossy.append((text, payload, "re-encoding is not valid OTB-1"))
        elif normalised(echoed) != normalised(mutant):
            lossy.append((text, payload, "re-encoding says something different"))

    print(f"records   {len(mutants)} records: {both_accept} accepted by both, "
          f"{both_reject} rejected by both, {len(disagreements)} disagreements")
    for text, schema_ok, cpp_ok, payload in disagreements[:20]:
        print(f"  DISAGREE  schema={'accept' if schema_ok else 'reject'}  "
              f"c++={'accept' if cpp_ok else 'reject (' + str(payload) + ')'}")
        print(f"            {text.decode('utf-8')}")
        if schema_ok is False and not quiet:
            for line in validate.problems(validator, json.loads(text)):
                print(f"            schema says:{line}")
    if len(disagreements) > 20:
        print(f"  ... and {len(disagreements) - 20} more")

    print(f"lossless  {both_accept} accepted records re-encoded: {len(lossy)} altered")
    for text, payload, why in lossy[:20]:
        print(f"  LOSSY     {why}")
        print(f"            in:  {text.decode('utf-8')}")
        print(f"            out: {payload.decode('utf-8', 'replace')}")

    # A check that never sees an accepted record, or never sees a rejected
    # one, is not checking anything.
    if both_accept < 100 or both_reject < 100:
        print("  the mutation set is too one-sided to be a meaningful check")
        return False
    return not disagreements and not lossy


# --------------------------------------------------------------------------
# Byte-level fuzzing of the JSON parser


class Rejected(Exception):
    pass


def strict_json(data):
    """Parse bytes as strict RFC 8259 JSON, the way libotb1's parser must.

    Python's json module is more forgiving than the RFC in a few places;
    each is closed here. Numbers come back as floats so that values compare
    the way a parser holding doubles sees them.
    """
    try:
        text = data.decode("utf-8")
    except UnicodeDecodeError:
        raise Rejected("not UTF-8")

    def pairs(items):
        out = {}
        for key, value in items:
            if key in out:
                raise Rejected("duplicate key")
            out[key] = value
        return out

    def constant(name):
        raise Rejected(f"{name} is not JSON")

    def real(token):
        value = float(token)
        if not math.isfinite(value):
            raise Rejected("number out of range")
        return value

    def integer(token):
        try:
            return float(int(token))
        except (ValueError, OverflowError):
            raise Rejected("number out of range")

    try:
        value = json.loads(text, object_pairs_hook=pairs, parse_constant=constant,
                           parse_float=real, parse_int=integer)
    except (json.JSONDecodeError, RecursionError) as exc:
        raise Rejected(str(exc))

    def well_formed(text):
        return not any(0xD800 <= ord(c) <= 0xDFFF for c in text)

    pending = [(value, 0)]
    while pending:
        node, depth = pending.pop()
        if depth > MAX_DEPTH:
            raise Rejected("nested too deep")
        if isinstance(node, str) and not well_formed(node):
            raise Rejected("lone surrogate")
        if isinstance(node, dict):
            for key, child in node.items():
                if not well_formed(key):
                    raise Rejected("lone surrogate in key")
                pending.append((child, depth + 1))
        elif isinstance(node, list):
            pending.extend((child, depth + 1) for child in node)
    return value


ALPHABET = (b'{}[]",:\\/ \t\n\r0123456789.eE+-truefalsnu'
            + bytes([0x00, 0x1F, 0x7F, 0x80, 0xBF, 0xC0, 0xC2, 0xE0, 0xED, 0xF0, 0xF4, 0xFF])
            + b"abcdefABCDEF")

HANDWRITTEN = [
    b'"\\ud83d\\ude80"', b'"\\ud83d"', b'"\\ude80\\ud83d"', b'"\\u0000\\u001f\\u007f"',
    b'[1,2.5e3,-0,true,false,null,"a\\n\\t\\"\\\\\\/"]', b'{"a":{"b":[{}]}}', b'{"a":1,"a":2}',
    b'{"a":1,"\\u0061":2}', "\"café € \U0001F680\"".encode("utf-8"),
    b"1e308", b"1e309", b"-1e309", b"1e-400", b"0.1", b"-0", b"-0.0", b"01", b"1.", b".5",
    b"123456789012345678901234567890", b"9007199254740993", b"NaN", b"Infinity", b"-Infinity",
    b"\xef\xbb\xbf{}", b" \t\r\n[ ] \t\r\n", b"", b" ", b"nul", b"truee",
    b"[" * 32 + b"]" * 32, b"[" * 33 + b"]" * 33, b"[" * 34 + b"]" * 34,
    b"[" * 32 + b"1" + b"]" * 32, b"[" * 33 + b"1" + b"]" * 33,
    b'{"a":' * 32 + b"1" + b"}" * 32, b'{"a":' * 33 + b"1" + b"}" * 33,
]


def mutate_bytes(data, rng):
    data = bytearray(data)
    for _ in range(rng.choice((1, 1, 1, 2, 2, 3, 4))):
        roll = rng.random()
        at = rng.randrange(len(data) + 1)
        if roll < 0.30 and data:
            at = min(at, len(data) - 1)
            data[at] = rng.choice(ALPHABET) if rng.random() < 0.8 else rng.randrange(256)
        elif roll < 0.50 and data:
            del data[at:at + rng.randint(1, 8)]
        elif roll < 0.75:
            data[at:at] = bytes(rng.choice(ALPHABET) for _ in range(rng.randint(1, 6)))
        elif roll < 0.85 and data:
            end = min(len(data), at + rng.randint(1, 12))
            data[at:at] = data[at:end]
        elif roll < 0.93 and len(data) > 1:
            at = min(at, len(data) - 2)
            data[at], data[at + 1] = data[at + 1], data[at]
        else:
            del data[at:]
    return bytes(data)


def random_text(rng):
    ranges = ((0x20, 0x7E), (0x00, 0x1F), (0x7F, 0xFF), (0x100, 0xD7FF), (0xE000, 0xFFFF),
              (0x10000, 0x10FFFF))
    weights = (60, 8, 8, 10, 6, 8)
    out = []
    for _ in range(rng.randint(0, 12)):
        low, high = rng.choices(ranges, weights)[0]
        out.append(chr(rng.randint(low, high)))
    return "".join(out) if rng.random() < 0.9 else rng.choice(('"', "\\", "/", "\\u", "\n"))


def random_number(rng):
    roll = rng.random()
    if roll < 0.25:
        return rng.randint(-1000, 1000)
    if roll < 0.40:
        return rng.randint(-10**20, 10**20)
    if roll < 0.55:
        return rng.choice((0, -0.0, 2**53, 2**53 + 1, -(2**53), 1e15, 9e15, 1e16, 1e-7, 5e-324,
                           1.7976931348623157e308))
    if roll < 0.80:
        return rng.uniform(-1e6, 1e6)
    return rng.uniform(-1, 1) * 10 ** rng.randint(-300, 300)


def random_document(rng, depth=0):
    roll = rng.random()
    if depth >= 6 or roll < 0.45:
        kind = rng.random()
        if kind < 0.40:
            return random_number(rng)
        if kind < 0.80:
            return random_text(rng)
        return rng.choice((True, False, None))
    if roll < 0.75:
        return [random_document(rng, depth + 1) for _ in range(rng.randint(0, 5))]
    return {random_text(rng): random_document(rng, depth + 1) for _ in range(rng.randint(0, 5))}


def as_floats(value):
    if isinstance(value, bool):
        return value
    if isinstance(value, (int, float)):
        return float(value)
    if isinstance(value, list):
        return [as_floats(item) for item in value]
    if isinstance(value, dict):
        return {key: as_floats(item) for key, item in value.items()}
    return value


def check_syntax(cli, seed_value):
    rng = random.Random(seed_value)

    seeds = list(HANDWRITTEN)
    for path in sorted((REPO_ROOT / "examples").glob("*.json")):
        raw = path.read_bytes()
        seeds.append(raw)
        seeds.append(json.dumps(json.loads(raw), separators=(",", ":")).encode("utf-8"))

    inputs = list(seeds)
    for _ in range(40000):
        inputs.append(mutate_bytes(rng.choice(seeds), rng))

    # Valid by construction, in every spelling Python will write: escaped or
    # raw non-ASCII, compact or indented.
    generated = 0
    for _ in range(6000):
        document = random_document(rng)
        text = json.dumps(document, ensure_ascii=rng.random() < 0.5,
                          indent=rng.choice((None, None, 1)),
                          separators=rng.choice(((",", ":"), (", ", ": "))))
        inputs.append(text.encode("utf-8"))
        generated += 1

    verdicts = run_cli(cli, "--stream-json", inputs)

    problems = []
    accepted = rejected = 0
    for data, (cpp_ok, payload) in zip(inputs, verdicts):
        try:
            expected = strict_json(data)
            strict_ok = True
        except Rejected as exc:
            expected = None
            strict_ok = False
            why = str(exc)
        if strict_ok != cpp_ok:
            detail = f"c++ rejects ({payload})" if strict_ok else f"reference rejects ({why})"
            problems.append((data, f"verdicts differ: {detail}"))
            continue
        if not strict_ok:
            rejected += 1
            continue
        accepted += 1
        try:
            echoed = strict_json(payload)
        except Rejected as exc:
            problems.append((data, f"c++ output is not strict JSON ({exc}): {payload!r}"))
            continue
        if as_floats(echoed) != as_floats(expected):
            problems.append((data, f"value changed: c++ wrote {payload!r}"))

    print(f"syntax    {len(inputs)} inputs ({generated} generated valid): {accepted} accepted "
          f"by both, {rejected} rejected by both, {len(problems)} disagreements")
    for data, why in problems[:20]:
        print(f"  DISAGREE  {why}")
        print(f"            input: {data!r}")
    if len(problems) > 20:
        print(f"  ... and {len(problems) - 20} more")

    if accepted < 100 or rejected < 100:
        print("  the input set is too one-sided to be a meaningful check")
        return False
    return not problems


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--cli", type=Path, default=REPO_ROOT / "build" / "otb1_check",
                        help="path to the otb1_check executable (default: build/otb1_check)")
    parser.add_argument("--seed", type=int, default=1, help="seed for the random mutations")
    parser.add_argument("--only", choices=("records", "syntax"),
                        help="run one check instead of all")
    parser.add_argument("--quiet", action="store_true",
                        help="do not print the schema's reasons for each disagreement")
    args = parser.parse_args()

    if not args.cli.exists():
        sys.exit(f"{args.cli} not found. Build it first:\n"
                 "  cmake -S . -B build && cmake --build build")

    ok = True
    if args.only in (None, "records"):
        ok = check_records(args.cli, args.seed, args.quiet) and ok
    if args.only in (None, "syntax"):
        ok = check_syntax(args.cli, args.seed) and ok

    print("\nThe C++ decoder and the JSON Schema agree." if ok
          else "\nThe C++ decoder and the JSON Schema DISAGREE. See above.")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
