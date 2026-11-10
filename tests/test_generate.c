/*
 * Generation tests using the real platform CSPRNG and clock.
 *
 * These checks are statistical only where a cryptographic property cannot be
 * observed directly: they would catch a constant, missing or obviously biased
 * randomness source, not a subtle CSPRNG defect.
 *
 * SPDX-License-Identifier: MIT
 */

#include "test_common.h"

#include <string.h>
#include <time.h>

/* Tolerance when comparing a generated timestamp with time(NULL). */
#define TIMESTAMP_TOLERANCE 300

static int timestamp_is_recent(uint64_t timestamp)
{
    time_t now = time(NULL);
    uint64_t current;

    if (now == (time_t)-1 || now < (time_t)0) {
        return 0;
    }
    current = (uint64_t)now;
    if (timestamp > current) {
        return timestamp - current <= TIMESTAMP_TOLERANCE;
    }
    return current - timestamp <= TIMESTAMP_TOLERANCE;
}

static void check_generate(void)
{
    char text[PIKAID_STRING_SIZE];
    pikaid_t decoded;
    uint64_t timestamp = 12345;

    CHECK_STATUS(pikaid_generate(text), PIKAID_OK);
    CHECK_SIZE(strlen(text), PIKAID_STRING_LENGTH);
    CHECK(pikaid_is_valid(text, PIKAID_STRING_LENGTH));
    CHECK_STATUS(pikaid_parse(text, PIKAID_STRING_LENGTH, &decoded),
                 PIKAID_OK);
    CHECK(timestamp_is_recent(decoded.timestamp));
    CHECK(pikaid_get_timestamp(&decoded, &timestamp) == PIKAID_OK);
    CHECK(timestamp_is_recent(timestamp));
}

static void check_generate_binary(void)
{
    uint8_t binary[PIKAID_BINARY_LENGTH];
    pikaid_t decoded;
    char text[PIKAID_STRING_SIZE];

    CHECK_STATUS(pikaid_generate_binary(binary), PIKAID_OK);
    CHECK_STATUS(pikaid_decode_binary(binary, sizeof(binary), &decoded),
                 PIKAID_OK);
    CHECK(timestamp_is_recent(decoded.timestamp));
    CHECK_STATUS(pikaid_format(&decoded, text), PIKAID_OK);
    CHECK(pikaid_is_valid(text, PIKAID_STRING_LENGTH));
}

static void check_explicit_timestamps(void)
{
    static const uint64_t timestamps[] = {
        UINT64_C(0), UINT64_C(1), UINT64_C(35), UINT64_C(36),
        UINT64_C(1700000000), PIKAID_TIMESTAMP_MAX};
    char text[PIKAID_STRING_SIZE];
    pikaid_t decoded;
    size_t index;

    for (index = 0; index < sizeof(timestamps) / sizeof(timestamps[0]);
         ++index) {
        CHECK_STATUS(pikaid_generate_at(timestamps[index], text), PIKAID_OK);
        CHECK(pikaid_is_valid(text, PIKAID_STRING_LENGTH));
        CHECK_STATUS(pikaid_parse(text, PIKAID_STRING_LENGTH, &decoded),
                     PIKAID_OK);
        CHECK_U64(decoded.timestamp, timestamps[index]);
    }
}

static void check_generate_binary_at(void)
{
    uint8_t binary[PIKAID_BINARY_LENGTH];
    pikaid_t decoded;
    char text[PIKAID_STRING_SIZE];

    CHECK_STATUS(pikaid_generate_binary_at(1700000000, binary), PIKAID_OK);
    CHECK_STATUS(pikaid_decode_binary(binary, sizeof(binary), &decoded),
                 PIKAID_OK);
    CHECK_U64(decoded.timestamp, UINT64_C(1700000000));
    CHECK_STATUS(pikaid_format(&decoded, text), PIKAID_OK);
    CHECK(pikaid_is_valid(text, PIKAID_STRING_LENGTH));

    CHECK_STATUS(pikaid_generate_binary_at(0, binary), PIKAID_OK);
    CHECK_STATUS(pikaid_decode_binary(binary, sizeof(binary), &decoded),
                 PIKAID_OK);
    CHECK_U64(decoded.timestamp, 0);
}

static void check_invalid_timestamp_does_not_generate(void)
{
    char text[PIKAID_STRING_SIZE];
    uint8_t binary[PIKAID_BINARY_LENGTH];
    size_t index;

    memset(text, 'Q', sizeof(text));
    CHECK_STATUS(pikaid_generate_at(PIKAID_TIMESTAMP_MAX + UINT64_C(1), text),
                 PIKAID_ERR_TIMESTAMP);
    CHECK(text[0] == '\0');

    memset(text, 'Q', sizeof(text));
    CHECK_STATUS(pikaid_generate_at(UINT64_MAX, text),
                 PIKAID_ERR_TIMESTAMP);
    CHECK(text[0] == '\0');

    memset(binary, 0xFF, sizeof(binary));
    CHECK_STATUS(pikaid_generate_binary_at(PIKAID_TIMESTAMP_MAX + UINT64_C(1),
                                           binary),
                 PIKAID_ERR_TIMESTAMP);
    for (index = 0; index < sizeof(binary); ++index) {
        CHECK(binary[index] == 0);
    }
}

