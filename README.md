<p align="center">
  <img src="https://raw.githubusercontent.com/pikaid/pikaid-specs/v1.0.4/logo.png" alt="pikaid" width="300" />
</p>

<p align="center">
  <strong>Small · Sortable · Secure ID generator</strong>
</p>

# pikaid — Official C Implementation

A C99 implementation of [pikaid specification revision 1.0.4](https://github.com/pikaid/pikaid-specs/tree/v1.0.4):
a compact, chronologically sortable identifier for databases, APIs and
distributed systems.

**Status:** the specification surface is implemented and tested; no release or
tag has been published. The library version is `1.0.0` and the package is
prepared for a `v1.0.0` tag once publication prerequisites are verified (see
[Versioning and compatibility](#versioning-and-compatibility)).

## The format

A pikaid is exactly **26 lowercase Base36 characters**:

```text
[ 7 characters: unsigned Unix seconds ][ 19 characters: 96 random bits ]
```

Its binary form is exactly **17 bytes**:

```text
[ 5 bytes: unsigned Unix seconds, big-endian ][ 12 bytes: randomness ]
```

The format is immutable and unversioned. This library never changes the wire
format and performs no normalization, truncation or locale-dependent
conversion.

Official known-answer vectors exercised by the test suite:

| Timestamp | Randomness | String form | Binary form |
| ---: | --- | --- | --- |
| `0` | all zero | `00000000000000000000000000` | `0000000000000000000000000000000000` |
| `1700000000` | `00112233445566778899aabb` | `0s44we8002loz3po6mcf6vsn23` | `006553f10000112233445566778899aabb` |
| `78364164095` | all `ff` | `zzzzzzz7oiylpimjg5u2ca1ypr` | `123ede3fffffffffffffffffffffffffff` |

`78364164095` (`36^7 - 1`) is the largest valid timestamp. The 96-bit
randomness field is bounded by `2^96 - 1`; larger suffixes are well formed but
not valid pikaids.

## Requirements

- A C99 compiler (the public header includes `<stdbool.h>`, `<stddef.h>` and
  `<stdint.h>`).
- CMake 3.16 or newer to build with the provided build system.
- A platform CSPRNG: `getrandom(2)` on Linux, `getentropy(2)` on macOS,
  FreeBSD and OpenBSD, or `BCryptGenRandom` on Windows. Other platforms
  compile but generation fails closed with `PIKAID_ERR_ENTROPY`.

There are no third-party runtime dependencies and the library never allocates
memory.

## Build and test

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
(cd build && ctest --output-on-failure)
```

Install it into a prefix:

```sh
cmake --install build --prefix /usr/local
```

This installs the header, the library, a CMake package (`pikaidConfig.cmake`,
`pikaid::pikaid`) and a `pkg-config` file.

### Build options

| Option | Default | Effect |
| --- | --- | --- |
| `BUILD_SHARED_LIBS` | `OFF` | Build a shared library instead of a static one |
| `PIKAID_BUILD_TESTS` | `ON` | Build the CTest suite |
| `PIKAID_BUILD_EXAMPLES` | `ON` | Build `examples/quickstart.c` |
| `PIKAID_WARNINGS_AS_ERRORS` | `OFF` | Turn compiler warnings into errors |
| `PIKAID_ENABLE_SANITIZERS` | `OFF` | Build with AddressSanitizer and UndefinedBehaviorSanitizer |
| `PIKAID_ENABLE_COVERAGE` | `OFF` | Build with statement coverage instrumentation |

## Use it

```c
#include <pikaid.h>

#include <stdio.h>

int main(void)
{
    char id[PIKAID_STRING_SIZE];
    pikaid_t decoded;
    uint64_t timestamp;

    if (pikaid_generate(id) != PIKAID_OK) {
        return 1; /* clock or CSPRNG failure: no identifier was produced */
    }
    if (pikaid_parse(id, PIKAID_STRING_LENGTH, &decoded) != PIKAID_OK) {
        return 1;
    }
    if (pikaid_get_timestamp(&decoded, &timestamp) != PIKAID_OK) {
        return 1;
    }

    (void)printf("%s (%llu)\n", id, (unsigned long long)timestamp);
    return 0;
}
```

With CMake:

```cmake
find_package(pikaid 1.0 CONFIG REQUIRED)
target_link_libraries(your_target PRIVATE pikaid::pikaid)
```

With `pkg-config`:

```sh
cc app.c $(pkg-config --cflags --libs pikaid)
```

A shared build publishes the `PIKAID_SHARED` compile definition through both
CMake and `pkg-config`; this matters on Windows, where consumers must see
`__declspec(dllimport)`.

`examples/quickstart.c` exercises generation, validation, parsing, conversion,
ordering and rejection end to end:

```sh
./build/pikaid_quickstart
```

### Versioning and compatibility

- The **identifier format** is immutable and unversioned. A future library
  release will never change the 26-character or 17-byte representation.
- The **library** follows semantic versioning. `1.0.0` is the first release;
  the CMake package requires the same major version (`find_package(pikaid 1.0
  CONFIG REQUIRED)`), and the shared library uses `SOVERSION 1`
  (`libpikaid.so.1`).
- The **public C API** is the set of declarations in `include/pikaid.h`.
  Internal entropy and clock helpers are not exported and are not part of the
  ABI.
- A `v1.0.0` Git tag and GitHub release are not part of this tree; they are
  created only after the publication prerequisites in
  [`RELEASING.md`](RELEASING.md) have been verified.

## API

Every fallible function returns a `pikaid_status`. `PIKAID_OK` is `0` and every
failure is a distinct positive value. `pikaid_status_string` returns a stable
human-readable label for any status.

### Constants

| Name | Value | Meaning |
| --- | ---: | --- |
| `PIKAID_STRING_LENGTH` | `26` | Canonical string length |
| `PIKAID_STRING_SIZE` | `27` | String buffer size including `NUL` |
| `PIKAID_BINARY_LENGTH` | `17` | Canonical binary length |
| `PIKAID_RANDOMNESS_LENGTH` | `12` | Random bytes |
| `PIKAID_TIMESTAMP_DIGITS` | `7` | Base36 timestamp digits |
| `PIKAID_RANDOMNESS_DIGITS` | `19` | Base36 randomness digits |
| `PIKAID_TIMESTAMP_BYTES` | `5` | Big-endian timestamp bytes |
| `PIKAID_TIMESTAMP_MAX` | `78364164095` | Largest valid timestamp |

### Functions

| Function | Purpose |
| --- | --- |
| `pikaid_generate` | Generate from the current time and a CSPRNG |
| `pikaid_generate_at` | Generate with an explicit timestamp and a CSPRNG |
| `pikaid_generate_binary` | Generate 17 bytes from the current time |
| `pikaid_generate_binary_at` | Generate 17 bytes with an explicit timestamp |
| `pikaid_encode` | Encode timestamp + randomness into a string |
| `pikaid_encode_binary` | Encode timestamp + randomness into 17 bytes |
| `pikaid_parse` | Strictly parse 26 input bytes |
| `pikaid_parse_cstr` | Strictly parse a NUL-terminated string |
| `pikaid_decode_binary` | Strictly decode 17 input bytes |
| `pikaid_format` | Format decoded components as a string |
| `pikaid_to_binary` | Convert decoded components to 17 bytes |
| `pikaid_compare` | Compare two decoded values |
| `pikaid_compare_strings` | Validate and compare two strings |
| `pikaid_is_valid` | Full semantic validation |
| `pikaid_is_well_formed` | Shape-only check (length and alphabet) |
| `pikaid_get_timestamp` | Read the timestamp |
| `pikaid_get_randomness` | Copy the 12 randomness bytes |
| `pikaid_status_string` | Describe a status code |

### Buffer and error contract

- String outputs require `PIKAID_STRING_SIZE` bytes; binary outputs require
  `PIKAID_BINARY_LENGTH` bytes. Buffers are caller-owned.
- On failure, string-producing functions set `out[0] = '\0'` and
  binary-producing functions zero the 17 bytes when the output pointer is
  valid. A failed call never leaves a partial identifier behind. Because a
  zeroed buffer is itself the syntactically valid all-zero pikaid, always
  branch on the returned status rather than on the buffer contents.
- Input and output buffers may overlap. Every function consumes all input bytes
  before writing output, and the encoders stage the randomness locally, so
  `pikaid_encode_binary(ts, buf, buf)` is well defined.
- `pikaid_parse`, `pikaid_parse_cstr` and `pikaid_decode_binary` leave their
  `pikaid_t` output untouched on failure.
- `pikaid_get_timestamp` and `pikaid_get_randomness` leave their outputs
  untouched on failure.
- `pikaid_is_well_formed` is **not** full validation. For example, nineteen
  `z` characters are well formed but exceed `2^96 - 1` and are rejected by
  `pikaid_is_valid`.
- There is no global state. Distinct output buffers may be used concurrently.

### Ordering

- Valid pikaids sort chronologically by their 7-character timestamp prefix.
- Within the same second, order is determined by the randomness field. It is
  **not** generation order and **not** monotonic.
- ASCII lexicographic order of the string form agrees with unsigned bytewise
  order of the 17-byte binary form for all valid pikaids.
- `pikaid_compare_strings` validates both operands first; `memcmp` on two
  strings already known to be valid gives the same result.

### Security notes

- Generation consumes **96 bits** from the operating system CSPRNG. Failure of
  the CSPRNG or clock is reported, never replaced by `rand()`, a counter, a
  hash of predictable input or a weaker fallback.
- A pikaid is an identifier, not a secret. It is not an authentication token
  and makes no confidentiality claim.
- The format guarantees neither uniqueness nor monotonicity. Collision risk is
  bounded by the 96-bit randomness field and the birthday bound; deployments
  that need stronger guarantees must enforce them at the storage layer.
- Timestamps come from the caller's clock. Clock skew, clock adjustments and
  backfills affect chronological ordering between hosts.
- There is no sub-second precision and no ordering guarantee beyond whole
  seconds plus the randomness tie-break.

## Platform support

| Platform | Entropy backend | Verification |
| --- | --- | --- |
| Linux | `getrandom(2)` | GitHub Actions matrix: GCC and Clang |
| macOS 10.12+ | `getentropy(2)` from `<sys/random.h>` | Local macOS arm64 run and GitHub Actions matrix |
| Windows 10+ | `BCryptGenRandom` | GitHub Actions matrix: MSVC |
| FreeBSD / OpenBSD | `getentropy(2)` from `<unistd.h>` | Backend present; not exercised by the verification performed for this tree |
| NetBSD / DragonFly BSD | None: generation fails closed | Compiles via the stub; returns `PIKAID_ERR_ENTROPY` (no backend claim) |
| Other platforms | None: generation fails closed | Compiles, returns `PIKAID_ERR_ENTROPY` |

The authoring machine for this tree was macOS arm64 with Apple Clang. Linux and
Windows entries above refer to the configured CI matrix, not to local runs.

## Testing

The suite includes official vectors, independently derived known-answer tests,
malformed and overflow cases, boundary timestamps, randomized round trips,
cross-representation comparisons, generation sanity checks and fault injection
for entropy and clock failures.

Local verification on macOS arm64 executed **55,690 assertions** across eleven
test programs with **zero failures**, plus AddressSanitizer and
UndefinedBehaviorSanitizer runs (14/14 CTests each). `lcov` 2.6 reported
**100% (249/249) line coverage of `src/pikaid.c`** and **98.52% (267/271)
across `src/`** after the fault-injection executables were included in the
measurement; the only uncovered lines are platform backends for operating
systems not exercised on macOS. The official shared conformance suite is run
through a test-only JSON-Lines adapter at a pinned commit; only its automated
checks are claimed, and its two CSPRNG security audits remain a manual
obligation. See [`tests/README.md`](tests/README.md) for provenance, oracle
independence, skip behaviour and the conformance adapter.

```sh
(cd build && ctest --output-on-failure)
```

The CI workflow additionally runs a 90% line-coverage gate with `lcov`,
sanitizer builds, a shared-library export check, installed CMake and
`pkg-config` consumers against the documented `--prefix` install flow, a
repository/source-archive hygiene check that `.ai/` never ships, the pinned
official conformance suite with a mutation control, and a gating
`clang-tidy` pass on the library sources.

## Specification and license

- Specification: [pikaid specification revision 1.0.4](https://github.com/pikaid/pikaid-specs/tree/v1.0.4)
- License: [MIT](LICENSE)
