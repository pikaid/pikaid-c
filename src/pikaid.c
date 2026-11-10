/*
 * pikaid — official C implementation: canonical encoding, decoding and
 * validation.
 *
 * SPDX-License-Identifier: MIT
 */

#include "pikaid.h"
#include "pikaid_internal.h"

#include <string.h>

static const char pikaid_alphabet[] = "0123456789abcdefghijklmnopqrstuvwxyz";

/* Returns 0..35 for a canonical Base36 character, or -1. */
static int pikaid_digit_value(unsigned char character)
{
    if (character >= (unsigned char)'0' && character <= (unsigned char)'9') {
        return (int)(character - (unsigned char)'0');
    }
    if (character >= (unsigned char)'a' && character <= (unsigned char)'z') {
        return (int)(character - (unsigned char)'a') + 10;
    }
    return -1;
}

static void pikaid_clear_string(char *out)
{
    if (out != NULL) {
        out[0] = '\0';
    }
}

static void pikaid_clear_binary(uint8_t *out)
{
    if (out != NULL) {
        memset(out, 0, PIKAID_BINARY_LENGTH);
    }
}

/* Encode 0..36^7-1 as exactly seven lowercase Base36 digits. */
static void pikaid_encode_timestamp(uint64_t timestamp,
                                    char out[PIKAID_TIMESTAMP_DIGITS])
{
    size_t index;

    for (index = PIKAID_TIMESTAMP_DIGITS; index > 0; --index) {
        out[index - 1] = pikaid_alphabet[timestamp % 36u];
        timestamp /= 36u;
    }
}

/* Decode seven pre-validated Base36 digits. The result cannot exceed 36^7-1. */
static uint64_t pikaid_decode_timestamp(const char *text)
{
    uint64_t timestamp = 0;
    size_t index;

    for (index = 0; index < PIKAID_TIMESTAMP_DIGITS; ++index) {
        timestamp = timestamp * 36u +
                    (uint64_t)pikaid_digit_value((unsigned char)text[index]);
    }
    return timestamp;
}

/*
 * Encode a 96-bit big-endian value as exactly 19 Base36 digits.
 *
 * 2^96 - 1 < 36^19, so 19 digits always suffice. Repeated long division on a
 * fixed-size byte array avoids 128-bit integer requirements and remains
 * portable to any C99 compiler.
 */
static void pikaid_encode_randomness(
    const uint8_t randomness[PIKAID_RANDOMNESS_LENGTH],
    char out[PIKAID_RANDOMNESS_DIGITS])
{
    uint8_t work[PIKAID_RANDOMNESS_LENGTH];
    size_t digit;

    memcpy(work, randomness, sizeof(work));

    for (digit = PIKAID_RANDOMNESS_DIGITS; digit > 0; --digit) {
        uint32_t remainder = 0;
        size_t index;

        for (index = 0; index < PIKAID_RANDOMNESS_LENGTH; ++index) {
            uint32_t value = (remainder << 8) | (uint32_t)work[index];

            work[index] = (uint8_t)(value / 36u);
            remainder = value % 36u;
        }
        out[digit - 1] = pikaid_alphabet[remainder];
    }
}

/*
 * Decode 19 pre-validated Base36 digits into exactly 12 bytes.
 *
 * Returns false when the numeric value does not fit in 96 bits. This is the
 * only semantic check a 26-character string can still fail after its length
 * and alphabet have been accepted.
 */
static bool pikaid_decode_randomness(const char *text,
                                     uint8_t out[PIKAID_RANDOMNESS_LENGTH])
{
    uint8_t work[PIKAID_RANDOMNESS_LENGTH];
    size_t digit;

    memset(work, 0, sizeof(work));

    for (digit = 0; digit < PIKAID_RANDOMNESS_DIGITS; ++digit) {
        uint32_t carry =
            (uint32_t)pikaid_digit_value((unsigned char)text[digit]);
        size_t index;

        for (index = PIKAID_RANDOMNESS_LENGTH; index > 0; --index) {
            uint32_t value = (uint32_t)work[index - 1] * 36u + carry;

            work[index - 1] = (uint8_t)(value & 0xffu);
            carry = value >> 8;
        }
        if (carry != 0) {
            return false;
        }
    }

    memcpy(out, work, sizeof(work));
    return true;
}

