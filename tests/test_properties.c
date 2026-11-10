/*
 * Randomized property tests and cross-representation ordering checks.
 *
 * SPDX-License-Identifier: MIT
 */

#include "test_common.h"

#include <stdio.h>
#include <string.h>

static uint64_t random_timestamp(void)
{
    return pikaid_test_random_u64() % (PIKAID_TIMESTAMP_MAX + UINT64_C(1));
}

static int sign_of(int value)
{
    return (value > 0) - (value < 0);
}

static void check_random_round_trips(void)
{
    unsigned int iteration;

    for (iteration = 0; iteration < 2000; ++iteration) {
        uint64_t timestamp = random_timestamp();
        uint8_t entropy[PIKAID_RANDOMNESS_LENGTH];
        uint8_t binary[PIKAID_BINARY_LENGTH];
        uint8_t restored[PIKAID_BINARY_LENGTH];
        pikaid_t decoded;
        pikaid_t from_binary;
        char text[PIKAID_STRING_SIZE];
        char formatted[PIKAID_STRING_SIZE];

        pikaid_test_random_bytes(entropy, sizeof(entropy));

        CHECK_STATUS(pikaid_encode(timestamp, entropy, text), PIKAID_OK);
        CHECK(pikaid_is_well_formed(text, PIKAID_STRING_LENGTH));
        CHECK(pikaid_is_valid(text, PIKAID_STRING_LENGTH));
        CHECK_STATUS(pikaid_parse(text, PIKAID_STRING_LENGTH, &decoded),
                     PIKAID_OK);
        CHECK_U64(decoded.timestamp, timestamp);
        CHECK_BYTES(decoded.randomness, entropy, sizeof(entropy));

        CHECK_STATUS(pikaid_encode_binary(timestamp, entropy, binary),
                     PIKAID_OK);
        CHECK_STATUS(pikaid_decode_binary(binary, sizeof(binary),
                                          &from_binary),
                     PIKAID_OK);
        CHECK_U64(from_binary.timestamp, timestamp);
        CHECK_BYTES(from_binary.randomness, entropy, sizeof(entropy));

        CHECK_STATUS(pikaid_to_binary(&decoded, restored), PIKAID_OK);
        CHECK_BYTES(restored, binary, sizeof(binary));

        CHECK_STATUS(pikaid_format(&from_binary, formatted), PIKAID_OK);
        CHECK_STRING(formatted, text);

        {
            int comparison = 99;
            CHECK_STATUS(pikaid_compare(&decoded, &from_binary, &comparison),
                         PIKAID_OK);
            CHECK(comparison == 0);
            CHECK_STATUS(pikaid_compare_strings(text, formatted, &comparison),
                         PIKAID_OK);
            CHECK(comparison == 0);
        }
    }
}

static void check_ordering_agreement(void)
{
    unsigned int iteration;

    for (iteration = 0; iteration < 1000; ++iteration) {
        uint64_t timestamp_a = random_timestamp();
        uint64_t timestamp_b = random_timestamp();
        uint8_t entropy_a[PIKAID_RANDOMNESS_LENGTH];
        uint8_t entropy_b[PIKAID_RANDOMNESS_LENGTH];
        char text_a[PIKAID_STRING_SIZE];
        char text_b[PIKAID_STRING_SIZE];
        uint8_t binary_a[PIKAID_BINARY_LENGTH];
        uint8_t binary_b[PIKAID_BINARY_LENGTH];
        pikaid_t a;
        pikaid_t b;
        int compared = 0;
        int string_order;
        int binary_order;

        pikaid_test_random_bytes(entropy_a, sizeof(entropy_a));
        pikaid_test_random_bytes(entropy_b, sizeof(entropy_b));

        CHECK_STATUS(pikaid_encode(timestamp_a, entropy_a, text_a), PIKAID_OK);
        CHECK_STATUS(pikaid_encode(timestamp_b, entropy_b, text_b), PIKAID_OK);
        CHECK_STATUS(pikaid_encode_binary(timestamp_a, entropy_a, binary_a),
                     PIKAID_OK);
        CHECK_STATUS(pikaid_encode_binary(timestamp_b, entropy_b, binary_b),
                     PIKAID_OK);
        CHECK_STATUS(pikaid_parse(text_a, PIKAID_STRING_LENGTH, &a), PIKAID_OK);
        CHECK_STATUS(pikaid_parse(text_b, PIKAID_STRING_LENGTH, &b), PIKAID_OK);

        CHECK_STATUS(pikaid_compare(&a, &b, &compared), PIKAID_OK);
        string_order = sign_of(strcmp(text_a, text_b));
        binary_order = sign_of(memcmp(binary_a, binary_b,
                                      PIKAID_BINARY_LENGTH));

        CHECK(compared == string_order);
        CHECK(compared == binary_order);

        CHECK_STATUS(pikaid_compare_strings(text_a, text_b, &compared),
                     PIKAID_OK);
        CHECK(compared == string_order);
    }
}

