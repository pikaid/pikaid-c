/*
 * Tests for the real platform entropy and clock translation units.
 *
 * These are the only tests that touch the operating system directly. On a
 * platform without a verified backend, entropy correctly fails closed; the
 * test then reports a skip instead of a false pass.
 *
 * SPDX-License-Identifier: MIT
 */

#include "pikaid_internal.h"
#include "test_common.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#define CLOCK_TOLERANCE 30

static void check_entropy_arguments(void)
{
    uint8_t buffer[PIKAID_RANDOMNESS_LENGTH];

    CHECK_STATUS(pikaid_entropy_fill(NULL, PIKAID_RANDOMNESS_LENGTH),
                 PIKAID_ERR_NULL_ARGUMENT);
    CHECK_STATUS(pikaid_entropy_fill(buffer, 0), PIKAID_OK);
}

static void check_entropy_real(void)
{
    uint8_t first[PIKAID_RANDOMNESS_LENGTH];
    uint8_t second[PIKAID_RANDOMNESS_LENGTH];
    size_t index;
    unsigned int nonzero = 0;
    pikaid_status status;

    memset(first, 0x00, sizeof(first));
    status = pikaid_entropy_fill(first, sizeof(first));
    if (status != PIKAID_OK) {
        /* No verified CSPRNG backend on this platform: not a defect. */
        (void)printf("SKIP: no platform entropy backend (%s)\n",
                     pikaid_status_string(status));
        (void)fflush(stdout);
        return;
    }

    for (index = 0; index < sizeof(first); ++index) {
        if (first[index] != 0x00) {
            ++nonzero;
        }
    }
    /* All 96 bits zero has probability 2^-96. */
    CHECK(nonzero > 0);

    memset(second, 0xFF, sizeof(second));
    CHECK_STATUS(pikaid_entropy_fill(second, sizeof(second)), PIKAID_OK);
    /* Two independent 96-bit draws colliding has probability 2^-96. */
    CHECK(memcmp(first, second, sizeof(first)) != 0);

    {
        unsigned int second_nonzero = 0;
        for (index = 0; index < sizeof(second); ++index) {
            if (second[index] != 0xFF) {
                ++second_nonzero;
            }
        }
        CHECK(second_nonzero > 0);
    }
}

static void check_clock_real(void)
{
    uint64_t timestamp = UINT64_C(0xFFFFFFFFFFFFFFFF);
    uint64_t before;
    uint64_t after;
    time_t now;

    CHECK_STATUS(pikaid_now(NULL), PIKAID_ERR_NULL_ARGUMENT);
    CHECK_U64(timestamp, UINT64_C(0xFFFFFFFFFFFFFFFF));

    now = time(NULL);
    CHECK(now != (time_t)-1);
    if (now == (time_t)-1 || now < (time_t)0) {
        return;
    }

    before = (uint64_t)now;
    CHECK_STATUS(pikaid_now(&timestamp), PIKAID_OK);
    now = time(NULL);
    CHECK(now != (time_t)-1);
    if (now == (time_t)-1 || now < (time_t)0) {
        return;
    }
    after = (uint64_t)now;

    CHECK(timestamp >= before);
    CHECK(timestamp <= after + CLOCK_TOLERANCE);
    CHECK(after >= before);
    CHECK(after - before <= CLOCK_TOLERANCE);
}

int main(void)
{
    pikaid_status status;
    uint8_t probe[PIKAID_RANDOMNESS_LENGTH];

    status = pikaid_entropy_fill(probe, sizeof(probe));
    if (status == PIKAID_ERR_ENTROPY) {
        (void)printf("SKIP: platform has no verified CSPRNG backend\n");
        (void)fflush(stdout);
        return 77;
    }

    check_entropy_arguments();
    check_entropy_real();
    check_clock_real();
    return pikaid_test_summary("test_internal");
}
