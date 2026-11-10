/*
 * pikaid — system clock.
 *
 * The library reads whole Unix seconds through the C standard library. It does
 * not attempt sub-second precision: the wire format stores seconds only, and
 * advertising more precision would be wrong.
 *
 * SPDX-License-Identifier: MIT
 */

#include "pikaid_internal.h"

#include <time.h>

pikaid_status pikaid_now(uint64_t *timestamp)
{
    time_t now;

    if (timestamp == NULL) {
        return PIKAID_ERR_NULL_ARGUMENT;
    }

    now = time(NULL);
    if (now == (time_t)-1) {
        return PIKAID_ERR_CLOCK;
    }
    if (now < (time_t)0) {
        /* A pre-1970 clock cannot be represented as unsigned Unix seconds. */
        return PIKAID_ERR_TIMESTAMP;
    }

    *timestamp = (uint64_t)now;
    return PIKAID_OK;
}