static void pikaid_write_timestamp(uint64_t timestamp,
                                   uint8_t out[PIKAID_TIMESTAMP_BYTES])
{
    out[0] = (uint8_t)(timestamp >> 32);
    out[1] = (uint8_t)(timestamp >> 24);
    out[2] = (uint8_t)(timestamp >> 16);
    out[3] = (uint8_t)(timestamp >> 8);
    out[4] = (uint8_t)timestamp;
}

static uint64_t pikaid_read_timestamp(const uint8_t in[PIKAID_TIMESTAMP_BYTES])
{
    return ((uint64_t)in[0] << 32) | ((uint64_t)in[1] << 24) |
           ((uint64_t)in[2] << 16) | ((uint64_t)in[3] << 8) | (uint64_t)in[4];
}

static bool pikaid_timestamp_is_valid(uint64_t timestamp)
{
    return timestamp <= PIKAID_TIMESTAMP_MAX;
}

pikaid_status pikaid_encode(uint64_t timestamp,
                            const uint8_t randomness[PIKAID_RANDOMNESS_LENGTH],
                            char out[PIKAID_STRING_SIZE])
{
    uint8_t staged[PIKAID_RANDOMNESS_LENGTH];

    if (out == NULL || randomness == NULL) {
        return PIKAID_ERR_NULL_ARGUMENT;
    }
    if (!pikaid_timestamp_is_valid(timestamp)) {
        pikaid_clear_string(out);
        return PIKAID_ERR_TIMESTAMP;
    }

    /*
     * Copy the randomness before writing anything: out may alias randomness,
     * and writing the timestamp first would otherwise corrupt the input.
     */
    memcpy(staged, randomness, sizeof(staged));
    pikaid_encode_timestamp(timestamp, out);
    pikaid_encode_randomness(staged, out + PIKAID_TIMESTAMP_DIGITS);
    out[PIKAID_STRING_LENGTH] = '\0';
    return PIKAID_OK;
}

pikaid_status pikaid_encode_binary(
    uint64_t timestamp, const uint8_t randomness[PIKAID_RANDOMNESS_LENGTH],
    uint8_t out[PIKAID_BINARY_LENGTH])
{
    uint8_t staged[PIKAID_RANDOMNESS_LENGTH];

    if (out == NULL || randomness == NULL) {
        return PIKAID_ERR_NULL_ARGUMENT;
    }
    if (!pikaid_timestamp_is_valid(timestamp)) {
        pikaid_clear_binary(out);
        return PIKAID_ERR_TIMESTAMP;
    }

    /*
     * Staging also avoids memcpy overlap when out and randomness are the same
     * buffer (or partially overlap).
     */
    memcpy(staged, randomness, sizeof(staged));
    pikaid_write_timestamp(timestamp, out);
    memcpy(out + PIKAID_TIMESTAMP_BYTES, staged, sizeof(staged));
    return PIKAID_OK;
}

pikaid_status pikaid_generate_at(uint64_t timestamp,
                                 char out[PIKAID_STRING_SIZE])
{
    uint8_t randomness[PIKAID_RANDOMNESS_LENGTH] = {0};
    pikaid_status status;

    if (out == NULL) {
        return PIKAID_ERR_NULL_ARGUMENT;
    }
    if (!pikaid_timestamp_is_valid(timestamp)) {
        pikaid_clear_string(out);
        return PIKAID_ERR_TIMESTAMP;
    }

    status = pikaid_entropy_fill(randomness, sizeof(randomness));
    if (status != PIKAID_OK) {
        pikaid_clear_string(out);
        return PIKAID_ERR_ENTROPY;
    }

    return pikaid_encode(timestamp, randomness, out);
}

