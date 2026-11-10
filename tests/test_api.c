/*
 * Public API contract tests: NULL handling, status codes, buffer-clearing and
 * untouched-output guarantees, comparison signs and status strings.
 *
 * SPDX-License-Identifier: MIT
 */

#include "test_common.h"

#include <string.h>

static const char *const sample = "0s44we8002loz3po6mcf6vsn23";
static const char *const zero_id = "00000000000000000000000000";
static const char *const max_id = "zzzzzzz7oiylpimjg5u2ca1ypr";

static void check_status_enum(void)
{
    CHECK(PIKAID_OK == 0);
    CHECK(PIKAID_ERR_NULL_ARGUMENT == 1);
    CHECK(PIKAID_ERR_LENGTH == 2);
    CHECK(PIKAID_ERR_CHARACTER == 3);
    CHECK(PIKAID_ERR_TIMESTAMP == 4);
    CHECK(PIKAID_ERR_RANDOMNESS == 5);
    CHECK(PIKAID_ERR_ENTROPY == 6);
    CHECK(PIKAID_ERR_CLOCK == 7);

    CHECK_STRING(pikaid_status_string(PIKAID_OK), "ok");
    CHECK_STRING(pikaid_status_string(PIKAID_ERR_NULL_ARGUMENT),
                 "null argument");
    CHECK_STRING(pikaid_status_string(PIKAID_ERR_LENGTH), "invalid length");
    CHECK_STRING(pikaid_status_string(PIKAID_ERR_CHARACTER),
                 "invalid character");
    CHECK_STRING(pikaid_status_string(PIKAID_ERR_TIMESTAMP),
                 "timestamp out of range");
    CHECK_STRING(pikaid_status_string(PIKAID_ERR_RANDOMNESS),
                 "randomness exceeds 96 bits");
    CHECK_STRING(pikaid_status_string(PIKAID_ERR_ENTROPY),
                 "entropy source failure");
    CHECK_STRING(pikaid_status_string(PIKAID_ERR_CLOCK), "clock failure");
    CHECK_STRING(pikaid_status_string((pikaid_status)99), "unknown status");
    CHECK_STRING(pikaid_status_string((pikaid_status)-1), "unknown status");
}

static void check_null_arguments(void)
{
    uint8_t entropy[PIKAID_RANDOMNESS_LENGTH] = {0};
    uint8_t binary[PIKAID_BINARY_LENGTH];
    char text[PIKAID_STRING_SIZE];
    pikaid_t value;
    int comparison = 0;
    uint64_t timestamp = 0;

    memset(&value, 0, sizeof(value));

    CHECK_STATUS(pikaid_encode(0, entropy, NULL),
                 PIKAID_ERR_NULL_ARGUMENT);
    CHECK_STATUS(pikaid_encode(0, NULL, NULL),
                 PIKAID_ERR_NULL_ARGUMENT);
    CHECK_STATUS(pikaid_encode_binary(0, entropy, NULL),
                 PIKAID_ERR_NULL_ARGUMENT);
    CHECK_STATUS(pikaid_encode_binary(0, NULL, binary),
                 PIKAID_ERR_NULL_ARGUMENT);
    CHECK_STATUS(pikaid_parse(NULL, PIKAID_STRING_LENGTH, &value),
                 PIKAID_ERR_NULL_ARGUMENT);
    CHECK_STATUS(pikaid_parse(sample, PIKAID_STRING_LENGTH, NULL),
                 PIKAID_ERR_NULL_ARGUMENT);
    CHECK_STATUS(pikaid_parse_cstr(NULL, &value),
                 PIKAID_ERR_NULL_ARGUMENT);
    CHECK_STATUS(pikaid_parse_cstr(sample, NULL),
                 PIKAID_ERR_NULL_ARGUMENT);
    CHECK_STATUS(pikaid_decode_binary(NULL, PIKAID_BINARY_LENGTH, &value),
                 PIKAID_ERR_NULL_ARGUMENT);
    CHECK_STATUS(pikaid_decode_binary(binary, PIKAID_BINARY_LENGTH, NULL),
                 PIKAID_ERR_NULL_ARGUMENT);
    CHECK_STATUS(pikaid_format(NULL, text), PIKAID_ERR_NULL_ARGUMENT);
    CHECK_STATUS(pikaid_format(&value, NULL), PIKAID_ERR_NULL_ARGUMENT);
    CHECK_STATUS(pikaid_to_binary(NULL, binary), PIKAID_ERR_NULL_ARGUMENT);
    CHECK_STATUS(pikaid_to_binary(&value, NULL), PIKAID_ERR_NULL_ARGUMENT);
    CHECK_STATUS(pikaid_compare(NULL, &value, &comparison),
                 PIKAID_ERR_NULL_ARGUMENT);
    CHECK_STATUS(pikaid_compare(&value, NULL, &comparison),
                 PIKAID_ERR_NULL_ARGUMENT);
    CHECK_STATUS(pikaid_compare(&value, &value, NULL),
                 PIKAID_ERR_NULL_ARGUMENT);
    CHECK_STATUS(pikaid_compare_strings(NULL, sample, &comparison),
                 PIKAID_ERR_NULL_ARGUMENT);
    CHECK_STATUS(pikaid_compare_strings(sample, NULL, &comparison),
                 PIKAID_ERR_NULL_ARGUMENT);
    CHECK_STATUS(pikaid_compare_strings(sample, sample, NULL),
                 PIKAID_ERR_NULL_ARGUMENT);
    CHECK_STATUS(pikaid_get_timestamp(NULL, &timestamp),
                 PIKAID_ERR_NULL_ARGUMENT);
    CHECK_STATUS(pikaid_get_timestamp(&value, NULL),
                 PIKAID_ERR_NULL_ARGUMENT);
    CHECK_STATUS(pikaid_get_randomness(NULL, entropy),
                 PIKAID_ERR_NULL_ARGUMENT);
    CHECK_STATUS(pikaid_get_randomness(&value, NULL),
                 PIKAID_ERR_NULL_ARGUMENT);

    CHECK_STATUS(pikaid_generate(NULL), PIKAID_ERR_NULL_ARGUMENT);
    CHECK_STATUS(pikaid_generate_at(0, NULL), PIKAID_ERR_NULL_ARGUMENT);
    CHECK_STATUS(pikaid_generate_binary(NULL), PIKAID_ERR_NULL_ARGUMENT);
    CHECK_STATUS(pikaid_generate_binary_at(0, NULL),
                 PIKAID_ERR_NULL_ARGUMENT);

    CHECK(!pikaid_is_valid(NULL, PIKAID_STRING_LENGTH));
    CHECK(!pikaid_is_well_formed(NULL, PIKAID_STRING_LENGTH));
    CHECK(comparison == 0);
}

