/*
 * pikaid — official C implementation
 *
 * Implements pikaid specification revision 1.0.4 (immutable, unversioned format):
 * https://github.com/pikaid/pikaid-specs/tree/v1.0.4
 *
 * A pikaid is 26 lowercase Base36 characters:
 *
 *   [ 7 characters: unsigned Unix seconds, Base36 ][ 19 characters: 96 bits, Base36 ]
 *
 * Its binary form is exactly 17 bytes:
 *
 *   [ 5 bytes: unsigned Unix seconds, big-endian ][ 12 bytes: randomness, unchanged ]
 *
 * The library never allocates memory. Every output buffer is caller-owned and
 * every function reports failure through a pikaid_status value. There is no
 * global state, so all functions are thread-safe as long as distinct output
 * buffers are used.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef PIKAID_H_INCLUDED
#define PIKAID_H_INCLUDED

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Export decoration.
 *
 * Static builds and non-Windows platforms need nothing here. A Windows DLL
 * build exports PIKAID_API symbols; CMake defines PIKAID_SHARED for both the
 * library and its consumers in that configuration.
 */
#if defined(_WIN32) && defined(PIKAID_SHARED)
#if defined(PIKAID_BUILDING_LIBRARY)
#define PIKAID_API __declspec(dllexport)
#else
#define PIKAID_API __declspec(dllimport)
#endif
#elif defined(__GNUC__) && (__GNUC__ >= 4)
#define PIKAID_API __attribute__((visibility("default")))
#else
#define PIKAID_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

/** Length of the canonical string form, excluding the NUL terminator. */
#define PIKAID_STRING_LENGTH 26

/** Required capacity for a NUL-terminated string output buffer. */
#define PIKAID_STRING_SIZE (PIKAID_STRING_LENGTH + 1)

/** Length of the canonical binary form. */
#define PIKAID_BINARY_LENGTH 17

/** Number of random bytes in the binary form. */
#define PIKAID_RANDOMNESS_LENGTH 12

/** Number of Base36 digits used for the timestamp. */
#define PIKAID_TIMESTAMP_DIGITS 7

/** Number of Base36 digits used for the randomness component. */
#define PIKAID_RANDOMNESS_DIGITS 19

/** Number of big-endian bytes used for the timestamp in binary form. */
#define PIKAID_TIMESTAMP_BYTES 5

/**
 * Largest representable timestamp: 36^7 - 1 = 78,364,164,095 seconds.
 *
 * This is 4453-04-05T15:21:35Z. Values above this limit are not valid pikaid
 * timestamps even though five binary bytes could hold larger numbers.
 */
#define PIKAID_TIMESTAMP_MAX UINT64_C(78364164095)

/**
 * Result codes returned by every fallible function.
 *
 * PIKAID_OK is zero; all failures are positive and distinguishable. Callers
 * must not treat a non-zero value as success.
 */
typedef enum pikaid_status {
    /** The operation completed successfully. */
    PIKAID_OK = 0,

    /** A required pointer argument was NULL. */
    PIKAID_ERR_NULL_ARGUMENT = 1,

    /** A string or binary input had the wrong length (26 or 17 expected). */
    PIKAID_ERR_LENGTH = 2,

    /** A string contained a character outside [0-9a-z]. */
    PIKAID_ERR_CHARACTER = 3,

    /** A timestamp was negative or greater than PIKAID_TIMESTAMP_MAX. */
    PIKAID_ERR_TIMESTAMP = 4,

    /** The 19-digit randomness component exceeded 2^96 - 1. */
    PIKAID_ERR_RANDOMNESS = 5,

    /** The operating system CSPRNG failed; no identifier was produced. */
    PIKAID_ERR_ENTROPY = 6,

    /** The system clock failed; no identifier was produced. */
    PIKAID_ERR_CLOCK = 7
} pikaid_status;