pikaid_status pikaid_generate_binary_at(uint64_t timestamp,
                                        uint8_t out[PIKAID_BINARY_LENGTH])
{
    uint8_t randomness[PIKAID_RANDOMNESS_LENGTH] = {0};
    pikaid_status status;

    if (out == NULL) {
        return PIKAID_ERR_NULL_ARGUMENT;
    }
    if (!pikaid_timestamp_is_valid(timestamp)) {
        pikaid_clear_binary(out);
        return PIKAID_ERR_TIMESTAMP;
    }

    status = pikaid_entropy_fill(randomness, sizeof(randomness));
    if (status != PIKAID_OK) {
        pikaid_clear_binary(out);
        return PIKAID_ERR_ENTROPY;
    }

    return pikaid_encode_binary(timestamp, randomness, out);
}

pikaid_status pikaid_generate(char out[PIKAID_STRING_SIZE])
{
    uint64_t timestamp = 0;
    pikaid_status status;

    if (out == NULL) {
        return PIKAID_ERR_NULL_ARGUMENT;
    }

    status = pikaid_now(&timestamp);
    if (status != PIKAID_OK) {
        pikaid_clear_string(out);
        return status;
    }

    return pikaid_generate_at(timestamp, out);
}

pikaid_status pikaid_generate_binary(uint8_t out[PIKAID_BINARY_LENGTH])
{
    uint64_t timestamp = 0;
    pikaid_status status;

    if (out == NULL) {
        return PIKAID_ERR_NULL_ARGUMENT;
    }

    status = pikaid_now(&timestamp);
    if (status != PIKAID_OK) {
        pikaid_clear_binary(out);
        return status;
    }

    return pikaid_generate_binary_at(timestamp, out);
}

pikaid_status pikaid_parse(const char *text, size_t length, pikaid_t *out)
{
    uint8_t randomness[PIKAID_RANDOMNESS_LENGTH];
    uint64_t timestamp;
    size_t index;

    if (text == NULL || out == NULL) {
        return PIKAID_ERR_NULL_ARGUMENT;
    }
    if (length != PIKAID_STRING_LENGTH) {
        return PIKAID_ERR_LENGTH;
    }
    for (index = 0; index < PIKAID_STRING_LENGTH; ++index) {
        if (pikaid_digit_value((unsigned char)text[index]) < 0) {
            return PIKAID_ERR_CHARACTER;
        }
    }

    timestamp = pikaid_decode_timestamp(text);
    if (!pikaid_decode_randomness(text + PIKAID_TIMESTAMP_DIGITS, randomness)) {
        return PIKAID_ERR_RANDOMNESS;
    }

    out->timestamp = timestamp;
    memcpy(out->randomness, randomness, PIKAID_RANDOMNESS_LENGTH);
    return PIKAID_OK;
}

pikaid_status pikaid_parse_cstr(const char *text, pikaid_t *out)
{
    size_t length = 0;

    if (text == NULL || out == NULL) {
        return PIKAID_ERR_NULL_ARGUMENT;
    }
    while (length < PIKAID_STRING_SIZE && text[length] != '\0') {
        ++length;
    }
    return pikaid_parse(text, length, out);
}

pikaid_status pikaid_decode_binary(const uint8_t *binary, size_t length,
                                   pikaid_t *out)
{
    uint8_t randomness[PIKAID_RANDOMNESS_LENGTH];
    uint64_t timestamp;

    if (binary == NULL || out == NULL) {
        return PIKAID_ERR_NULL_ARGUMENT;
    }
    if (length != PIKAID_BINARY_LENGTH) {
        return PIKAID_ERR_LENGTH;
    }

    /*
     * Read the complete input before writing *out: binary may alias out, in
     * which case writing the timestamp would corrupt the randomness bytes.
     */
    memcpy(randomness, binary + PIKAID_TIMESTAMP_BYTES, sizeof(randomness));
    timestamp = pikaid_read_timestamp(binary);
    if (!pikaid_timestamp_is_valid(timestamp)) {
        return PIKAID_ERR_TIMESTAMP;
    }

    out->timestamp = timestamp;
    memcpy(out->randomness, randomness, sizeof(randomness));
    return PIKAID_OK;
}

pikaid_status pikaid_format(const pikaid_t *id, char out[PIKAID_STRING_SIZE])
{
    if (id == NULL || out == NULL) {
        return PIKAID_ERR_NULL_ARGUMENT;
    }
    return pikaid_encode(id->timestamp, id->randomness, out);
}

