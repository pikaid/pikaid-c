/*
 * Official conformance vectors (pikaid-php v1.2.0 fixtures) plus the
 * independent re-derivation performed by tests/vectors/generate_vectors.py.
 *
 * SPDX-License-Identifier: MIT
 */

#include "test_common.h"
#include "vectors/pikaid_vectors.h"

#include <stdio.h>
#include <string.h>

static void check_valid_vectors(void)
{
    size_t index;

    CHECK_SIZE(pikaid_valid_vector_count, 4);

    for (index = 0; index < pikaid_valid_vector_count; ++index) {
        const struct pikaid_valid_vector *vector = &pikaid_valid_vectors[index];
        pikaid_t decoded;
        pikaid_t from_binary;
        char text[PIKAID_STRING_SIZE];
        uint8_t binary[PIKAID_BINARY_LENGTH];
        uint8_t entropy[PIKAID_RANDOMNESS_LENGTH];
        uint64_t timestamp = 0;

        CHECK_SIZE(strlen(vector->id), PIKAID_STRING_LENGTH);
        CHECK(pikaid_is_well_formed(vector->id, strlen(vector->id)));
        CHECK(pikaid_is_valid(vector->id, strlen(vector->id)));

        memset(&decoded, 0xAA, sizeof(decoded));
        CHECK_STATUS(pikaid_parse(vector->id, strlen(vector->id), &decoded),
                     PIKAID_OK);
        CHECK_U64(decoded.timestamp, vector->timestamp);
        CHECK_BYTES(decoded.randomness, vector->entropy,
                    PIKAID_RANDOMNESS_LENGTH);

        CHECK_STATUS(pikaid_parse_cstr(vector->id, &decoded), PIKAID_OK);
        CHECK_U64(decoded.timestamp, vector->timestamp);

        CHECK_STATUS(pikaid_get_timestamp(&decoded, &timestamp), PIKAID_OK);
        CHECK_U64(timestamp, vector->timestamp);
        CHECK_STATUS(pikaid_get_randomness(&decoded, entropy), PIKAID_OK);
        CHECK_BYTES(entropy, vector->entropy, PIKAID_RANDOMNESS_LENGTH);

        memset(text, '?', sizeof(text));
        CHECK_STATUS(pikaid_encode(vector->timestamp, vector->entropy, text),
                     PIKAID_OK);
        CHECK_STRING(text, vector->id);
        CHECK(text[PIKAID_STRING_LENGTH] == '\0');

        memset(binary, 0x5A, sizeof(binary));
        CHECK_STATUS(pikaid_encode_binary(vector->timestamp, vector->entropy,
                                          binary),
                     PIKAID_OK);
        CHECK_BYTES(binary, vector->binary, PIKAID_BINARY_LENGTH);

        CHECK_STATUS(pikaid_format(&decoded, text), PIKAID_OK);
        CHECK_STRING(text, vector->id);

        CHECK_STATUS(pikaid_to_binary(&decoded, binary), PIKAID_OK);
        CHECK_BYTES(binary, vector->binary, PIKAID_BINARY_LENGTH);

        memset(&from_binary, 0x55, sizeof(from_binary));
        CHECK_STATUS(pikaid_decode_binary(vector->binary,
                                          PIKAID_BINARY_LENGTH, &from_binary),
                     PIKAID_OK);
        CHECK_U64(from_binary.timestamp, vector->timestamp);
        CHECK_BYTES(from_binary.randomness, vector->entropy,
                    PIKAID_RANDOMNESS_LENGTH);
        CHECK_STATUS(pikaid_format(&from_binary, text), PIKAID_OK);
        CHECK_STRING(text, vector->id);

        {
            int comparison = 99;
            CHECK_STATUS(pikaid_compare(&decoded, &from_binary, &comparison),
                         PIKAID_OK);
            CHECK(comparison == 0);
        }
    }
}

static void check_invalid_strings(void)
{
    size_t index;

    CHECK_SIZE(pikaid_invalid_string_vector_count, 6);

    for (index = 0; index < pikaid_invalid_string_vector_count; ++index) {
        const struct pikaid_invalid_string_vector *vector =
            &pikaid_invalid_string_vectors[index];
        pikaid_t decoded;

        memset(&decoded, 0x5A, sizeof(decoded));
        CHECK_STATUS(pikaid_parse(vector->id, vector->length, &decoded),
                     vector->expected);
        /* Failure must not modify the output value. */
        CHECK_U64(decoded.timestamp, UINT64_C(0x5A5A5A5A5A5A5A5A));

        CHECK(!pikaid_is_valid(vector->id, vector->length));

        if (vector->expected == PIKAID_ERR_RANDOMNESS) {
            /* Shape is valid; only the 96-bit semantic limit fails. */
            CHECK(pikaid_is_well_formed(vector->id, vector->length));
        } else {
            CHECK(!pikaid_is_well_formed(vector->id, vector->length));
        }

        /* parse_cstr must agree for every canonical-length fixture. */
        if (vector->length <= PIKAID_STRING_LENGTH) {
            CHECK_STATUS(pikaid_parse_cstr(vector->id, &decoded),
                         vector->expected);
        }

        /* No invalid string may compare successfully with a valid one. */
        {
            int comparison = 7;
            CHECK(pikaid_compare_strings(vector->id,
                                         pikaid_valid_vectors[0].id,
                                         &comparison) != PIKAID_OK);
            CHECK(comparison == 7);
        }

    }
}

static void check_invalid_binaries(void)
{
    size_t index;

    CHECK_SIZE(pikaid_invalid_binary_vector_count, 4);

    for (index = 0; index < pikaid_invalid_binary_vector_count; ++index) {
        const struct pikaid_invalid_binary_vector *vector =
            &pikaid_invalid_binary_vectors[index];
        pikaid_t decoded;

        memset(&decoded, 0x33, sizeof(decoded));
        CHECK_STATUS(pikaid_decode_binary(vector->binary, vector->length,
                                          &decoded),
                     vector->expected);
        CHECK_U64(decoded.timestamp, UINT64_C(0x3333333333333333));
    }
}

static void check_meta(void)
{
    CHECK_STRING(PIKAID_VECTOR_SPEC_REVISION, "1.0.4");
    CHECK_STRING(PIKAID_VECTOR_SOURCE_SHA256,
                 "b0c9dfdd063613424414fef5c9065fbd2f81d645d0ab42b34e27cde715613500");
    CHECK_STRING(pikaid_valid_vectors[0].name, "all-zero");
    CHECK_STRING(pikaid_valid_vectors[1].name, "max-values");
    CHECK_STRING(pikaid_valid_vectors[2].name, "sample-1700000000");
    CHECK_STRING(pikaid_valid_vectors[3].name, "ts-1-entropy-1");
    CHECK_SIZE(sizeof(((struct pikaid_valid_vector *)0)->entropy),
               PIKAID_RANDOMNESS_LENGTH);
    CHECK_SIZE(sizeof(((struct pikaid_valid_vector *)0)->binary),
               PIKAID_BINARY_LENGTH);
}

int main(void)
{
    check_meta();
    check_valid_vectors();
    check_invalid_strings();
    check_invalid_binaries();
    return pikaid_test_summary("test_vectors");
}