static void check_encode_buffer_contract(void)
{
    uint8_t entropy[PIKAID_RANDOMNESS_LENGTH] = {0};
    uint8_t binary[PIKAID_BINARY_LENGTH];
    char text[PIKAID_STRING_SIZE];
    size_t index;

    /* Failed string writes set out[0] = '\0' and nothing else. */
    memset(text, 'X', sizeof(text));
    CHECK_STATUS(pikaid_encode(PIKAID_TIMESTAMP_MAX + UINT64_C(1), entropy,
                               text),
                 PIKAID_ERR_TIMESTAMP);
    CHECK(text[0] == '\0');
    for (index = 1; index < sizeof(text); ++index) {
        CHECK(text[index] == 'X');
    }

    /* Failed binary writes zero the whole 17-byte buffer. */
    memset(binary, 0xFF, sizeof(binary));
    CHECK_STATUS(pikaid_encode_binary(PIKAID_TIMESTAMP_MAX + UINT64_C(1),
                                      entropy, binary),
                 PIKAID_ERR_TIMESTAMP);
    for (index = 0; index < sizeof(binary); ++index) {
        CHECK(binary[index] == 0);
    }

    /* NULL out must be rejected before anything is written. */
    CHECK_STATUS(pikaid_encode(PIKAID_TIMESTAMP_MAX + UINT64_C(1), entropy,
                               NULL),
                 PIKAID_ERR_NULL_ARGUMENT);
    CHECK_STATUS(pikaid_encode_binary(PIKAID_TIMESTAMP_MAX + UINT64_C(1),
                                      entropy, NULL),
                 PIKAID_ERR_NULL_ARGUMENT);
}

static void check_format_buffer_contract(void)
{
    pikaid_t value;
    char text[PIKAID_STRING_SIZE];
    uint8_t binary[PIKAID_BINARY_LENGTH];
    size_t index;

    memset(&value, 0, sizeof(value));
    value.timestamp = PIKAID_TIMESTAMP_MAX + UINT64_C(1);

    memset(text, 'Y', sizeof(text));
    CHECK_STATUS(pikaid_format(&value, text), PIKAID_ERR_TIMESTAMP);
    CHECK(text[0] == '\0');
    for (index = 1; index < sizeof(text); ++index) {
        CHECK(text[index] == 'Y');
    }

    memset(binary, 0xFF, sizeof(binary));
    CHECK_STATUS(pikaid_to_binary(&value, binary), PIKAID_ERR_TIMESTAMP);
    for (index = 0; index < sizeof(binary); ++index) {
        CHECK(binary[index] == 0);
    }

    CHECK_STATUS(pikaid_format(&value, NULL), PIKAID_ERR_NULL_ARGUMENT);
    CHECK_STATUS(pikaid_to_binary(&value, NULL), PIKAID_ERR_NULL_ARGUMENT);
}

