/*
 * Fault-injection tests for the entropy and clock boundaries.
 *
 * This executable is linked against src/pikaid.c only; it provides its own
 * definitions of pikaid_entropy_fill() and pikaid_now() instead of the
 * platform translation units. That keeps the injected behaviour out of the
 * installed library and out of the public header.
 *
 * Exactly one PIKAID_TEST_MODE_* macro must be defined by the build system.
 *
 * SPDX-License-Identifier: MIT
 */

#include "pikaid_internal.h"
#include "test_common.h"

#include <string.h>

#if !defined(PIKAID_TEST_MODE_FIXED) &&                                   \
    !defined(PIKAID_TEST_MODE_ENTROPY_FAILURE) &&                         \
    !defined(PIKAID_TEST_MODE_CLOCK_FAILURE) &&                           \
    !defined(PIKAID_TEST_MODE_CLOCK_OUT_OF_RANGE)
#error "define exactly one PIKAID_TEST_MODE_* macro"
#endif

#if !defined(PIKAID_TEST_MODE_ENTROPY_FAILURE)
static const uint8_t fixed_entropy[PIKAID_RANDOMNESS_LENGTH] = {
    0xA5, 0xA4, 0xA7, 0xA6, 0xA1, 0xA0,
    0xA3, 0xA2, 0xAD, 0xAC, 0xAF, 0xAE};
#endif

#if !defined(PIKAID_TEST_MODE_CLOCK_OUT_OF_RANGE)
static const uint64_t fixed_time = UINT64_C(1700000000);
#endif

/* Expected values derived independently with Python integers. */
#if defined(PIKAID_TEST_MODE_FIXED) || defined(PIKAID_TEST_MODE_CLOCK_FAILURE)
static const char *const expected_id = "0s44we84yxam0o8mkx4koc3j5a";
#endif
#if defined(PIKAID_TEST_MODE_FIXED)
static const uint8_t expected_binary[PIKAID_BINARY_LENGTH] = {
    0x00, 0x65, 0x53, 0xF1, 0x00, 0xA5, 0xA4, 0xA7, 0xA6,
    0xA1, 0xA0, 0xA3, 0xA2, 0xAD, 0xAC, 0xAF, 0xAE};
#endif

static unsigned int entropy_calls = 0;
static unsigned int clock_calls = 0;

pikaid_status pikaid_entropy_fill(uint8_t *buffer, size_t length)
{
    ++entropy_calls;

    if (buffer == NULL) {
        return PIKAID_ERR_NULL_ARGUMENT;
    }

#if defined(PIKAID_TEST_MODE_ENTROPY_FAILURE)
    (void)length;
    return PIKAID_ERR_ENTROPY;
#else
    if (length != PIKAID_RANDOMNESS_LENGTH) {
        return PIKAID_ERR_ENTROPY;
    }
    memcpy(buffer, fixed_entropy, PIKAID_RANDOMNESS_LENGTH);
    return PIKAID_OK;
#endif
}

pikaid_status pikaid_now(uint64_t *timestamp)
{
    ++clock_calls;

    if (timestamp == NULL) {
        return PIKAID_ERR_NULL_ARGUMENT;
    }

#if defined(PIKAID_TEST_MODE_CLOCK_FAILURE)
    return PIKAID_ERR_CLOCK;
#elif defined(PIKAID_TEST_MODE_CLOCK_OUT_OF_RANGE)
    *timestamp = PIKAID_TIMESTAMP_MAX + UINT64_C(1);
    return PIKAID_OK;
#else
    *timestamp = fixed_time;
    return PIKAID_OK;
#endif
}

#if defined(PIKAID_TEST_MODE_FIXED)
static void check_fixed_entropy(void)
{
    char text[PIKAID_STRING_SIZE];
    uint8_t binary[PIKAID_BINARY_LENGTH];
    pikaid_t decoded;
    unsigned int before;

    before = entropy_calls;
    CHECK_STATUS(pikaid_generate_at(fixed_time, text), PIKAID_OK);
    CHECK_STRING(text, expected_id);
    CHECK(entropy_calls == before + 1);

    CHECK_STATUS(pikaid_generate_binary_at(fixed_time, binary), PIKAID_OK);
    CHECK_BYTES(binary, expected_binary, sizeof(binary));

    /* Round-trip through the public parsing path. */
    CHECK_STATUS(pikaid_parse(expected_id, PIKAID_STRING_LENGTH, &decoded),
                 PIKAID_OK);
    CHECK_U64(decoded.timestamp, fixed_time);
    CHECK_BYTES(decoded.randomness, fixed_entropy, PIKAID_RANDOMNESS_LENGTH);

    /* An invalid timestamp must not consume entropy. */
    before = entropy_calls;
    memset(text, 'Z', sizeof(text));
    CHECK_STATUS(pikaid_generate_at(PIKAID_TIMESTAMP_MAX + UINT64_C(1), text),
                 PIKAID_ERR_TIMESTAMP);
    CHECK(text[0] == '\0');
    CHECK(entropy_calls == before);

    memset(binary, 0xFF, sizeof(binary));
    CHECK_STATUS(pikaid_generate_binary_at(UINT64_MAX, binary),
                 PIKAID_ERR_TIMESTAMP);
    CHECK(entropy_calls == before);
    {
        size_t index;
        for (index = 0; index < sizeof(binary); ++index) {
            CHECK(binary[index] == 0);
        }
    }
}

