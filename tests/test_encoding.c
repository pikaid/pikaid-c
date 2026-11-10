/*
 * Independent known-answer tests.
 *
 * Every expected value in this file was computed with Python integers from the
 * published layout rules (see tests/README.md), not from the C implementation.
 * If the C code and these constants disagree, one of them is wrong.
 *
 * SPDX-License-Identifier: MIT
 */

#include "test_common.h"

#include <string.h>

struct known_answer {
    const char *name;
    uint64_t timestamp;
    const char *entropy_hex;
    const char *id;
    const char *binary_hex;
};

static const struct known_answer known_answers[] = {
    {"ts-zero-entropy-zero", UINT64_C(0), "000000000000000000000000",
     "00000000000000000000000000",
     "0000000000000000000000000000000000"},
    {"ts-one-entropy-zero", UINT64_C(1), "000000000000000000000000",
     "00000010000000000000000000",
     "0000000001000000000000000000000000"},
    {"ts-35", UINT64_C(35), "000000000000000000000000",
     "000000z0000000000000000000",
     "0000000023000000000000000000000000"},
    {"ts-36", UINT64_C(36), "000000000000000000000000",
     "00000100000000000000000000",
     "0000000024000000000000000000000000"},
    {"ts-1295", UINT64_C(1295), "000000000000000000000000",
     "00000zz0000000000000000000",
     "000000050f000000000000000000000000"},
    {"ts-36pow6", UINT64_C(2176782336), "000000000000000000000000",
     "10000000000000000000000000",
     "0081bf1000000000000000000000000000"},
    {"entropy-1", UINT64_C(0), "000000000000000000000001",
     "00000000000000000000000001",
     "0000000000000000000000000000000001"},
    {"entropy-35", UINT64_C(0), "000000000000000000000023",
     "0000000000000000000000000z",
     "0000000000000000000000000000000023"},
    {"entropy-36", UINT64_C(0), "000000000000000000000024",
     "00000000000000000000000010",
     "0000000000000000000000000000000024"},
    {"entropy-36pow18", UINT64_C(0), "2153e468b91c6d1000000000",
     "00000001000000000000000000",
     "00000000002153e468b91c6d1000000000"},
    {"entropy-max", UINT64_C(0), "ffffffffffffffffffffffff",
     "00000007oiylpimjg5u2ca1ypr",
     "0000000000ffffffffffffffffffffffff"},
    {"timestamp-max", UINT64_C(78364164095), "000000000000000000000000",
     "zzzzzzz0000000000000000000",
     "123ede3fff000000000000000000000000"},
    {"official-sample", UINT64_C(1700000000), "00112233445566778899aabb",
     "0s44we8002loz3po6mcf6vsn23",
     "006553f10000112233445566778899aabb"},
    {"all-bytes", UINT64_C(1234567890), "000102030405060708090a0b",
     "0kf12oi0005ierqx6qq54n1onf",
     "00499602d2000102030405060708090a0b"},
};

static const size_t known_answer_count =
    sizeof(known_answers) / sizeof(known_answers[0]);

static void check_known_answers(void)
{
    size_t index;

    for (index = 0; index < known_answer_count; ++index) {
        const struct known_answer *answer = &known_answers[index];
        uint8_t entropy[PIKAID_RANDOMNESS_LENGTH];
        uint8_t expected_binary[PIKAID_BINARY_LENGTH];
        uint8_t binary[PIKAID_BINARY_LENGTH];
        char text[PIKAID_STRING_SIZE];
        pikaid_t decoded;

        CHECK(pikaid_test_hex_decode(answer->entropy_hex, entropy,
                                     sizeof(entropy)));
        CHECK(pikaid_test_hex_decode(answer->binary_hex, expected_binary,
                                     sizeof(expected_binary)));

        memset(text, 0x7F, sizeof(text));
        CHECK_STATUS(pikaid_encode(answer->timestamp, entropy, text), PIKAID_OK);
        CHECK_STRING(text, answer->id);
        CHECK(text[PIKAID_STRING_LENGTH] == '\0');

        memset(binary, 0x7F, sizeof(binary));
        CHECK_STATUS(pikaid_encode_binary(answer->timestamp, entropy, binary),
                     PIKAID_OK);
        CHECK_BYTES(binary, expected_binary, sizeof(binary));

        CHECK_STATUS(pikaid_parse(answer->id, strlen(answer->id), &decoded),
                     PIKAID_OK);
        CHECK_U64(decoded.timestamp, answer->timestamp);
        CHECK_BYTES(decoded.randomness, entropy, sizeof(entropy));

        memset(text, 0x7F, sizeof(text));
        CHECK_STATUS(pikaid_format(&decoded, text), PIKAID_OK);
        CHECK_STRING(text, answer->id);
        CHECK_STATUS(pikaid_to_binary(&decoded, binary), PIKAID_OK);
        CHECK_BYTES(binary, expected_binary, sizeof(binary));

        CHECK_STATUS(pikaid_decode_binary(expected_binary, sizeof(binary),
                                          &decoded),
                     PIKAID_OK);
        CHECK_U64(decoded.timestamp, answer->timestamp);
        CHECK_BYTES(decoded.randomness, entropy, sizeof(entropy));
    }
}

