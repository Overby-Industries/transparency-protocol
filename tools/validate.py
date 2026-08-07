#!/usr/bin/env python3
"""Validate OTB-1 example/ledger files against schema/otb-1.schema.json.

Usage:
    python tools/validate.py [file_or_dir ...]

With no arguments, validates every *.json file under examples/.
"""
import json
import sys
from pathlib import Path

try:
    import jsonschema
except ImportError:
    print("Missing dependency: pip install jsonschema", file=sys.stderr)
    sys.exit(2)

REPO_ROOT = Path(__file__).resolve().parent.parent
SCHEMA_PATH = REPO_ROOT / "schema" / "otb-1.schema.json"


def load_schema():
    with open(SCHEMA_PATH, encoding="utf-8") as f:
        return json.load(f)


def collect_targets(args):
    if not args:
        return sorted((REPO_ROOT / "examples").glob("*.json"))
    targets = []
    for arg in args:
        p = Path(arg)
        targets.extend(sorted(p.glob("*.json")) if p.is_dir() else [p])
    return targets


def main():
    schema = load_schema()
    validator = jsonschema.Draft202012Validator(schema)

    targets = collect_targets(sys.argv[1:])
    if not targets:
        print("No files to validate.")
        return 0

    failures = 0
    for path in targets:
        with open(path, encoding="utf-8") as f:
            instance = json.load(f)
        errors = sorted(validator.iter_errors(instance), key=lambda e: e.path)
        if errors:
            failures += 1
            print(f"FAIL {path}")
            for err in errors:
                loc = "/".join(str(p) for p in err.path) or "<root>"
                print(f"  {loc}: {err.message}")
        else:
            print(f"OK   {path}")

    if failures:
        print(f"\n{failures} of {len(targets)} file(s) failed validation.")
        return 1
    print(f"\nAll {len(targets)} file(s) valid against OTB-1.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
