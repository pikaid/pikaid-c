/*
 * Strict decoding and validation tests: malformed strings, malformed binary
 * values, boundaries, error precedence and the shape/validity distinction.
 *
 * SPDX-License-Identifier: MIT
 */

#include "test_common.h"

#include <stdlib.h>
#include <string.h>

static const char *const valid_sample = "0s44we8002loz3po6mcf6vsn23";

static void check_parse_lengths(void)
{
    char buffer[64];
    size_t length;

    memset(buffer, '0', sizeof(buffer));
    for (length = 0; length <= 32; ++length) {
        pikaid_t decoded;

        memset(&decoded, 0x5A, sizeof(decoded));
        CHECK_STATUS(pikaid_parse(buffer, length, &decoded),
                     length == PIKAID_STRING_LENGTH ? PIKAID_OK
                                                    : PIKAID_ERR_LENGTH);
        if (length != PIKAID_STRING_LENGTH) {
            CHECK_U64(decoded.timestamp,
                      UINT64_C(0x5A5A5A5A5A5A5A5A));
        }
    }

    CHECK_STATUS(pikaid_parse(NULL, PIKAID_STRING_LENGTH, NULL),
                 PIKAID_ERR_NULL_ARGUMENT);
    CHECK_STATUS(pikaid_parse(buffer, PIKAID_STRING_LENGTH, NULL),
                 PIKAID_ERR_NULL_ARGUMENT);
}

static void check_invalid_characters(void)
{
    static const char bad_characters[] = {
        'A', 'Z', '-', '_', ' ', '\n', '\t', '+', '/', ':', '@',
        '.', '~', '!', '\'', '"', '\\', '|', '\r', '\v', '\f'};
    char text[PIKAID_STRING_SIZE];
    size_t position;
    size_t index;

    for (index = 0; index < sizeof(bad_characters); ++index) {
        for (position = 0; position < PIKAID_STRING_LENGTH; ++position) {
            pikaid_t decoded;

            memcpy(text, valid_sample, PIKAID_STRING_LENGTH);
            text[position] = bad_characters[index];
            text[PIKAID_STRING_LENGTH] = '\0';

            memset(&decoded, 0x11, sizeof(decoded));
            CHECK_STATUS(pikaid_parse(text, PIKAID_STRING_LENGTH, &decoded),
                         PIKAID_ERR_CHARACTER);
            CHECK_U64(decoded.timestamp, UINT64_C(0x1111111111111111));
            CHECK(!pikaid_is_valid(text, PIKAID_STRING_LENGTH));
            CHECK(!pikaid_is_well_formed(text, PIKAID_STRING_LENGTH));
        }
    }

    /* Bytes outside ASCII and embedded NULs are characters, not separators. */
    for (index = 0; index < 2; ++index) {
        unsigned char byte = (unsigned char)(index == 0 ? 0x80u : 0xFFu);
        pikaid_t decoded;

        memcpy(text, valid_sample, PIKAID_STRING_LENGTH);
        text[13] = (char)byte;
        text[PIKAID_STRING_LENGTH] = '\0';
        memset(&decoded, 0x11, sizeof(decoded));
        CHECK_STATUS(pikaid_parse(text, PIKAID_STRING_LENGTH, &decoded),
                     PIKAID_ERR_CHARACTER);
        CHECK_U64(decoded.timestamp, UINT64_C(0x1111111111111111));
        CHECK(!pikaid_is_valid(text, PIKAID_STRING_LENGTH));
        CHECK(!pikaid_is_well_formed(text, PIKAID_STRING_LENGTH));
    }

    /*
     * A zero-width space (U+200B) is a three-byte UTF-8 sequence, not a
     * single ignorable character: all three bytes must be rejected rather
     * than collapsed or skipped.
     */
    {
        static const unsigned char zero_width_space[3] = {0xE2u, 0x80u,
                                                          0x8Bu};
        pikaid_t decoded;

        memcpy(text, valid_sample, PIKAID_STRING_LENGTH);
        memcpy(text + 13, zero_width_space, sizeof(zero_width_space));
        text[PIKAID_STRING_LENGTH] = '\0';
        memset(&decoded, 0x11, sizeof(decoded));
        CHECK_STATUS(pikaid_parse(text, PIKAID_STRING_LENGTH, &decoded),
                     PIKAID_ERR_CHARACTER);
        CHECK_U64(decoded.timestamp, UINT64_C(0x1111111111111111));
        CHECK(!pikaid_is_valid(text, PIKAID_STRING_LENGTH));
        CHECK(!pikaid_is_well_formed(text, PIKAID_STRING_LENGTH));
    }

    for (position = 0; position < PIKAID_STRING_LENGTH; ++position) {
        pikaid_t decoded;

        memcpy(text, valid_sample, PIKAID_STRING_LENGTH);
        text[position] = '\0';
        text[PIKAID_STRING_LENGTH] = '\0';
        memset(&decoded, 0x11, sizeof(decoded));
        CHECK_STATUS(pikaid_parse(text, PIKAID_STRING_LENGTH, &decoded),
                     PIKAID_ERR_CHARACTER);
        CHECK_U64(decoded.timestamp, UINT64_C(0x1111111111111111));
        CHECK(!pikaid_is_well_formed(text, PIKAID_STRING_LENGTH));
    }
}