/**
 * Decoded pikaid components.
 *
 * Both fields are canonical: timestamp is the exact unsigned Unix second and
 * randomness is the original 12 entropy bytes, including leading zeros.
 * A value produced by this library is always valid; a value assembled by
 * hand should be validated through pikaid_format or pikaid_to_binary.
 */
typedef struct pikaid {
    /** Unsigned Unix seconds, 0..PIKAID_TIMESTAMP_MAX. */
    uint64_t timestamp;

    /** Exactly 12 bytes (96 bits) of randomness. */
    uint8_t randomness[PIKAID_RANDOMNESS_LENGTH];
} pikaid_t;

/*
 * Buffer and error conventions
 * ----------------------------
 *
 * - String output buffers must have room for PIKAID_STRING_SIZE bytes; the
 *   library writes 26 characters plus a NUL terminator.
 * - Binary output buffers must have room for PIKAID_BINARY_LENGTH bytes.
 * - On failure, functions that write a string set out[0] to '\0' when the
 *   output pointer is valid, and functions that write binary zero the buffer,
 *   so a failed call never leaves a partial identifier behind. A zeroed
 *   binary buffer is nevertheless the syntactically well-formed all-zero
 *   pikaid (timestamp 0), so callers must check the status: output contents
 *   are never a substitute for it.
 * - Input and output buffers may overlap. Every function reads all input bytes
 *   before writing any output byte, and the encoding functions stage the
 *   randomness locally, so calls such as pikaid_encode_binary(ts, buf, buf)
 *   are safe.
 * - pikaid_parse and pikaid_decode_binary leave *out untouched on failure.
 * - pikaid_get_timestamp and pikaid_get_randomness leave their output
 *   untouched on failure.
 * - No function reads or writes beyond the documented buffer length. Length
 *   arguments are always explicit; pikaid_parse never calls strlen.
 */

/**
 * Generate a pikaid using the current system time and 12 CSPRNG bytes.
 *
 * @param out Buffer of PIKAID_STRING_SIZE bytes; receives a NUL-terminated id.
 * @return PIKAID_OK, PIKAID_ERR_NULL_ARGUMENT, PIKAID_ERR_TIMESTAMP when the
 *         clock is outside 0..PIKAID_TIMESTAMP_MAX (including before 1970),
 *         PIKAID_ERR_CLOCK when the clock cannot be read, or
 *         PIKAID_ERR_ENTROPY when the CSPRNG fails.
 */
PIKAID_API pikaid_status pikaid_generate(char out[PIKAID_STRING_SIZE]);

/**
 * Generate a pikaid with an explicit timestamp and 12 CSPRNG bytes.
 *
 * The timestamp is validated before any entropy is consumed. Entropy is still
 * cryptographic; this function exists for imports, tests and backfills, not
 * for replacing secure generation.
 *
 * @param timestamp Unsigned Unix seconds in 0..PIKAID_TIMESTAMP_MAX.
 * @param out Buffer of PIKAID_STRING_SIZE bytes.
 * @return PIKAID_OK, PIKAID_ERR_NULL_ARGUMENT, PIKAID_ERR_TIMESTAMP or
 *         PIKAID_ERR_ENTROPY.
 */
PIKAID_API pikaid_status pikaid_generate_at(uint64_t timestamp,
                                 char out[PIKAID_STRING_SIZE]);

/**
 * Generate a pikaid directly in 17-byte binary form using the current time.
 *
 * @param out Buffer of PIKAID_BINARY_LENGTH bytes.
 * @return The same statuses as pikaid_generate.
 */
PIKAID_API pikaid_status pikaid_generate_binary(uint8_t out[PIKAID_BINARY_LENGTH]);

/**
 * Generate a pikaid in 17-byte binary form with an explicit timestamp.
 *
 * @param timestamp Unsigned Unix seconds in 0..PIKAID_TIMESTAMP_MAX.
 * @param out Buffer of PIKAID_BINARY_LENGTH bytes.
 * @return The same statuses as pikaid_generate_at.
 */
