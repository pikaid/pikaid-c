#!/usr/bin/env python3
"""Generate C conformance fixtures from the official pikaid vector JSON.

The generator never imports or executes the C implementation. It re-derives
every vector from the documented layout using Python integers, so a corrupt or
mis-transcribed fixture cannot silently become a test oracle.

Usage:
    generate_vectors.py --json tests/vectors/pikaid-v1.json \
                        --output tests/vectors/pikaid_vectors.h
    generate_vectors.py ... --check     # exit 1 if the committed file differs

Exit status: 0 success, 1 mismatch in --check mode, 2 invalid fixture.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import pathlib
import sys

ALPHABET = "0123456789abcdefghijklmnopqrstuvwxyz"
STRING_LENGTH = 26
TIMESTAMP_DIGITS = 7
RANDOMNESS_DIGITS = 19
RANDOMNESS_BYTES = 12
BINARY_LENGTH = 17
MAX_TIMESTAMP = 36**TIMESTAMP_DIGITS - 1
MAX_RANDOMNESS = 2 ** (8 * RANDOMNESS_BYTES) - 1


class FixtureError(Exception):
    pass


def encode_base36(value: int, width: int) -> str:
    if value < 0:
        raise FixtureError(f"negative value {value}")
    digits = []
    for _ in range(width):
        value, remainder = divmod(value, 36)
        digits.append(ALPHABET[remainder])
    if value != 0:
        raise FixtureError(f"value does not fit in {width} base36 digits")
    return "".join(reversed(digits))


def decode_base36(text: str) -> int:
    value = 0
    for character in text:
        index = ALPHABET.find(character)
        if index < 0:
            raise FixtureError(f"non-canonical character {character!r} in {text!r}")
        value = value * 36 + index
    return value


def parse_hex(text: str, expected_bytes: int, label: str) -> bytes:
    if not isinstance(text, str) or len(text) != expected_bytes * 2:
        raise FixtureError(f"{label}: expected {expected_bytes * 2} hex characters")
    try:
        data = bytes.fromhex(text)
    except ValueError as error:
        raise FixtureError(f"{label}: invalid hex ({error})") from error
    if data.hex() != text:
        raise FixtureError(f"{label}: hex is not canonical lowercase")
    return data


def format_bytes(data: bytes) -> str:
    return ", ".join(f"0x{byte:02x}" for byte in data)


def load_vectors(path: pathlib.Path) -> dict:
    # Read the raw bytes once: the recorded SHA-256 must describe exactly the
    # bytes that were parsed, and line endings must not depend on the platform.
    try:
        raw = path.read_bytes()
    except OSError as error:
        raise FixtureError(f"cannot read {path}: {error}") from error
    if b"\r" in raw:
        raise FixtureError(
            f"{path} contains CR bytes; conformance fixtures must be stored "
            "with LF line endings (see .gitattributes)")
    try:
        document = json.loads(raw.decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        raise FixtureError(f"cannot decode {path}: {error}") from error

    for key in ("meta", "valid", "invalid"):
        if key not in document:
            raise FixtureError(f"missing top-level key {key!r}")

    meta = document["meta"]
    if meta.get("spec_revision") != "1.0.4":
        raise FixtureError("unexpected specification revision")
    if meta.get("string_length") != STRING_LENGTH:
        raise FixtureError("unexpected string length in meta")
    if meta.get("timestamp", {}).get("max") != MAX_TIMESTAMP:
        raise FixtureError("unexpected timestamp maximum in meta")
    if meta.get("entropy", {}).get("bytes") != RANDOMNESS_BYTES:
        raise FixtureError("unexpected entropy size in meta")
    if meta.get("binary", {}).get("bytes") != BINARY_LENGTH:
        raise FixtureError("unexpected binary size in meta")

    valid = []
    for vector in document["valid"]:
        for key in ("name", "timestamp", "entropy_hex", "id", "bin_hex"):
            if key not in vector:
                raise FixtureError(f"valid vector missing {key!r}: {vector}")
        timestamp = vector["timestamp"]
        if not isinstance(timestamp, int) or not 0 <= timestamp <= MAX_TIMESTAMP:
            raise FixtureError(f"{vector['name']}: timestamp out of range")
        entropy = parse_hex(vector["entropy_hex"], RANDOMNESS_BYTES,
                            f"{vector['name']} entropy_hex")
        randomness = int.from_bytes(entropy, "big")
        expected_id = (encode_base36(timestamp, TIMESTAMP_DIGITS) +
                       encode_base36(randomness, RANDOMNESS_DIGITS))
        expected_binary = timestamp.to_bytes(5, "big") + entropy
        if vector["id"] != expected_id:
            raise FixtureError(
                f"{vector['name']}: id {vector['id']!r} != derived {expected_id!r}")
        if vector["bin_hex"] != expected_binary.hex():
            raise FixtureError(
                f"{vector['name']}: bin_hex {vector['bin_hex']!r} != derived "
                f"{expected_binary.hex()!r}")
        if len(expected_id) != STRING_LENGTH:
            raise FixtureError(f"{vector['name']}: derived id has wrong length")
        if decode_base36(expected_id[:TIMESTAMP_DIGITS]) != timestamp:
            raise FixtureError(f"{vector['name']}: timestamp does not round-trip")
        if decode_base36(expected_id[TIMESTAMP_DIGITS:]) != randomness:
            raise FixtureError(f"{vector['name']}: randomness does not round-trip")
        valid.append({
            "name": vector["name"],
            "timestamp": timestamp,
            "entropy": entropy,
            "id": expected_id,
            "binary": expected_binary,
        })

    invalid_strings = []
    for vector in document["invalid"].get("string", []):
        name = vector.get("name", "<unnamed>")
        identifier = vector.get("id")
        if not isinstance(identifier, str):
            raise FixtureError(f"invalid string vector {name}: missing id")
        if len(identifier) != STRING_LENGTH:
            reason = "PIKAID_ERR_LENGTH"
        elif any(character not in ALPHABET for character in identifier):
            reason = "PIKAID_ERR_CHARACTER"
        else:
            randomness = decode_base36(identifier[TIMESTAMP_DIGITS:])
            if randomness <= MAX_RANDOMNESS:
                raise FixtureError(
                    f"invalid string vector {name} is actually valid")
            reason = "PIKAID_ERR_RANDOMNESS"
        invalid_strings.append({
            "name": name,
            "id": identifier,
            "reason": reason,
        })

    invalid_binaries = []
    for vector in document["invalid"].get("binary", []):
        name = vector.get("name", "<unnamed>")
        raw_hex = vector.get("bin_hex")
        if not isinstance(raw_hex, str) or len(raw_hex) % 2 != 0:
            raise FixtureError(f"invalid binary vector {name}: invalid bin_hex")
        data = bytes.fromhex(raw_hex)
        if len(data) != BINARY_LENGTH:
            reason = "PIKAID_ERR_LENGTH"
        else:
            timestamp = int.from_bytes(data[:5], "big")
            if timestamp <= MAX_TIMESTAMP:
                raise FixtureError(
                    f"invalid binary vector {name} is actually valid")
            reason = "PIKAID_ERR_TIMESTAMP"
        invalid_binaries.append({
            "name": name,
            "binary": data,
            "reason": reason,
        })

    return {
        "source_sha256": hashlib.sha256(raw).hexdigest(),
        "valid": valid,
        "invalid_strings": invalid_strings,
        "invalid_binaries": invalid_binaries,
    }


def render_header(vectors: dict) -> str:
    lines = [
        "/*",
        " * Generated by tests/vectors/generate_vectors.py from the official",
        " * pikaid v1 conformance vectors (pikaid-php v1.2.0). Do not edit.",
        " *",
        f" * Source SHA-256: {vectors['source_sha256']}",
        " * Specification revision: 1.0.4 (identifier format is unversioned)",
        " * SPDX-License-Identifier: MIT",
        " */",
        "",
        "#ifndef PIKAID_TEST_VECTORS_H_INCLUDED",
        "#define PIKAID_TEST_VECTORS_H_INCLUDED",
        "",
        "#include <stddef.h>",
        "#include <stdint.h>",
        "",
        "#include \"pikaid.h\"",
        "",
        "#define PIKAID_VECTOR_SPEC_REVISION \"1.0.4\"",
        f"#define PIKAID_VECTOR_SOURCE_SHA256 \"{vectors['source_sha256']}\"",
        "",
        "struct pikaid_valid_vector {",
        "    const char *name;",
        "    uint64_t timestamp;",
        "    uint8_t entropy[12];",
        "    const char *id;",
        "    uint8_t binary[17];",
        "};",
        "",
        "struct pikaid_invalid_string_vector {",
        "    const char *name;",
        "    const char *id;",
        "    size_t length;",
        "    pikaid_status expected;",
        "};",
        "",
        "struct pikaid_invalid_binary_vector {",
        "    const char *name;",
        "    uint8_t binary[18];",
        "    size_t length;",
        "    pikaid_status expected;",
        "};",
        "",
        "static const struct pikaid_valid_vector pikaid_valid_vectors[] = {",
    ]

    for vector in vectors["valid"]:
        lines.extend([
            "    {",
            f"        \"{vector['name']}\",",
            f"        UINT64_C({vector['timestamp']}),",
            f"        {{{format_bytes(vector['entropy'])}}},",
            f"        \"{vector['id']}\",",
            f"        {{{format_bytes(vector['binary'])}}}",
            "    },",
        ])
    lines.extend([
        "};",
        "",
        "static const size_t pikaid_valid_vector_count =",
        "    sizeof(pikaid_valid_vectors) / sizeof(pikaid_valid_vectors[0]);",
        "",
        "static const struct pikaid_invalid_string_vector "
        "pikaid_invalid_string_vectors[] = {",
    ])
    for vector in vectors["invalid_strings"]:
        escaped = vector["id"].replace("\\", "\\\\").replace("\"", "\\\"")
        lines.extend([
            "    {",
            f"        \"{vector['name']}\",",
            f"        \"{escaped}\",",
            f"        {len(vector['id'])},",
            f"        {vector['reason']}",
            "    },",
        ])
    lines.extend([
        "};",
        "",
        "static const size_t pikaid_invalid_string_vector_count =",
        "    sizeof(pikaid_invalid_string_vectors) /",
        "    sizeof(pikaid_invalid_string_vectors[0]);",
        "",
        "static const struct pikaid_invalid_binary_vector "
        "pikaid_invalid_binary_vectors[] = {",
    ])
    for vector in vectors["invalid_binaries"]:
        padded = vector["binary"] + bytes(18 - len(vector["binary"]))
        lines.extend([
            "    {",
            f"        \"{vector['name']}\",",
            f"        {{{format_bytes(padded)}}},",
            f"        {len(vector['binary'])},",
            f"        {vector['reason']}",
            "    },",
        ])
    lines.extend([
        "};",
        "",
        "static const size_t pikaid_invalid_binary_vector_count =",
        "    sizeof(pikaid_invalid_binary_vectors) /",
        "    sizeof(pikaid_invalid_binary_vectors[0]);",
        "",
        "#endif /* PIKAID_TEST_VECTORS_H_INCLUDED */",
        "",
    ])
    return "\n".join(lines)


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--json", required=True, type=pathlib.Path)
    parser.add_argument("--output", required=True, type=pathlib.Path)
    parser.add_argument("--check", action="store_true",
                        help="compare with the output file instead of writing")
    arguments = parser.parse_args(argv)

    try:
        vectors = load_vectors(arguments.json)
    except FixtureError as error:
        print(f"generate_vectors: {error}", file=sys.stderr)
        return 2

    rendered = render_header(vectors)
    encoded = rendered.encode("utf-8")
    if arguments.check:
        try:
            existing = arguments.output.read_bytes()
        except OSError as error:
            print(f"generate_vectors: {error}", file=sys.stderr)
            return 1
        if b"\r" in existing:
            print(f"generate_vectors: {arguments.output} contains CR bytes; "
                  "generated files must use LF line endings "
                  "(see .gitattributes)", file=sys.stderr)
            return 1
        if existing != encoded:
            print(f"generate_vectors: {arguments.output} is stale; "
                  "regenerate it from the official JSON", file=sys.stderr)
            return 1
        print(f"generate_vectors: {arguments.output} matches "
              f"{arguments.json} ({len(vectors['valid'])} valid, "
              f"{len(vectors['invalid_strings'])} invalid string, "
              f"{len(vectors['invalid_binaries'])} invalid binary vectors)")
        return 0

    arguments.output.parent.mkdir(parents=True, exist_ok=True)
    arguments.output.write_bytes(encoded)
    print(f"generate_vectors: wrote {arguments.output}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
