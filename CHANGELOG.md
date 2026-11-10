# Changelog

All notable changes to this project are documented here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and the library uses
[semantic versioning](https://semver.org/spec/v2.0.0.html). The identifier
format itself is immutable and unversioned.

## [Unreleased]

Nothing yet.

## [1.0.0] — unreleased

Initial implementation of the [pikaid specification revision 1.0.4](https://github.com/pikaid/pikaid-specs/tree/v1.0.4).

### Added

- C99 library with no allocation and no global state.
- Canonical 26-character lowercase Base36 string encoding and 17-byte binary
  encoding, with strict parsing, validation and conversion.
- Cryptographic generation from the platform CSPRNG (`getrandom(2)`,
  `getentropy(2)` or `BCryptGenRandom`) with fail-closed error reporting.
- CMake package (`pikaid::pikaid`) and relocatable `pkg-config` metadata.
- Official known-answer vectors, boundary, malformed-input, randomized
  round-trip and entropy/clock fault-injection tests.
- CI matrix for Ubuntu GCC, Ubuntu Clang, macOS Clang and Windows MSVC, plus
  sanitizer, coverage, shared-library export and installed-consumer jobs.

### Notes

- No `v1.0.0` tag or GitHub release has been created from this tree. The
  version is prepared for release once publication prerequisites are verified.