static void check_cross_second_ordering(void)
{
    unsigned int iteration;

    for (iteration = 0; iteration < 500; ++iteration) {
        uint64_t earlier = random_timestamp();
        uint64_t later;
        uint8_t entropy_a[PIKAID_RANDOMNESS_LENGTH];
        uint8_t entropy_b[PIKAID_RANDOMNESS_LENGTH];
        char text_a[PIKAID_STRING_SIZE];
        char text_b[PIKAID_STRING_SIZE];
        uint8_t binary_a[PIKAID_BINARY_LENGTH];
        uint8_t binary_b[PIKAID_BINARY_LENGTH];

        if (earlier == PIKAID_TIMESTAMP_MAX) {
            continue;
        }
        later = earlier + UINT64_C(1) +
                (pikaid_test_random_u64() %
                 (PIKAID_TIMESTAMP_MAX - earlier));

        pikaid_test_random_bytes(entropy_a, sizeof(entropy_a));
        pikaid_test_random_bytes(entropy_b, sizeof(entropy_b));

        CHECK_STATUS(pikaid_encode(earlier, entropy_a, text_a), PIKAID_OK);
        CHECK_STATUS(pikaid_encode(later, entropy_b, text_b), PIKAID_OK);
        CHECK(strcmp(text_a, text_b) < 0);
        /* The reverse comparison must be the strict opposite. */
        CHECK(strcmp(text_b, text_a) > 0);

        CHECK_STATUS(pikaid_encode_binary(earlier, entropy_a, binary_a),
                     PIKAID_OK);
        CHECK_STATUS(pikaid_encode_binary(later, entropy_b, binary_b),
                     PIKAID_OK);
        CHECK(memcmp(binary_a, binary_b, PIKAID_BINARY_LENGTH) < 0);
        CHECK(memcmp(binary_b, binary_a, PIKAID_BINARY_LENGTH) > 0);
    }
}

static void check_same_second_ordering(void)
{
    /*
     * Expected ids were derived independently with Python integers from the
     * layout rules. Within one second, order is by the unsigned 96-bit
     * randomness value, which is the same as Base36 suffix order.
     */
    static const struct {
        const char *entropy_hex;
        const char *expected_id;
    } cases[] = {
        {"000000000000000000000000", "0s44we80000000000000000000"},
        {"000000000000000000000001", "0s44we80000000000000000001"},
        {"000000000000000000000023", "0s44we8000000000000000000z"},
        {"000000000000000000000024", "0s44we80000000000000000010"},
        {"7fffffffffffffffffffffff", "0s44we83u9haurb9q2x1650zcv"},
        {"ffffffffffffffffffffffff", "0s44we87oiylpimjg5u2ca1ypr"},
    };
    size_t index;

    for (index = 0; index < sizeof(cases) / sizeof(cases[0]); ++index) {
        uint8_t entropy[PIKAID_RANDOMNESS_LENGTH];
        char text[PIKAID_STRING_SIZE];

        CHECK(pikaid_test_hex_decode(cases[index].entropy_hex, entropy,
                                     sizeof(entropy)));
        CHECK_STATUS(pikaid_encode(1700000000, entropy, text), PIKAID_OK);
        CHECK_STRING(text, cases[index].expected_id);
        CHECK(pikaid_is_valid(text, PIKAID_STRING_LENGTH));

        if (index > 0) {
            CHECK(strcmp(cases[index - 1].expected_id,
                         cases[index].expected_id) < 0);
            {
                int comparison = 0;
                CHECK_STATUS(pikaid_compare_strings(
                                 cases[index - 1].expected_id, text,
                                 &comparison),
                             PIKAID_OK);
                CHECK(comparison == -1);
            }
        }
    }
}

