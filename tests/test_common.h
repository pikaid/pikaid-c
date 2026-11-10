/*
 * Minimal test harness shared by the pikaid test programs.
 *
 * The harness is deliberately dependency-free. It records failures, prints
 * actionable diagnostics and returns a non-zero process status so CTest can
 * detect a broken suite. It is not installed.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef PIKAID_TEST_COMMON_H_INCLUDED
#define PIKAID_TEST_COMMON_H_INCLUDED

#include <stddef.h>
#include <stdint.h>

#include "pikaid.h"

extern int pikaid_test_checks;
extern int pikaid_test_failures;

void pikaid_test_fail_at(const char *file, int line, const char *message);

void pikaid_test_check(int condition, const char *expression, const char *file,
                       int line);
void pikaid_test_check_status(pikaid_status actual, pikaid_status expected,
                              const char *expression, const char *file,
                              int line);
void pikaid_test_check_u64(uint64_t actual, uint64_t expected,
                           const char *expression, const char *file, int line);
void pikaid_test_check_size(size_t actual, size_t expected,
                            const char *expression, const char *file, int line);
void pikaid_test_check_string(const char *actual, const char *expected,
                              const char *expression, const char *file,
                              int line);
void pikaid_test_check_bytes(const uint8_t *actual, const uint8_t *expected,
                             size_t length, const char *expression,
                             const char *file, int line);

#define CHECK(condition)                                                     \
    pikaid_test_check((condition) ? 1 : 0, #condition, __FILE__, __LINE__)

#define CHECK_STATUS(expression, expected)                                   \
    pikaid_test_check_status((expression), (expected), #expression, __FILE__, \
                             __LINE__)

#define CHECK_U64(expression, expected)                                      \
    pikaid_test_check_u64((expression), (expected), #expression, __FILE__,    \
                          __LINE__)

#define CHECK_SIZE(expression, expected)                                     \
    pikaid_test_check_size((expression), (expected), #expression, __FILE__,   \
                           __LINE__)

#define CHECK_STRING(expression, expected)                                   \
    pikaid_test_check_string((expression), (expected), #expression, __FILE__, \
                             __LINE__)

#define CHECK_BYTES(expression, expected, length)                            \
    pikaid_test_check_bytes((expression), (expected), (length), #expression,  \
                            __FILE__, __LINE__)

/*
 * Deterministic test-only pseudorandom generator (xorshift64*). This is used
 * to build test inputs; it is never used by the library for generation.
 */
void pikaid_test_random_seed(uint64_t seed);
uint64_t pikaid_test_random_u64(void);
void pikaid_test_random_bytes(uint8_t *buffer, size_t length);

/* Decode exactly out_length bytes from a lowercase/uppercase hex string. */
int pikaid_test_hex_decode(const char *hex, uint8_t *out, size_t out_length);

/* Print a summary and return the process exit status (0 = all passed). */
int pikaid_test_summary(const char *suite);

#endif /* PIKAID_TEST_COMMON_H_INCLUDED */
