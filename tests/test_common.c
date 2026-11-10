/*
 * SPDX-License-Identifier: MIT
 */

#include "test_common.h"

#include <stdio.h>
#include <string.h>

int pikaid_test_checks = 0;
int pikaid_test_failures = 0;

static uint64_t pikaid_test_rng_state = UINT64_C(0x9e3779b97f4a7c15);

void pikaid_test_fail_at(const char *file, int line, const char *message)
{
    ++pikaid_test_failures;
    (void)fprintf(stderr, "FAIL %s:%d: %s\n", file, line, message);
}

void pikaid_test_check(int condition, const char *expression, const char *file,
                       int line)
{
    ++pikaid_test_checks;
    if (!condition) {
        ++pikaid_test_failures;
        (void)fprintf(stderr, "FAIL %s:%d: %s\n", file, line, expression);
    }
}

void pikaid_test_check_status(pikaid_status actual, pikaid_status expected,
                              const char *expression, const char *file,
                              int line)
{
    ++pikaid_test_checks;
    if (actual != expected) {
        ++pikaid_test_failures;
        (void)fprintf(stderr,
                      "FAIL %s:%d: %s expected %s (%d), got %s (%d)\n", file,
                      line, expression, pikaid_status_string(expected),
                      (int)expected, pikaid_status_string(actual), (int)actual);
    }
}

void pikaid_test_check_u64(uint64_t actual, uint64_t expected,
                           const char *expression, const char *file, int line)
{
    ++pikaid_test_checks;
    if (actual != expected) {
        ++pikaid_test_failures;
        (void)fprintf(stderr, "FAIL %s:%d: %s expected %llu, got %llu\n", file,
                      line, expression, (unsigned long long)expected,
                      (unsigned long long)actual);
    }
}

void pikaid_test_check_size(size_t actual, size_t expected,
                            const char *expression, const char *file, int line)
{
    ++pikaid_test_checks;
    if (actual != expected) {
        ++pikaid_test_failures;
        (void)fprintf(stderr, "FAIL %s:%d: %s expected %zu, got %zu\n", file,
                      line, expression, expected, actual);
    }
}

void pikaid_test_check_string(const char *actual, const char *expected,
                              const char *expression, const char *file,
                              int line)
{
    ++pikaid_test_checks;
    if (actual == NULL || expected == NULL || strcmp(actual, expected) != 0) {
        ++pikaid_test_failures;
        (void)fprintf(stderr, "FAIL %s:%d: %s expected \"%s\", got \"%s\"\n",
                      file, line, expression,
                      expected == NULL ? "(null)" : expected,
                      actual == NULL ? "(null)" : actual);
    }
}

void pikaid_test_check_bytes(const uint8_t *actual, const uint8_t *expected,
                             size_t length, const char *expression,
                             const char *file, int line)
{
    ++pikaid_test_checks;
    if (actual == NULL || expected == NULL ||
        memcmp(actual, expected, length) != 0) {
        size_t index;
        ++pikaid_test_failures;
        (void)fprintf(stderr, "FAIL %s:%d: %s byte mismatch\n", file, line,
                      expression);
        (void)fprintf(stderr, "  expected:");
        for (index = 0; index < length; ++index) {
            (void)fprintf(stderr, " %02x", expected[index]);
        }
        (void)fprintf(stderr, "\n  actual:  ");
        for (index = 0; index < length; ++index) {
            (void)fprintf(stderr, " %02x", actual[index]);
        }
        (void)fprintf(stderr, "\n");
    }
}

void pikaid_test_random_seed(uint64_t seed)
{
    pikaid_test_rng_state = seed == 0 ? UINT64_C(0x9e3779b97f4a7c15) : seed;
}

uint64_t pikaid_test_random_u64(void)
{
    uint64_t state = pikaid_test_rng_state;

    state ^= state >> 12;
    state ^= state << 25;
    state ^= state >> 27;
    pikaid_test_rng_state = state;
    return state * UINT64_C(2685821657736338717);
}

void pikaid_test_random_bytes(uint8_t *buffer, size_t length)
{
    size_t index;

    for (index = 0; index < length; ++index) {
        if (index % 8 == 0) {
            uint64_t value = pikaid_test_random_u64();
            size_t offset;

            for (offset = 0; offset < 8 && index + offset < length; ++offset) {
                buffer[index + offset] = (uint8_t)(value >> (offset * 8));
            }
        }
    }
}

static int hex_digit_value(char character)
{
    if (character >= '0' && character <= '9') {
        return character - '0';
    }
    if (character >= 'a' && character <= 'f') {
        return character - 'a' + 10;
    }
    if (character >= 'A' && character <= 'F') {
        return character - 'A' + 10;
    }
    return -1;
}

int pikaid_test_hex_decode(const char *hex, uint8_t *out, size_t out_length)
{
    size_t index;

    if (hex == NULL || out == NULL || strlen(hex) != out_length * 2) {
        return 0;
    }
    for (index = 0; index < out_length; ++index) {
        int high = hex_digit_value(hex[index * 2]);
        int low = hex_digit_value(hex[index * 2 + 1]);

        if (high < 0 || low < 0) {
            return 0;
        }
        out[index] = (uint8_t)((high << 4) | low);
    }
    return 1;
}

int pikaid_test_summary(const char *suite)
{
    (void)printf("%s: %d checks, %d failures\n", suite, pikaid_test_checks,
                 pikaid_test_failures);
    return pikaid_test_failures == 0 ? 0 : 1;
}