static void check_fixed_clock(void)
{
    char text[PIKAID_STRING_SIZE];
    uint8_t binary[PIKAID_BINARY_LENGTH];
    unsigned int before;

    before = clock_calls;
    CHECK_STATUS(pikaid_generate(text), PIKAID_OK);
    CHECK_STRING(text, expected_id);
    CHECK(clock_calls == before + 1);

    before = clock_calls;
    CHECK_STATUS(pikaid_generate_binary(binary), PIKAID_OK);
    CHECK_BYTES(binary, expected_binary, sizeof(binary));
    CHECK(clock_calls == before + 1);
}
#endif

#if defined(PIKAID_TEST_MODE_ENTROPY_FAILURE)
static void check_entropy_failure(void)
{
    char text[PIKAID_STRING_SIZE];
    uint8_t binary[PIKAID_BINARY_LENGTH];
    size_t index;

    memset(text, 'A', sizeof(text));
    CHECK_STATUS(pikaid_generate_at(fixed_time, text), PIKAID_ERR_ENTROPY);
    CHECK(text[0] == '\0');

    memset(text, 'A', sizeof(text));
    CHECK_STATUS(pikaid_generate(text), PIKAID_ERR_ENTROPY);
    CHECK(text[0] == '\0');

    memset(binary, 0xFF, sizeof(binary));
    CHECK_STATUS(pikaid_generate_binary_at(fixed_time, binary),
                 PIKAID_ERR_ENTROPY);
    for (index = 0; index < sizeof(binary); ++index) {
        CHECK(binary[index] == 0);
    }

    memset(binary, 0xFF, sizeof(binary));
    CHECK_STATUS(pikaid_generate_binary(binary), PIKAID_ERR_ENTROPY);
    for (index = 0; index < sizeof(binary); ++index) {
        CHECK(binary[index] == 0);
    }

    /* Timestamp validation happens before entropy is requested. */
    {
        unsigned int before = entropy_calls;
        CHECK_STATUS(pikaid_generate_at(PIKAID_TIMESTAMP_MAX + UINT64_C(1),
                                        text),
                     PIKAID_ERR_TIMESTAMP);
        CHECK(entropy_calls == before);
    }
}
#endif

#if defined(PIKAID_TEST_MODE_CLOCK_FAILURE)
static void check_clock_failure(void)
{
    char text[PIKAID_STRING_SIZE];
    uint8_t binary[PIKAID_BINARY_LENGTH];
    size_t index;

    memset(text, 'B', sizeof(text));
    CHECK_STATUS(pikaid_generate(text), PIKAID_ERR_CLOCK);
    CHECK(text[0] == '\0');

    memset(binary, 0xFF, sizeof(binary));
    CHECK_STATUS(pikaid_generate_binary(binary), PIKAID_ERR_CLOCK);
    for (index = 0; index < sizeof(binary); ++index) {
        CHECK(binary[index] == 0);
    }

    /* Explicit timestamps bypass the clock but still use entropy. */
    CHECK_STATUS(pikaid_generate_at(fixed_time, text), PIKAID_OK);
    CHECK_STRING(text, expected_id);
}
#endif

#if defined(PIKAID_TEST_MODE_CLOCK_OUT_OF_RANGE)
static void check_clock_out_of_range(void)
{
    char text[PIKAID_STRING_SIZE];
    uint8_t binary[PIKAID_BINARY_LENGTH];
    unsigned int before;
    size_t index;

    memset(text, 'C', sizeof(text));
    before = entropy_calls;
    CHECK_STATUS(pikaid_generate(text), PIKAID_ERR_TIMESTAMP);
    CHECK(text[0] == '\0');
    CHECK(entropy_calls == before);

    memset(binary, 0xFF, sizeof(binary));
    before = entropy_calls;
    CHECK_STATUS(pikaid_generate_binary(binary), PIKAID_ERR_TIMESTAMP);
    for (index = 0; index < sizeof(binary); ++index) {
        CHECK(binary[index] == 0);
    }
    CHECK(entropy_calls == before);
}
#endif

#if defined(PIKAID_TEST_MODE_FIXED)
#define SUITE "test_injection_fixed"
#elif defined(PIKAID_TEST_MODE_ENTROPY_FAILURE)
#define SUITE "test_injection_entropy_failure"
#elif defined(PIKAID_TEST_MODE_CLOCK_FAILURE)
#define SUITE "test_injection_clock_failure"
#else
#define SUITE "test_injection_clock_out_of_range"
#endif

int main(void)
{
#if defined(PIKAID_TEST_MODE_FIXED)
    check_fixed_entropy();
    check_fixed_clock();
#elif defined(PIKAID_TEST_MODE_ENTROPY_FAILURE)
    check_entropy_failure();
#elif defined(PIKAID_TEST_MODE_CLOCK_FAILURE)
    check_clock_failure();
#else
    check_clock_out_of_range();
#endif
    return pikaid_test_summary(SUITE);
}
