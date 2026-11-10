/*
 * pikaid — platform cryptographic entropy.
 *
 * This translation unit contains the only code in the library that talks to a
 * platform CSPRNG. Each supported platform uses its documented operating
 * system interface; any failure is reported, never replaced by a weaker
 * fallback. Platforms without a verified backend compile to a stub that always
 * reports PIKAID_ERR_ENTROPY, so generation fails closed.
 *
 * SPDX-License-Identifier: MIT
 */

/*
 * Request the platform declarations for getrandom()/getentropy(). These must be
 * defined before the first libc header is included; the library is built with
 * strict -std=c99, where they are otherwise hidden.
 */
#if defined(__linux__) && !defined(_DEFAULT_SOURCE)
#define _DEFAULT_SOURCE 1
#endif
#if (defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__)) &&    \
    !defined(_DARWIN_C_SOURCE) && !defined(_DEFAULT_SOURCE)
#define _DARWIN_C_SOURCE 1
#endif

#include "pikaid_internal.h"

#include <limits.h>
#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32)

#include <windows.h>
#include <bcrypt.h>

#if defined(_MSC_VER)
#pragma comment(lib, "bcrypt.lib")
#endif

pikaid_status pikaid_entropy_fill(uint8_t *buffer, size_t length)
{
    NTSTATUS status;

    if (buffer == NULL) {
        return PIKAID_ERR_NULL_ARGUMENT;
    }
    if (length == 0) {
        return PIKAID_OK;
    }
    if (length > (size_t)ULONG_MAX) {
        return PIKAID_ERR_ENTROPY;
    }

    status = BCryptGenRandom(NULL, buffer, (ULONG)length,
                             BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    if (status < 0) {
        return PIKAID_ERR_ENTROPY;
    }
    return PIKAID_OK;
}

#elif defined(__linux__)

#include <errno.h>
#include <sys/random.h>

pikaid_status pikaid_entropy_fill(uint8_t *buffer, size_t length)
{
    size_t filled = 0;
    unsigned int interrupted = 0;

    if (buffer == NULL) {
        return PIKAID_ERR_NULL_ARGUMENT;
    }

    while (filled < length) {
        ssize_t received = getrandom(buffer + filled, length - filled, 0);

        if (received < 0) {
            if (errno == EINTR) {
                /*
                 * Retry interruptions, but bound the loop: a signal storm
                 * must not turn entropy collection into an unbounded wait.
                 * Failing closed is preferable to never returning.
                 */
                if (++interrupted >= 16u) {
                    return PIKAID_ERR_ENTROPY;
                }
                continue;
            }
            return PIKAID_ERR_ENTROPY;
        }
        if (received == 0) {
            return PIKAID_ERR_ENTROPY;
        }
        interrupted = 0;
        filled += (size_t)received;
    }
    return PIKAID_OK;
}

#elif defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__)

/*
 * NetBSD and DragonFly BSD are deliberately absent. Their CSPRNG interfaces
 * are not exercised by this tree, so they compile to the fail-closed stub
 * below rather than an unverified backend claim.
 */

#if defined(__APPLE__)
#include <sys/random.h>
#else
#include <unistd.h>
#endif

/*
 * getentropy() is limited to 256 bytes per call by POSIX. The library only
 * ever requests 12 bytes, but the guard keeps the contract explicit.
 */
#define PIKAID_GETENTROPY_MAX 256

pikaid_status pikaid_entropy_fill(uint8_t *buffer, size_t length)
{
    if (buffer == NULL) {
        return PIKAID_ERR_NULL_ARGUMENT;
    }
    if (length > PIKAID_GETENTROPY_MAX) {
        return PIKAID_ERR_ENTROPY;
    }
    if (length == 0) {
        return PIKAID_OK;
    }

    if (getentropy(buffer, length) != 0) {
        return PIKAID_ERR_ENTROPY;
    }
    return PIKAID_OK;
}

#else

/*
 * No verified CSPRNG binding exists for this platform. Generation fails closed
 * instead of falling back to time(), rand(), a hash of predictable input or
 * /dev/urandom heuristics. Add a documented platform backend before claiming
 * support for a new system.
 */
pikaid_status pikaid_entropy_fill(uint8_t *buffer, size_t length)
{
    /*
     * Keep the same argument contract as the real backends so callers and
     * tests observe identical behavior; only actual entropy requests fail.
     */
    if (buffer == NULL) {
        return PIKAID_ERR_NULL_ARGUMENT;
    }
    if (length == 0) {
        return PIKAID_OK;
    }
    return PIKAID_ERR_ENTROPY;
}

#endif