/*
 * Generation within one second is not monotonic. This is a property of the
 * design (randomness is independent), not a defect, and the README says so.
 */
static void check_within_second_not_monotonic(void)
{
    char previous[PIKAID_STRING_SIZE];
    unsigned int iteration;
    unsigned int inversions = 0;

    CHECK_STATUS(pikaid_generate_at(1700000000, previous), PIKAID_OK);
    for (iteration = 0; iteration < 64; ++iteration) {
        char current[PIKAID_STRING_SIZE];

        CHECK_STATUS(pikaid_generate_at(1700000000, current), PIKAID_OK);
        CHECK(pikaid_is_valid(current, PIKAID_STRING_LENGTH));
        if (strcmp(current, previous) < 0) {
            ++inversions;
        }
        memcpy(previous, current, sizeof(previous));
    }
    /* With 64 independent 96-bit values, an inversion is certain in practice. */
    CHECK(inversions > 0);
}

static void check_boundaries(void)
{
    static const uint64_t timestamps[] = {
        UINT64_C(0), UINT64_C(1), UINT64_C(35), UINT64_C(36),
        UINT64_C(2176782336), PIKAID_TIMESTAMP_MAX - UINT64_C(1),
        PIKAID_TIMESTAMP_MAX};
    static const struct {
        const char *hex;
    } entropies[] = {
        {"000000000000000000000000"},
        {"000000000000000000000001"},
        {"000000000000000000000023"},
        {"000000000000000000000024"},
        {"2153e468b91c6d1000000000"},
        {"ffffffffffffffffffffffff"},
    };
    size_t timestamp_index;
    size_t entropy_index;

    for (timestamp_index = 0;
         timestamp_index < sizeof(timestamps) / sizeof(timestamps[0]);
         ++timestamp_index) {
        for (entropy_index = 0;
             entropy_index < sizeof(entropies) / sizeof(entropies[0]);
             ++entropy_index) {
            uint8_t entropy[PIKAID_RANDOMNESS_LENGTH];
            pikaid_t decoded;
            char text[PIKAID_STRING_SIZE];

            CHECK(pikaid_test_hex_decode(entropies[entropy_index].hex, entropy,
                                         sizeof(entropy)));
            CHECK_STATUS(pikaid_encode(timestamps[timestamp_index], entropy,
                                       text),
                         PIKAID_OK);
            CHECK_STATUS(pikaid_parse(text, PIKAID_STRING_LENGTH, &decoded),
                         PIKAID_OK);
            CHECK_U64(decoded.timestamp, timestamps[timestamp_index]);
            CHECK_BYTES(decoded.randomness, entropy, sizeof(entropy));
            CHECK(pikaid_is_valid(text, PIKAID_STRING_LENGTH));
        }
    }
}

static void check_out_of_range_timestamps(void)
{
    uint8_t entropy[PIKAID_RANDOMNESS_LENGTH] = {0};
    char text[PIKAID_STRING_SIZE];
    uint8_t binary[PIKAID_BINARY_LENGTH];
    pikaid_t value;
    uint64_t extracted = 0;

    CHECK_STATUS(pikaid_encode(PIKAID_TIMESTAMP_MAX + UINT64_C(1), entropy,
                               text),
                 PIKAID_ERR_TIMESTAMP);
    CHECK(text[0] == '\0');
    CHECK_STATUS(pikaid_encode_binary(PIKAID_TIMESTAMP_MAX + UINT64_C(1),
                                      entropy, binary),
                 PIKAID_ERR_TIMESTAMP);
    {
        size_t index;
        for (index = 0; index < sizeof(binary); ++index) {
            CHECK(binary[index] == 0);
        }
    }

    memset(&value, 0, sizeof(value));
    value.timestamp = PIKAID_TIMESTAMP_MAX + UINT64_C(1);
    CHECK_STATUS(pikaid_format(&value, text), PIKAID_ERR_TIMESTAMP);
    CHECK_STATUS(pikaid_to_binary(&value, binary), PIKAID_ERR_TIMESTAMP);
    CHECK_STATUS(pikaid_get_timestamp(&value, &extracted),
                 PIKAID_ERR_TIMESTAMP);
}