PIKAID_API pikaid_status pikaid_generate_binary_at(uint64_t timestamp,
                                        uint8_t out[PIKAID_BINARY_LENGTH]);

/**
 * Encode explicit components into the 26-character string form.
 *
 * @param timestamp Unsigned Unix seconds in 0..PIKAID_TIMESTAMP_MAX.
 * @param randomness Exactly PIKAID_RANDOMNESS_LENGTH bytes.
 * @param out Buffer of PIKAID_STRING_SIZE bytes.
 * @return PIKAID_OK, PIKAID_ERR_NULL_ARGUMENT or PIKAID_ERR_TIMESTAMP.
 */
PIKAID_API pikaid_status pikaid_encode(uint64_t timestamp,
                            const uint8_t randomness[PIKAID_RANDOMNESS_LENGTH],
                            char out[PIKAID_STRING_SIZE]);

/**
 * Encode explicit components into the 17-byte binary form.
 *
 * @param timestamp Unsigned Unix seconds in 0..PIKAID_TIMESTAMP_MAX.
 * @param randomness Exactly PIKAID_RANDOMNESS_LENGTH bytes.
 * @param out Buffer of PIKAID_BINARY_LENGTH bytes.
 * @return PIKAID_OK, PIKAID_ERR_NULL_ARGUMENT or PIKAID_ERR_TIMESTAMP.
 */
PIKAID_API pikaid_status pikaid_encode_binary(
    uint64_t timestamp, const uint8_t randomness[PIKAID_RANDOMNESS_LENGTH],
    uint8_t out[PIKAID_BINARY_LENGTH]);

/**
 * Strictly parse a 26-character pikaid.
 *
 * Rejects wrong lengths, any character outside [0-9a-z] (including uppercase,
 * whitespace, separators, non-ASCII bytes and embedded NULs), and randomness
 * values above 2^96 - 1. The input is never normalized or truncated.
 *
 * @param text Input bytes; need not be NUL-terminated.
 * @param length Exact input length; must be PIKAID_STRING_LENGTH.
 * @param out Decoded components; untouched on failure.
 * @return PIKAID_OK, PIKAID_ERR_NULL_ARGUMENT, PIKAID_ERR_LENGTH,
 *         PIKAID_ERR_CHARACTER or PIKAID_ERR_RANDOMNESS.
 */
PIKAID_API pikaid_status pikaid_parse(const char *text, size_t length, pikaid_t *out);

/**
 * Strictly parse a NUL-terminated pikaid string.
 *
 * At most PIKAID_STRING_SIZE bytes are examined, so oversized input is
 * rejected without an unbounded scan. The pointer must reference a valid C
 * string (NUL-terminated within the readable allocation).
 *
 * @param text NUL-terminated input string.
 * @param out Decoded components; untouched on failure.
 * @return The same statuses as pikaid_parse.
 */
PIKAID_API pikaid_status pikaid_parse_cstr(const char *text, pikaid_t *out);

/**
 * Strictly decode the 17-byte binary form.
 *
 * Rejects any length other than PIKAID_BINARY_LENGTH and timestamps above
 * PIKAID_TIMESTAMP_MAX. The 12 randomness bytes are preserved exactly.
 *
 * @param binary Input bytes; need not be aligned.
 * @param length Exact input length; must be PIKAID_BINARY_LENGTH.
 * @param out Decoded components; untouched on failure.
 * @return PIKAID_OK, PIKAID_ERR_NULL_ARGUMENT, PIKAID_ERR_LENGTH or
 *         PIKAID_ERR_TIMESTAMP.
 */
PIKAID_API pikaid_status pikaid_decode_binary(const uint8_t *binary, size_t length,
                                   pikaid_t *out);

/**
 * Format decoded components as a 26-character pikaid.
 *
 * The timestamp is range-checked. Randomness is always exactly 96 bits by
 * construction of pikaid_t.
 *
 * @param id Decoded components; must not be NULL.
 * @param out Buffer of PIKAID_STRING_SIZE bytes.
 * @return PIKAID_OK, PIKAID_ERR_NULL_ARGUMENT or PIKAID_ERR_TIMESTAMP.
 */
