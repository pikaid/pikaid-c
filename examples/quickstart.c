/*
 * pikaid quickstart: generate, validate, convert and inspect identifiers.
 *
 * Build with CMake, then run:
 *     ./pikaid_quickstart
 *
 * SPDX-License-Identifier: MIT
 */

#include <pikaid.h>

#include <stdio.h>
#include <string.h>

static int report(const char *operation, pikaid_status status)
{
    if (status == PIKAID_OK) {
        (void)printf("%-24s ok\n", operation);
        return 0;
    }
    (void)printf("%-24s failed: %s\n", operation,
                 pikaid_status_string(status));
    return 1;
}

int main(void)
{
    char id[PIKAID_STRING_SIZE];
    char formatted[PIKAID_STRING_SIZE];
    char identity[PIKAID_STRING_SIZE];
    uint8_t binary[PIKAID_BINARY_LENGTH];
    uint8_t randomness[PIKAID_RANDOMNESS_LENGTH];
    pikaid_t decoded;
    pikaid_t from_binary;
    uint64_t timestamp = 0;
    int comparison = 0;
    int failures = 0;

    /* 1. Generate an identifier with the current time and 96 CSPRNG bits. */
    failures += report("generate", pikaid_generate(id));
    if (failures != 0) {
        return 1;
    }
    (void)printf("%-24s %s\n", "generated", id);

    /* 2. Validate strictly: 26 lowercase Base36 characters, <= 96 bits. */
    if (!pikaid_is_valid(id, strlen(id))) {
        (void)printf("%-24s unexpected failure\n", "is_valid");
        return 1;
    }
    (void)printf("%-24s true\n", "is_valid");

    /* 3. Decode the string form. */
    failures += report("parse", pikaid_parse(id, PIKAID_STRING_LENGTH,
                                             &decoded));

    /* 4. Read the components back through the accessors. */
    failures += report("get_timestamp", pikaid_get_timestamp(&decoded,
                                                             &timestamp));
    failures += report("get_randomness", pikaid_get_randomness(&decoded,
                                                               randomness));
    (void)printf("%-24s %llu\n", "timestamp", (unsigned long long)timestamp);

    /* 5. Convert to the 17-byte binary form and back. */
    failures += report("to_binary", pikaid_to_binary(&decoded, binary));
    failures += report("decode_binary",
                       pikaid_decode_binary(binary, sizeof(binary),
                                            &from_binary));
    failures += report("format", pikaid_format(&from_binary, formatted));
    failures += report("compare", pikaid_compare(&decoded, &from_binary,
                                                 &comparison));
    if (comparison != 0 || strcmp(formatted, id) != 0) {
        (void)printf("%-24s unexpected mismatch\n", "round trip");
        return 1;
    }
    (void)printf("%-24s identical\n", "string/binary round trip");

    /* 6. Build an identifier from known components; useful for imports. */
    failures += report("encode", pikaid_encode(timestamp, randomness,
                                               identity));
    if (strcmp(identity, id) != 0) {
        (void)printf("%-24s unexpected mismatch\n", "encode");
        return 1;
    }

    /* 7. Ordering: ids sort chronologically, then by randomness. */
    {
        pikaid_t earlier;
        pikaid_t later;
        char earlier_id[PIKAID_STRING_SIZE];
        char later_id[PIKAID_STRING_SIZE];
        static const uint8_t zero_randomness[PIKAID_RANDOMNESS_LENGTH] = {0};

        failures += report("encode(0)", pikaid_encode(0, zero_randomness,
                                                      earlier_id));
        failures += report("encode(1700000000)",
                           pikaid_encode(UINT64_C(1700000000),
                                         zero_randomness, later_id));
        failures += report("parse(earlier)",
                           pikaid_parse(earlier_id, PIKAID_STRING_LENGTH,
                                        &earlier));
        failures += report("parse(later)",
                           pikaid_parse(later_id, PIKAID_STRING_LENGTH,
                                        &later));
        failures += report("compare_strings",
                           pikaid_compare_strings(earlier_id, later_id,
                                                  &comparison));
        if (comparison != -1) {
            (void)printf("%-24s unexpected order\n", "ordering");
            return 1;
        }
        (void)printf("%-24s %s < %s\n", "ordering", earlier_id, later_id);
    }

    /* 8. Rejection is explicit; no truncation or normalisation. */
    if (pikaid_is_valid("0000000000000000000000000A", 26)) {
        (void)printf("%-24s unexpected acceptance\n", "invalid input");
        return 1;
    }
    (void)printf("%-24s rejected\n", "invalid input");

    if (failures != 0) {
        return 1;
    }
    (void)printf("\nquickstart completed successfully\n");
    return 0;
}