static void check_parse_leaves_output_untouched(void)
{
    pikaid_t value;
    char buffer[PIKAID_STRING_SIZE];
    uint8_t binary[PIKAID_BINARY_LENGTH] = {0};

    /* Wrong length. */
    memset(&value, 0xAB, sizeof(value));
    CHECK_STATUS(pikaid_parse(sample, PIKAID_STRING_LENGTH - 1, &value),
                 PIKAID_ERR_LENGTH);
    CHECK_U64(value.timestamp, UINT64_C(0xABABABABABABABAB));
    CHECK(value.randomness[0] == 0xAB);

    /* Bad character. */
    memcpy(buffer, sample, sizeof(buffer));
    buffer[3] = 'A';
    memset(&value, 0xCD, sizeof(value));
    CHECK_STATUS(pikaid_parse(buffer, PIKAID_STRING_LENGTH, &value),
                 PIKAID_ERR_CHARACTER);
    CHECK_U64(value.timestamp, UINT64_C(0xCDCDCDCDCDCDCDCD));

    /* Randomness overflow. */
    memset(&value, 0xEF, sizeof(value));
    CHECK_STATUS(pikaid_parse("0000000zzzzzzzzzzzzzzzzzzz",
                              PIKAID_STRING_LENGTH, &value),
                 PIKAID_ERR_RANDOMNESS);
    CHECK_U64(value.timestamp, UINT64_C(0xEFEFEFEFEFEFEFEF));

    /* Binary wrong length and out-of-range timestamp. */
    memset(&value, 0x12, sizeof(value));
    CHECK_STATUS(pikaid_decode_binary(binary, PIKAID_BINARY_LENGTH - 1,
                                      &value),
                 PIKAID_ERR_LENGTH);
    CHECK_U64(value.timestamp, UINT64_C(0x1212121212121212));

    memset(binary, 0xFF, sizeof(binary));
    memset(&value, 0x34, sizeof(value));
    CHECK_STATUS(pikaid_decode_binary(binary, sizeof(binary), &value),
                 PIKAID_ERR_TIMESTAMP);
    CHECK_U64(value.timestamp, UINT64_C(0x3434343434343434));
    CHECK(value.randomness[0] == 0x34);
}

static void check_getters(void)
{
    pikaid_t value;
    uint64_t timestamp = UINT64_C(0xDEADBEEF);
    uint8_t randomness[PIKAID_RANDOMNESS_LENGTH];

    CHECK_STATUS(pikaid_parse(sample, PIKAID_STRING_LENGTH, &value),
                 PIKAID_OK);
    CHECK_STATUS(pikaid_get_timestamp(&value, &timestamp), PIKAID_OK);
    CHECK_U64(timestamp, UINT64_C(1700000000));

    memset(randomness, 0x5A, sizeof(randomness));
    CHECK_STATUS(pikaid_get_randomness(&value, randomness), PIKAID_OK);
    {
        static const uint8_t expected[PIKAID_RANDOMNESS_LENGTH] = {
            0x00, 0x11, 0x22, 0x33, 0x44, 0x55,
            0x66, 0x77, 0x88, 0x99, 0xAA, 0xBB};
        CHECK_BYTES(randomness, expected, sizeof(expected));
    }

    /* Out-of-range structures fail and leave outputs untouched. */
    value.timestamp = PIKAID_TIMESTAMP_MAX + UINT64_C(1);
    timestamp = UINT64_C(0xFEEDFACE);
    CHECK_STATUS(pikaid_get_timestamp(&value, &timestamp),
                 PIKAID_ERR_TIMESTAMP);
    CHECK_U64(timestamp, UINT64_C(0xFEEDFACE));

    memset(randomness, 0xA5, sizeof(randomness));
    CHECK_STATUS(pikaid_get_randomness(&value, randomness),
                 PIKAID_ERR_TIMESTAMP);
    CHECK(randomness[0] == 0xA5);
    CHECK(randomness[PIKAID_RANDOMNESS_LENGTH - 1] == 0xA5);

    /* pikaid_t.timestamp is uint64_t, so negative values cannot be built. */
    CHECK_U64(PIKAID_TIMESTAMP_MAX, UINT64_C(78364164095));
}