PIKAID_API pikaid_status pikaid_format(const pikaid_t *id, char out[PIKAID_STRING_SIZE]);

/**
 * Convert decoded components to the 17-byte binary form.
 *
 * @param id Decoded components; must not be NULL.
 * @param out Buffer of PIKAID_BINARY_LENGTH bytes.
 * @return PIKAID_OK, PIKAID_ERR_NULL_ARGUMENT or PIKAID_ERR_TIMESTAMP.
 */
PIKAID_API pikaid_status pikaid_to_binary(const pikaid_t *id,
                               uint8_t out[PIKAID_BINARY_LENGTH]);

/**
 * Compare two decoded pikaid values.
 *
 * The order is timestamp first, then unsigned bytewise randomness order. This
 * matches ASCII-compatible lexicographic order of the string form and
 * unsigned bytewise order of the binary form for all valid values.
 *
 * Values with different timestamps sort chronologically. Values with the same
 * timestamp sort by randomness, which is not generation order.
 *
 * @param a First value; must not be NULL.
 * @param b Second value; must not be NULL.
 * @param result Receives -1, 0 or 1 when the function returns PIKAID_OK;
 *               untouched on failure.
 * @return PIKAID_OK, PIKAID_ERR_NULL_ARGUMENT or PIKAID_ERR_TIMESTAMP when a
 *         struct contains an out-of-range timestamp.
 */
PIKAID_API pikaid_status pikaid_compare(const pikaid_t *a, const pikaid_t *b, int *result);

/**
 * Compare two pikaid strings after full validation.
 *
 * Because both operands are validated, the result always agrees with
 * memcmp() over the 26 bytes and with pikaid_compare on the decoded values.
 *
 * @param a First string; must not be NULL.
 * @param b Second string; must not be NULL.
 * @param result Receives -1, 0 or 1; untouched on failure.
 * @return PIKAID_OK or any status produced by pikaid_parse (for a first).
 */
PIKAID_API pikaid_status pikaid_compare_strings(const char *a, const char *b, int *result);

/**
 * Return true only if the input is a fully valid pikaid.
 *
 * This performs semantic validation, including the 96-bit randomness limit.
 * NULL input returns false.
 */
PIKAID_API bool pikaid_is_valid(const char *text, size_t length);

/**
 * Return true if the input has the right length and alphabet only.
 *
 * This is a shape check, not full validation: for example, a 19-character
 * suffix of 'z' is well formed but not a valid pikaid because it exceeds
 * 2^96 - 1. Never describe a true result as full validity.
 */
PIKAID_API bool pikaid_is_well_formed(const char *text, size_t length);

/**
 * Extract the timestamp from decoded components.
 *
 * @param id Decoded components; must not be NULL.
 * @param timestamp Receives the timestamp; untouched on failure.
 * @return PIKAID_OK, PIKAID_ERR_NULL_ARGUMENT or PIKAID_ERR_TIMESTAMP.
 */
PIKAID_API pikaid_status pikaid_get_timestamp(const pikaid_t *id, uint64_t *timestamp);

/**
 * Copy the 12 randomness bytes from decoded components.
 *
 * @param id Decoded components; must not be NULL.
 * @param randomness Receives exactly PIKAID_RANDOMNESS_LENGTH bytes;
 *                   untouched on failure.
 * @return PIKAID_OK, PIKAID_ERR_NULL_ARGUMENT or PIKAID_ERR_TIMESTAMP.
 */
PIKAID_API pikaid_status pikaid_get_randomness(
    const pikaid_t *id, uint8_t randomness[PIKAID_RANDOMNESS_LENGTH]);

/**
 * Return a short static English description of a status code.
 *
 * The returned pointer has static storage duration and must not be freed.
 * Unknown values return "unknown status".
 */
PIKAID_API const char *pikaid_status_string(pikaid_status status);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* PIKAID_H_INCLUDED */
