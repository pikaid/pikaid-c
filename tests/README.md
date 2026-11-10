# pikaid test suite

The suite proves conformance against the
[pikaid specification revision 1.0.4](https://github.com/pikaid/pikaid-specs/tree/v1.0.4)
using official vectors, independently derived oracles, malformed input,
boundary values, randomized properties and fault injection.

## Run it

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DPIKAID_WARNINGS_AS_ERRORS=ON
cmake --build build
(cd build && ctest --output-on-failure)
```

One test with its assertions:

```sh
(cd build && ctest -R pikaid_test_vectors -V)
# or run the executable directly
./build/tests/pikaid_test_vectors
```

Every executable prints `<name>: <n> checks, <n> failures` and exits non-zero
on any failure.

## What each program covers

| Program | Checks | Coverage |
| --- | ---: | --- |
| `test_vectors` | 172 | Every official valid/invalid vector, both forms, plus the schema's boundary values |
| `test_encoding` | 294 | Base36 division, zero padding, width, timestamp and randomness bounds, output clearing |
| `test_decoding` | 2,632 | Strict alphabet, length, case, non-ASCII and zero-width Unicode, overflow, lossy input, untouched outputs |
| `test_api` | 245 | NULL arguments, buffer aliasing, status labels, contract details |
| `test_generate` | 748 | Generation sanity, explicit timestamps, binary/string agreement, clock range |
| `test_properties` | 51,462 | 2,000 randomized round trips, ascending/descending ordering agreement, boundaries, within-second non-monotonicity, buffer aliasing |
| `test_injection_fixed` | 36 | Deterministic entropy/clock doubles and known-answer output |
| `test_injection_entropy_failure` | 42 | Entropy failure propagation and failed-output clearing |
| `test_injection_clock_failure` | 22 | Clock read failure propagation |
| `test_injection_clock_out_of_range` | 22 | Clock above `PIKAID_TIMESTAMP_MAX` |
| `test_internal` | 15 | Real `getrandom`/`getentropy`/`BCryptGenRandom` and `time()` paths |

The local macOS arm64 run executes **55,690 checks with zero failures**.

## Official shared conformance suite

The native suite above is complemented by the shared
[pikaid conformance suite](https://github.com/pikaid/pikaid-specs/tree/v1.0.4)
at pinned commit `059780cc0e5b113443812720d95c630649bf3aa3`
(package revision `1.0.0`, specification revision `1.0.4`). The bridge is
`conformance/pikaid_conformance_adapter.c`: a test-only protocol-v1 JSON-Lines
adapter that is **not installed** and contains no codec, validator, clock or
randomness logic of its own. Every answer comes from the public `pikaid` API.

Run it against a clean suite checkout:

```sh
python3 tests/conformance/run_official_suite.py \
  --suite-dir /path/to/pikaid-specs \
  --adapter build/tests/pikaid_conformance_adapter \
  --mutant-adapter build/tests/pikaid_conformance_adapter_mutant \
  --work-dir conformance-work
```

The orchestrator fails closed: it verifies the suite's Git commit, every
`conformance/SHA256SUMS` entry, the manifest status/revisions and fixture row
counts, then runs the official `verify_vectors.py`, its unit tests, the Node
oracle and `run_adapter.py --automated-only`. It also runs a deterministic
mutant adapter that corrupts one wire value and requires the official runner to
reject it with exit 1.

The official suite reports `AUTOMATED_PASS_AUDIT_NOT_PERFORMED` with exit 0:
1,100 fixture rows pass, while the two CSPRNG security audits
(`P-GENERATE-RNG-SOURCE`, `P-GENERATE-RNG-FAILURE`) remain a separate manual
obligation. Exit 3 from `run_adapter.py` without `--automated-only` means the
same: automated checks passed, human evidence is outstanding. Neither result is
a claim of full conformance certification.

## Fixture provenance

| File | Source | SHA-256 |
| --- | --- | --- |
| `pikaid-v1.json` | Official vector document, specification revision 1.0.4 | `b0c9dfdd063613424414fef5c9065fbd2f81d645d0ab42b34e27cde715613500` |
| `pikaid-v1.schema.json` | Official JSON Schema, specification revision 1.0.4 | `4441d4dbaa6c5450ae9cbfb2438c53bc0421bba2b7fcc9ef9b158e5d1de0759e` |
| `pikaid_vectors.h` | Generated from `pikaid-v1.json` | `90a1b749ebe70275de9ba41ae578331fd04d5ff8c489d2277267f45d02f45174` |

The normative specification README used during implementation hashed to
`cd122a7b854d301d1c5697d3a72975307d8f6d51fdc6e15ed334485968be474d`.

All fixture and generated files are stored with LF line endings, enforced by
`.gitattributes`. This matters because `generate_vectors.py` records the raw
SHA-256 of `pikaid-v1.json` and compares `pikaid_vectors.h` byte for byte; a
Windows checkout with `core.autocrlf=true` would otherwise change the hash and
report a false stale header. The generator reads and writes bytes directly and
rejects CR bytes in the fixture or the generated header.

The fixture document is copied byte for byte; it is not edited to match the
implementation. Regenerate and verify the C header with:

```sh
python3 tests/vectors/generate_vectors.py \
  --json tests/vectors/pikaid-v1.json \
  --output tests/vectors/pikaid_vectors.h
python3 tests/vectors/generate_vectors.py \
  --json tests/vectors/pikaid-v1.json \
  --output tests/vectors/pikaid_vectors.h --check
```

## Oracle independence

The oracles do not trust the C implementation:

- `generate_vectors.py` re-derives every string and binary value from the
  documented layout using Python arbitrary-precision integers. It never
  imports or executes the library. `--check` fails if the committed header
  differs from either the JSON or the independent derivation.
- `test_injection.c` hard-codes expected identifiers and binary values derived
  with Python integers from fixed timestamp and entropy inputs.
- `test_properties.c` hard-codes six same-second identifiers derived
  independently from the Base36 layout, covering `0`, `1`, carry boundaries
  (`0z` → `10`), the 96-bit midpoint and `2^96 - 1`.
- Randomized round trips compare string, binary and decoded representations
  against each other rather than against a second copy of the same codec.
- Ordering tests assert that `pikaid_compare`, `strcmp` over the string form and
  `memcmp` over the binary form always agree.

The randomized tests use a deterministic `xorshift64*` generator in
`test_common.c`; it exists only to make failures reproducible and is never part
of the library.

## Fault injection

`test_injection.c` is compiled four times. Each executable links
`src/pikaid.c` directly and supplies its own `pikaid_entropy_fill` and
`pikaid_now` definitions, selected by `PIKAID_TEST_MODE_*`:

| Mode | Behavior |
| --- | --- |
| `FIXED` | Fixed 12 bytes and fixed timestamp; exact known-answer output |
| `ENTROPY_FAILURE` | CSPRNG returns `PIKAID_ERR_ENTROPY` |
| `CLOCK_FAILURE` | Clock returns `PIKAID_ERR_CLOCK` |
| `CLOCK_OUT_OF_RANGE` | Clock returns `PIKAID_TIMESTAMP_MAX + 1` |

The doubles live only in the test executables. They are not compiled into the
installed library and add no public API.

## Skips, not false passes

- `pikaid_vectors_schema_validation` exits `77` when Python `jsonschema` is not
  installed, and CTest records a skip. Configure with
  `-DPIKAID_REQUIRE_SCHEMA_VALIDATION=ON` to fail at configure time instead of
  skipping; CI always sets this option after installing `jsonschema`, so the
  schema check cannot silently disappear from a required build. Install it with
  `python3 -m pip install jsonschema` to run the check locally.
- `pikaid_test_internal` exits `77` when the platform has no verified CSPRNG
  backend. Linux, macOS, FreeBSD, OpenBSD and Windows exercise the real system
  calls; NetBSD, DragonFly BSD and other systems use the fail-closed stub.
- Missing `python3` disables the two Python checks at configure time and prints
  a `NOT RUN` message; it never turns them into passes.

## Sanitizers

```sh
cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug \
  -DPIKAID_ENABLE_SANITIZERS=ON -DPIKAID_WARNINGS_AS_ERRORS=ON
cmake --build build-asan
(cd build-asan && ASAN_OPTIONS=strict_string_checks=1 \
  UBSAN_OPTIONS=halt_on_error=1 ctest --output-on-failure)
```

All tests pass with AddressSanitizer and UndefinedBehaviorSanitizer and no
diagnostics. Leak detection is enabled in the Linux CI job; Apple's sanitizer
runtime rejects `detect_leaks`, so it is not part of the macOS invocation.

## Coverage

```sh
cmake -S . -B build-cov -DCMAKE_BUILD_TYPE=Debug -DPIKAID_ENABLE_COVERAGE=ON
cmake --build build-cov
(cd build-cov && ctest --output-on-failure)
```

The local macOS measurement used `lcov` 2.6 over the whole coverage build, so
the fault-injection executables (which compile `src/pikaid.c` themselves) are
included in the union:

| Translation unit | Lines executed |
| --- | ---: |
| `src/pikaid.c` | 100% of 249 |
| `src/pikaid_entropy.c` | 81.82% of 11 |
| `src/pikaid_clock.c` | 81.82% of 11 |
| **`src/` total** | **98.52% of 271** |

The uncovered lines are defensive failure paths that the local OS does not
produce: the `time()` failure and pre-1970 returns in `src/pikaid_clock.c`
(lines 25 and 29) and the over-long request and `getentropy()` failure returns
in `src/pikaid_entropy.c` (lines 114 and 121). Their propagation is exercised
through the injection executables' test doubles. The Linux CI job compiles and
exercises its own backend and enforces a 90% line gate with `lcov` over the
library sources.

## Schema check

```sh
python3 tests/vectors/validate_schema.py \
  --schema tests/vectors/pikaid-v1.schema.json \
  --json tests/vectors/pikaid-v1.json
```

Add `--require` to make a missing `jsonschema` module an error instead of
exit `77`; `-DPIKAID_REQUIRE_SCHEMA_VALIDATION=ON` passes it through CTest.