pikaid_status pikaid_to_binary(const pikaid_t *id,
                               uint8_t out[PIKAID_BINARY_LENGTH])
{
    if (id == NULL || out == NULL) {
        return PIKAID_ERR_NULL_ARGUMENT;
    }
    return pikaid_encode_binary(id->timestamp, id->randomness, out);
}

pikaid_status pikaid_compare(const pikaid_t *a, const pikaid_t *b, int *result)
{
    int difference;

    if (a == NULL || b == NULL || result == NULL) {
        return PIKAID_ERR_NULL_ARGUMENT;
    }
    if (!pikaid_timestamp_is_valid(a->timestamp) ||
        !pikaid_timestamp_is_valid(b->timestamp)) {
        return PIKAID_ERR_TIMESTAMP;
    }

    if (a->timestamp != b->timestamp) {
        *result = (a->timestamp < b->timestamp) ? -1 : 1;
        return PIKAID_OK;
    }

    difference = memcmp(a->randomness, b->randomness,
                        PIKAID_RANDOMNESS_LENGTH);
    *result = (difference > 0) - (difference < 0);
    return PIKAID_OK;
}

pikaid_status pikaid_compare_strings(const char *a, const char *b, int *result)
{
    pikaid_t left;
    pikaid_t right;
    pikaid_status status;

    if (a == NULL || b == NULL || result == NULL) {
        return PIKAID_ERR_NULL_ARGUMENT;
    }

    status = pikaid_parse_cstr(a, &left);
    if (status != PIKAID_OK) {
        return status;
    }
    status = pikaid_parse_cstr(b, &right);
    if (status != PIKAID_OK) {
        return status;
    }

    return pikaid_compare(&left, &right, result);
}

bool pikaid_is_valid(const char *text, size_t length)
{
    pikaid_t decoded;

    return pikaid_parse(text, length, &decoded) == PIKAID_OK;
}

bool pikaid_is_well_formed(const char *text, size_t length)
{
    size_t index;

    if (text == NULL || length != PIKAID_STRING_LENGTH) {
        return false;
    }
    for (index = 0; index < PIKAID_STRING_LENGTH; ++index) {
        if (pikaid_digit_value((unsigned char)text[index]) < 0) {
            return false;
        }
    }
    return true;
}

pikaid_status pikaid_get_timestamp(const pikaid_t *id, uint64_t *timestamp)
{
    if (id == NULL || timestamp == NULL) {
        return PIKAID_ERR_NULL_ARGUMENT;
    }
    if (!pikaid_timestamp_is_valid(id->timestamp)) {
        return PIKAID_ERR_TIMESTAMP;
    }

    *timestamp = id->timestamp;
    return PIKAID_OK;
}

pikaid_status pikaid_get_randomness(
    const pikaid_t *id, uint8_t randomness[PIKAID_RANDOMNESS_LENGTH])
{
    if (id == NULL || randomness == NULL) {
        return PIKAID_ERR_NULL_ARGUMENT;
    }
    if (!pikaid_timestamp_is_valid(id->timestamp)) {
        return PIKAID_ERR_TIMESTAMP;
    }

    memmove(randomness, id->randomness, PIKAID_RANDOMNESS_LENGTH);
    return PIKAID_OK;
}

const char *pikaid_status_string(pikaid_status status)
{
    switch (status) {
    case PIKAID_OK:
        return "ok";
    case PIKAID_ERR_NULL_ARGUMENT:
        return "null argument";
    case PIKAID_ERR_LENGTH:
        return "invalid length";
    case PIKAID_ERR_CHARACTER:
        return "invalid character";
    case PIKAID_ERR_TIMESTAMP:
        return "timestamp out of range";
    case PIKAID_ERR_RANDOMNESS:
        return "randomness exceeds 96 bits";
    case PIKAID_ERR_ENTROPY:
        return "entropy source failure";
    case PIKAID_ERR_CLOCK:
        return "clock failure";
    default:
        return "unknown status";
    }
}