static void check_alphabet(void)
{
    static const char alphabet[] = "0123456789abcdefghijklmnopqrstuvwxyz";
    char text[PIKAID_STRING_SIZE];
    size_t index;

    for (index = 0; index < sizeof(alphabet) - 1; ++index) {
        memset(text, '0', PIKAID_STRING_LENGTH);
        text[0] = alphabet[index];
        text[PIKAID_STRING_LENGTH] = '\0';
        CHECK(pikaid_is_well_formed(text, PIKAID_STRING_LENGTH));
        CHECK(pikaid_is_valid(text, PIKAID_STRING_LENGTH));

        memset(text, '0', PIKAID_STRING_LENGTH);
        text[PIKAID_STRING_LENGTH - 1] = alphabet[index];
        text[PIKAID_STRING_LENGTH] = '\0';
        CHECK(pikaid_is_well_formed(text, PIKAID_STRING_LENGTH));
        CHECK(pikaid_is_valid(text, PIKAID_STRING_LENGTH));
    }
}

static void check_randomness_limits(void)
{
    pikaid_t decoded;

    /* Largest 96-bit value encoded in 19 Base36 digits. */
    CHECK(pikaid_is_valid("00000007oiylpimjg5u2ca1ypr",
                          PIKAID_STRING_LENGTH));
    CHECK_STATUS(pikaid_parse("00000007oiylpimjg5u2ca1ypr",
                              PIKAID_STRING_LENGTH, &decoded),
                 PIKAID_OK);
    {
        static const uint8_t maximum[PIKAID_RANDOMNESS_LENGTH] = {
            0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
            0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
        CHECK_BYTES(decoded.randomness, maximum, sizeof(maximum));
    }

    /* 2^96, one above the maximum, must be rejected. */
    memset(&decoded, 0x77, sizeof(decoded));
    CHECK_STATUS(pikaid_parse("00000007oiylpimjg5u2ca1yps",
                              PIKAID_STRING_LENGTH, &decoded),
                 PIKAID_ERR_RANDOMNESS);
    CHECK_U64(decoded.timestamp, UINT64_C(0x7777777777777777));

    /* All-'z' randomness is well formed but numerically impossible. */
    CHECK(pikaid_is_well_formed("0000000zzzzzzzzzzzzzzzzzzz",
                                PIKAID_STRING_LENGTH));
    CHECK(!pikaid_is_valid("0000000zzzzzzzzzzzzzzzzzzz",
                           PIKAID_STRING_LENGTH));
    CHECK_STATUS(pikaid_parse("0000000zzzzzzzzzzzzzzzzzzz",
                              PIKAID_STRING_LENGTH, &decoded),
                 PIKAID_ERR_RANDOMNESS);

    /* The maximum timestamp is always representable in seven digits. */
    CHECK(pikaid_is_valid("zzzzzzz0000000000000000000",
                          PIKAID_STRING_LENGTH));
}

static void check_parse_cstr(void)
{
    char text[64];
    pikaid_t decoded;

    memset(text, '0', sizeof(text));
    text[PIKAID_STRING_LENGTH] = '\0';
    CHECK_STATUS(pikaid_parse_cstr(text, &decoded), PIKAID_OK);

    text[PIKAID_STRING_LENGTH] = '0';
    text[PIKAID_STRING_LENGTH + 1] = '\0';
    CHECK_STATUS(pikaid_parse_cstr(text, &decoded), PIKAID_ERR_LENGTH);

    text[0] = '\0';
    CHECK_STATUS(pikaid_parse_cstr(text, &decoded), PIKAID_ERR_LENGTH);

    CHECK_STATUS(pikaid_parse_cstr(NULL, &decoded), PIKAID_ERR_NULL_ARGUMENT);
    CHECK_STATUS(pikaid_parse_cstr("", NULL), PIKAID_ERR_NULL_ARGUMENT);

    /* An embedded NUL is a short string, not a valid 26-byte identifier. */
    memcpy(text, valid_sample, PIKAID_STRING_LENGTH);
    text[10] = '\0';
    CHECK_STATUS(pikaid_parse_cstr(text, &decoded), PIKAID_ERR_LENGTH);
}

static void put_timestamp(uint8_t *binary, uint64_t timestamp)
{
    binary[0] = (uint8_t)(timestamp >> 32);
    binary[1] = (uint8_t)(timestamp >> 24);
    binary[2] = (uint8_t)(timestamp >> 16);
    binary[3] = (uint8_t)(timestamp >> 8);
    binary[4] = (uint8_t)timestamp;
}

static void check_binary_lengths(void)
{
    uint8_t buffer[32];
    size_t length;

    memset(buffer, 0, sizeof(buffer));
    for (length = 0; length <= 24; ++length) {
        pikaid_t decoded;

        memset(&decoded, 0x66, sizeof(decoded));
        CHECK_STATUS(pikaid_decode_binary(buffer, length, &decoded),
                     length == PIKAID_BINARY_LENGTH ? PIKAID_OK
                                                    : PIKAID_ERR_LENGTH);
        if (length != PIKAID_BINARY_LENGTH) {
            CHECK_U64(decoded.timestamp,
                      UINT64_C(0x6666666666666666));
        }
    }

    CHECK_STATUS(pikaid_decode_binary(NULL, PIKAID_BINARY_LENGTH, NULL),
                 PIKAID_ERR_NULL_ARGUMENT);
    CHECK_STATUS(pikaid_decode_binary(buffer, PIKAID_BINARY_LENGTH, NULL),
                 PIKAID_ERR_NULL_ARGUMENT);
}

static void check_binary_timestamps(void)
{
    static const uint64_t timestamps[] = {
        UINT64_C(0), UINT64_C(1), UINT64_C(35), UINT64_C(36),
        UINT64_C(1295), UINT64_C(1700000000), UINT64_C(78364164095)};
    uint8_t binary[PIKAID_BINARY_LENGTH];
    size_t index;

    for (index = 0; index < sizeof(timestamps) / sizeof(timestamps[0]);
         ++index) {
        pikaid_t decoded;

        memset(binary, 0xA7, sizeof(binary));
        put_timestamp(binary, timestamps[index]);
        CHECK_STATUS(pikaid_decode_binary(binary, sizeof(binary), &decoded),
                     PIKAID_OK);
        CHECK_U64(decoded.timestamp, timestamps[index]);
    }

    memset(binary, 0, sizeof(binary));
    put_timestamp(binary, UINT64_C(78364164096)); /* one above maximum */
    {
        pikaid_t decoded;

        memset(&decoded, 0x22, sizeof(decoded));
        CHECK_STATUS(pikaid_decode_binary(binary, sizeof(binary), &decoded),
                     PIKAID_ERR_TIMESTAMP);
        CHECK_U64(decoded.timestamp, UINT64_C(0x2222222222222222));
    }

    memset(binary, 0xFF, sizeof(binary));
    {
        pikaid_t decoded;

        CHECK_STATUS(pikaid_decode_binary(binary, sizeof(binary), &decoded),
                     PIKAID_ERR_TIMESTAMP);
    }
}

static void check_binary_entropy_preserved(void)
{
    uint8_t binary[PIKAID_BINARY_LENGTH];
    pikaid_t decoded;
    size_t index;

    for (index = 0; index < PIKAID_RANDOMNESS_LENGTH; ++index) {
        memset(binary, 0, sizeof(binary));
        binary[PIKAID_TIMESTAMP_BYTES + index] = 0xA5;
        CHECK_STATUS(pikaid_decode_binary(binary, sizeof(binary), &decoded),
                     PIKAID_OK);
        CHECK_U64(decoded.timestamp, 0);
        CHECK_SIZE(decoded.randomness[index], 0xA5);
        CHECK(decoded.randomness[(index + 1) % PIKAID_RANDOMNESS_LENGTH] == 0);
    }
}

static void check_unaligned_and_exact_buffers(void)
{
    uint8_t storage[PIKAID_BINARY_LENGTH + 1];
    uint8_t *exact;
    pikaid_t decoded;
    char *text;
    size_t index;

    memset(storage, 0x3C, sizeof(storage));
    memset(storage + 1, 0, PIKAID_BINARY_LENGTH);
    CHECK_STATUS(pikaid_decode_binary(storage + 1, PIKAID_BINARY_LENGTH,
                                      &decoded),
                 PIKAID_OK);
    CHECK_U64(decoded.timestamp, 0);

    /* Exact-size heap allocations catch overreads under AddressSanitizer. */
    exact = (uint8_t *)malloc(PIKAID_BINARY_LENGTH);
    CHECK(exact != NULL);
    if (exact != NULL) {
        memset(exact, 0, PIKAID_BINARY_LENGTH);
        CHECK_STATUS(pikaid_decode_binary(exact, PIKAID_BINARY_LENGTH,
                                          &decoded),
                     PIKAID_OK);
        CHECK(pikaid_is_valid("00000000000000000000000000",
                              PIKAID_STRING_LENGTH));
        free(exact);
    }

    text = (char *)malloc(PIKAID_STRING_LENGTH);
    CHECK(text != NULL);
    if (text != NULL) {
        for (index = 0; index < PIKAID_STRING_LENGTH; ++index) {
            text[index] = '0';
        }
        CHECK_STATUS(pikaid_parse(text, PIKAID_STRING_LENGTH, &decoded),
                     PIKAID_OK);
        text[PIKAID_STRING_LENGTH - 1] = '1';
        CHECK(pikaid_is_valid(text, PIKAID_STRING_LENGTH));
        free(text);
    }
}

static void check_shape_helpers(void)
{
    CHECK(!pikaid_is_valid(NULL, PIKAID_STRING_LENGTH));
    CHECK(!pikaid_is_valid(valid_sample, 25));
    CHECK(!pikaid_is_valid(valid_sample, 27));
    CHECK(!pikaid_is_well_formed(NULL, PIKAID_STRING_LENGTH));
    CHECK(!pikaid_is_well_formed(valid_sample, 25));
    CHECK(!pikaid_is_well_formed(valid_sample, 27));
    CHECK(pikaid_is_well_formed(valid_sample, PIKAID_STRING_LENGTH));
    CHECK(pikaid_is_valid(valid_sample, PIKAID_STRING_LENGTH));
}

int main(void)
{
    check_parse_lengths();
    check_invalid_characters();
    check_alphabet();
    check_randomness_limits();
    check_parse_cstr();
    check_binary_lengths();
    check_binary_timestamps();
    check_binary_entropy_preserved();
    check_unaligned_and_exact_buffers();
    check_shape_helpers();
    return pikaid_test_summary("test_decoding");
}
