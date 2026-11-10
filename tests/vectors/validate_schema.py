#!/usr/bin/env python3
"""Validate the official pikaid vector document against its JSON Schema.

The library build does not require Python or jsonschema. When jsonschema is
missing this script exits 77 so CTest records a skip rather than a pass, unless
--require is given, in which case the missing dependency is a failure.

Exit status: 0 valid, 1 invalid, unreadable or (with --require) missing
jsonschema, 77 jsonschema unavailable without --require.
"""

from __future__ import annotations

import argparse
import json
import pathlib
import sys

SKIP_STATUS = 77


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--schema", required=True, type=pathlib.Path)
    parser.add_argument("--json", required=True, type=pathlib.Path)
    parser.add_argument(
        "--require",
        action="store_true",
        help="treat a missing jsonschema module as failure instead of a skip")
    arguments = parser.parse_args(argv)

    try:
        import jsonschema  # type: ignore[import-not-found]
    except ImportError:
        if arguments.require:
            print(
                "validate_schema: FAIL (python jsonschema is not installed and "
                "--require was given)",
                file=sys.stderr)
            return 1
        print("validate_schema: SKIP (python jsonschema is not installed)")
        return SKIP_STATUS

    try:
        schema = json.loads(arguments.schema.read_text(encoding="utf-8"))
        document = json.loads(arguments.json.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        print(f"validate_schema: {error}", file=sys.stderr)
        return 1

    validator_class = jsonschema.validators.validator_for(schema)
    validator_class.check_schema(schema)
    validator = validator_class(schema)
    errors = sorted(validator.iter_errors(document), key=lambda error: error.path)

    if not errors:
        print(f"validate_schema: {arguments.json} is valid against "
              f"{arguments.schema}")
        return 0

    for error in errors:
        location = "/".join(str(part) for part in error.absolute_path) or "<root>"
        print(f"validate_schema: {location}: {error.message}", file=sys.stderr)
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