static void check_compare(void)
{
    pikaid_t a;
    pikaid_t b;
    int result = 55;

    /* Different timestamps. */
    CHECK_STATUS(pikaid_parse(zero_id, PIKAID_STRING_LENGTH, &a), PIKAID_OK);
    CHECK_STATUS(pikaid_parse(max_id, PIKAID_STRING_LENGTH, &b), PIKAID_OK);
    CHECK_STATUS(pikaid_compare(&a, &b, &result), PIKAID_OK);
    CHECK(result == -1);
    CHECK_STATUS(pikaid_compare(&b, &a, &result), PIKAID_OK);
    CHECK(result == 1);

    /* Same timestamp, different randomness. */
    CHECK_STATUS(pikaid_parse("00000000000000000000000000", PIKAID_STRING_LENGTH,
                              &a),
                 PIKAID_OK);
    CHECK_STATUS(pikaid_parse("00000000000000000000000001", PIKAID_STRING_LENGTH,
                              &b),
                 PIKAID_OK);
    CHECK_STATUS(pikaid_compare(&a, &b, &result), PIKAID_OK);
    CHECK(result == -1);
    CHECK_STATUS(pikaid_compare(&b, &a, &result), PIKAID_OK);
    CHECK(result == 1);

    /* Equal values, including a struct that is a copy. */
    CHECK_STATUS(pikaid_compare(&a, &a, &result), PIKAID_OK);
    CHECK(result == 0);
    b = a;
    CHECK_STATUS(pikaid_compare(&a, &b, &result), PIKAID_OK);
    CHECK(result == 0);

    /* Out-of-range timestamp anywhere fails and leaves the result untouched. */
    b.timestamp = PIKAID_TIMESTAMP_MAX + UINT64_C(1);
    result = 77;
    CHECK_STATUS(pikaid_compare(&a, &b, &result), PIKAID_ERR_TIMESTAMP);
    CHECK(result == 77);
    CHECK_STATUS(pikaid_compare(&b, &a, &result), PIKAID_ERR_TIMESTAMP);
    CHECK(result == 77);
}

static void check_compare_strings(void)
{
    int result = 33;
    char invalid[PIKAID_STRING_SIZE];

    CHECK_STATUS(pikaid_compare_strings(sample, sample, &result), PIKAID_OK);
    CHECK(result == 0);

    CHECK_STATUS(pikaid_compare_strings(zero_id, max_id, &result), PIKAID_OK);
    CHECK(result == -1);
    CHECK_STATUS(pikaid_compare_strings(max_id, zero_id, &result), PIKAID_OK);
    CHECK(result == 1);

    /* Any validation failure propagates and leaves the result untouched. */
    memcpy(invalid, sample, PIKAID_STRING_SIZE);
    invalid[0] = 'A';
    result = 44;
    CHECK_STATUS(pikaid_compare_strings(invalid, sample, &result),
                 PIKAID_ERR_CHARACTER);
    CHECK(result == 44);
    CHECK_STATUS(pikaid_compare_strings(sample, invalid, &result),
                 PIKAID_ERR_CHARACTER);
    CHECK(result == 44);

    result = 66;
    CHECK_STATUS(pikaid_compare_strings("", sample, &result),
                 PIKAID_ERR_LENGTH);
    CHECK(result == 66);
    CHECK_STATUS(pikaid_compare_strings(sample, "short", &result),
                 PIKAID_ERR_LENGTH);
    CHECK(result == 66);

    result = 88;
    CHECK_STATUS(pikaid_compare_strings("0000000zzzzzzzzzzzzzzzzzzz",
                                        sample, &result),
                 PIKAID_ERR_RANDOMNESS);
    CHECK(result == 88);
}

static void check_length_rejection(void)
{
    size_t length;

    for (length = 0; length <= 40; ++length) {
        char buffer[41];

        memset(buffer, '0', sizeof(buffer));
        CHECK(pikaid_is_valid(buffer, length) ==
              (length == PIKAID_STRING_LENGTH));
    }
}

int main(void)
{
    check_status_enum();
    check_null_arguments();
    check_encode_buffer_contract();
    check_format_buffer_contract();
    check_parse_leaves_output_untouched();
    check_getters();
    check_compare();
    check_compare_strings();
    check_length_rejection();
    return pikaid_test_summary("test_api");
}