static void check_buffer_aliasing(void)
{
    static const uint8_t randomness[PIKAID_RANDOMNESS_LENGTH] = {
        0x40, 0x41, 0x42, 0x43, 0x44, 0x45,
        0x46, 0x47, 0x48, 0x49, 0x4a, 0x4b
    };
    pikaid_t value;
    pikaid_t aliased_value;
    char text[PIKAID_STRING_SIZE];
    char expected_text[PIKAID_STRING_SIZE];
    uint8_t binary[PIKAID_BINARY_LENGTH];
    uint8_t expected_binary[PIKAID_BINARY_LENGTH];
    uint8_t overlapping[PIKAID_BINARY_LENGTH];

    /* Formatting into a buffer that already holds the source is safe. */
    CHECK_STATUS(pikaid_parse("0s44we8002loz3po6mcf6vsn23",
                              PIKAID_STRING_LENGTH, &value),
                 PIKAID_OK);
    CHECK_STATUS(pikaid_format(&value, text), PIKAID_OK);
    CHECK_STATUS(pikaid_format(&value, text), PIKAID_OK);
    CHECK_STRING(text, "0s44we8002loz3po6mcf6vsn23");

    CHECK_STATUS(pikaid_to_binary(&value, binary), PIKAID_OK);
    CHECK_STATUS(pikaid_to_binary(&value, binary), PIKAID_OK);
    CHECK_STATUS(pikaid_decode_binary(binary, sizeof(binary), &value),
                 PIKAID_OK);
    CHECK_U64(value.timestamp, 1700000000);

    /* The binary encoder must accept out == randomness without aliasing UB. */
    memset(overlapping, 0, sizeof(overlapping));
    memcpy(overlapping, randomness, sizeof(randomness));
    CHECK_STATUS(pikaid_encode_binary(UINT64_C(1700000000), overlapping,
                                      overlapping),
                 PIKAID_OK);
    CHECK_STATUS(pikaid_encode_binary(UINT64_C(1700000000), randomness,
                                      expected_binary),
                 PIKAID_OK);
    CHECK_BYTES(overlapping, expected_binary, sizeof(overlapping));

    /* The string encoder must read randomness before overwriting the buffer. */
    memset(text, 0, sizeof(text));
    memcpy(text, randomness, sizeof(randomness));
    CHECK_STATUS(pikaid_encode(UINT64_C(1700000000),
                               (const uint8_t *)text, text),
                 PIKAID_OK);
    CHECK_STATUS(pikaid_encode(UINT64_C(1700000000), randomness,
                               expected_text),
                 PIKAID_OK);
    CHECK_STRING(text, expected_text);

    /* The binary decoder must read all input before writing *out. */
    CHECK_STATUS(pikaid_encode_binary(UINT64_C(1900000000), randomness,
                                      expected_binary),
                 PIKAID_OK);
    memcpy(&aliased_value, expected_binary, sizeof(expected_binary));
    CHECK_STATUS(pikaid_decode_binary((const uint8_t *)&aliased_value,
                                      sizeof(expected_binary),
                                      &aliased_value),
                 PIKAID_OK);
    CHECK_U64(aliased_value.timestamp, UINT64_C(1900000000));
    CHECK_BYTES(aliased_value.randomness, randomness, sizeof(randomness));
}

int main(void)
{
    pikaid_test_random_seed(UINT64_C(0xC0FFEE123456789A));
    check_random_round_trips();
    check_ordering_agreement();
    check_cross_second_ordering();
    check_same_second_ordering();
    check_within_second_not_monotonic();
    check_boundaries();
    check_out_of_range_timestamps();
    check_buffer_aliasing();
    return pikaid_test_summary("test_properties");
}