static void check_uniqueness(void)
{
    enum { COUNT = 256 };
    static char identifiers[COUNT][PIKAID_STRING_SIZE];
    unsigned int index;
    unsigned int other;
    unsigned int duplicates = 0;

    for (index = 0; index < COUNT; ++index) {
        CHECK_STATUS(pikaid_generate_at(1700000000, identifiers[index]),
                     PIKAID_OK);
        CHECK(pikaid_is_valid(identifiers[index], PIKAID_STRING_LENGTH));
    }

    for (index = 0; index < COUNT; ++index) {
        for (other = index + 1; other < COUNT; ++other) {
            if (strcmp(identifiers[index], identifiers[other]) == 0) {
                ++duplicates;
            }
        }
    }
    /* A collision among 256 independent 96-bit values is negligible. */
    CHECK(duplicates == 0);
}

static void check_randomness_present(void)
{
    enum { COUNT = 64 };
    unsigned int seen[256];
    unsigned int index;
    unsigned int distinct = 0;
    unsigned int byte_index;
    unsigned int all_zero_suffixes = 0;
    unsigned int all_zero_entropies = 0;
    char text[PIKAID_STRING_SIZE];

    memset(seen, 0, sizeof(seen));

    for (index = 0; index < COUNT; ++index) {
        pikaid_t decoded;
        unsigned int zero_bytes = 0;

        CHECK_STATUS(pikaid_generate_at(1700000000, text), PIKAID_OK);
        CHECK_STATUS(pikaid_parse(text, PIKAID_STRING_LENGTH, &decoded),
                     PIKAID_OK);
        for (byte_index = 0; byte_index < PIKAID_RANDOMNESS_LENGTH;
             ++byte_index) {
            seen[decoded.randomness[byte_index]] = 1;
            if (decoded.randomness[byte_index] == 0) {
                ++zero_bytes;
            }
        }
        if (zero_bytes == PIKAID_RANDOMNESS_LENGTH) {
            ++all_zero_entropies;
        }
        if (strcmp(text + PIKAID_TIMESTAMP_DIGITS,
                   "0000000000000000000") == 0) {
            ++all_zero_suffixes;
        }
    }

    for (index = 0; index < 256; ++index) {
        distinct += seen[index];
    }

    /*
     * 768 random bytes should cover well over 200 distinct values. A constant
     * or zero-filled source would cover at most one.
     */
    CHECK(distinct > 200);
    CHECK(all_zero_suffixes == 0);
    CHECK(all_zero_entropies == 0);
}

static void check_write_bounds(void)
{
    char text[PIKAID_STRING_SIZE + 8];
    uint8_t binary[PIKAID_BINARY_LENGTH + 8];
    size_t index;

    memset(text, 0x7E, sizeof(text));
    CHECK_STATUS(pikaid_generate(text), PIKAID_OK);
    CHECK(text[PIKAID_STRING_LENGTH] == '\0');
    for (index = PIKAID_STRING_SIZE; index < sizeof(text); ++index) {
        CHECK(text[index] == 0x7E);
    }

    memset(binary, 0x7E, sizeof(binary));
    CHECK_STATUS(pikaid_generate_binary(binary), PIKAID_OK);
    for (index = PIKAID_BINARY_LENGTH; index < sizeof(binary); ++index) {
        CHECK(binary[index] == 0x7E);
    }

    memset(text, 0x7E, sizeof(text));
    CHECK_STATUS(pikaid_generate_at(42, text), PIKAID_OK);
    CHECK(text[PIKAID_STRING_LENGTH] == '\0');
    for (index = PIKAID_STRING_SIZE; index < sizeof(text); ++index) {
        CHECK(text[index] == 0x7E);
    }

    memset(binary, 0x7E, sizeof(binary));
    CHECK_STATUS(pikaid_generate_binary_at(42, binary), PIKAID_OK);
    for (index = PIKAID_BINARY_LENGTH; index < sizeof(binary); ++index) {
        CHECK(binary[index] == 0x7E);
    }
}

int main(void)
{
    check_generate();
    check_generate_binary();
    check_explicit_timestamps();
    check_generate_binary_at();
    check_invalid_timestamp_does_not_generate();
    check_uniqueness();
    check_randomness_present();
    check_write_bounds();
    return pikaid_test_summary("test_generate");
}