static void check_write_bounds(void)
{
    uint8_t entropy[PIKAID_RANDOMNESS_LENGTH];
    char text[40];
    uint8_t binary[32];
    size_t index;

    memset(entropy, 0xC3, sizeof(entropy));
    memset(text, 0x7F, sizeof(text));
    CHECK_STATUS(pikaid_encode(UINT64_C(1234), entropy, text), PIKAID_OK);
    for (index = PIKAID_STRING_SIZE; index < sizeof(text); ++index) {
        CHECK(text[index] == 0x7F);
    }

    memset(binary, 0x7F, sizeof(binary));
    CHECK_STATUS(pikaid_encode_binary(UINT64_C(1234), entropy, binary),
                 PIKAID_OK);
    for (index = PIKAID_BINARY_LENGTH; index < sizeof(binary); ++index) {
        CHECK(binary[index] == 0x7F);
    }
}

static void check_binary_layout(void)
{
    /* 0x0102030405 = 4,328,719,365 seconds, comfortably below the limit. */
    const uint64_t timestamp = UINT64_C(0x0102030405);
    static const uint8_t entropy[PIKAID_RANDOMNESS_LENGTH] = {
        0xF0, 0xE1, 0xD2, 0xC3, 0xB4, 0xA5,
        0x96, 0x87, 0x78, 0x69, 0x5A, 0x4B};
    static const uint8_t expected[PIKAID_BINARY_LENGTH] = {
        0x01, 0x02, 0x03, 0x04, 0x05, 0xF0, 0xE1, 0xD2, 0xC3,
        0xB4, 0xA5, 0x96, 0x87, 0x78, 0x69, 0x5A, 0x4B};
    uint8_t binary[PIKAID_BINARY_LENGTH];
    pikaid_t decoded;

    CHECK_STATUS(pikaid_encode_binary(timestamp, entropy, binary), PIKAID_OK);
    CHECK_BYTES(binary, expected, sizeof(binary));

    /* Entropy is copied unchanged, including leading zero bytes. */
    CHECK_STATUS(pikaid_decode_binary(expected, sizeof(expected), &decoded),
                 PIKAID_OK);
    CHECK_U64(decoded.timestamp, timestamp);
    CHECK_BYTES(decoded.randomness, entropy, sizeof(entropy));
}

static void check_length_constants(void)
{
    CHECK_SIZE(PIKAID_STRING_LENGTH, 26);
    CHECK_SIZE(PIKAID_STRING_SIZE, 27);
    CHECK_SIZE(PIKAID_BINARY_LENGTH, 17);
    CHECK_SIZE(PIKAID_RANDOMNESS_LENGTH, 12);
    CHECK_SIZE(PIKAID_TIMESTAMP_DIGITS, 7);
    CHECK_SIZE(PIKAID_RANDOMNESS_DIGITS, 19);
    CHECK_SIZE(PIKAID_TIMESTAMP_BYTES, 5);
    CHECK_U64(PIKAID_TIMESTAMP_MAX, UINT64_C(78364164095));
    CHECK_SIZE(sizeof(((pikaid_t *)0)->randomness), PIKAID_RANDOMNESS_LENGTH);
}

static void check_known_order(void)
{
    static const char *ordered[] = {
        "00000000000000000000000000", /* timestamp 0 */
        "000000z0000000000000000000", /* timestamp 35 */
        "00000100000000000000000000", /* timestamp 36 */
        "00000zz0000000000000000000", /* timestamp 1295 */
        "zzzzzzz0000000000000000000"  /* maximum timestamp */
    };
    size_t index;

    for (index = 1; index < sizeof(ordered) / sizeof(ordered[0]); ++index) {
        CHECK(strcmp(ordered[index - 1], ordered[index]) < 0);
        CHECK(pikaid_is_valid(ordered[index - 1], PIKAID_STRING_LENGTH));
        CHECK(pikaid_is_valid(ordered[index], PIKAID_STRING_LENGTH));
    }
}

int main(void)
{
    check_length_constants();
    check_known_answers();
    check_write_bounds();
    check_binary_layout();
    check_known_order();
    return pikaid_test_summary("test_encoding");
}
