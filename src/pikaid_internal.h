/*
 * Internal interfaces for the pikaid library.
 *
 * These symbols are not part of the public API. They are separate translation
 * units so the test suite can substitute deterministic or failing entropy and
 * clock sources without exposing test hooks in the installed header.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef PIKAID_INTERNAL_H_INCLUDED
#define PIKAID_INTERNAL_H_INCLUDED

#include "pikaid.h"

/**
 * Fill a caller-owned buffer with cryptographically secure random bytes.
 *
 * Contract: returns PIKAID_OK only when every byte was written from an
 * operating-system CSPRNG. Any failure must return a non-OK status; callers
 * treat every non-OK value as PIKAID_ERR_ENTROPY and discard the identifier.
 */
pikaid_status pikaid_entropy_fill(uint8_t *buffer, size_t length);

/**
 * Read the current Unix time in whole seconds.
 *
 * Contract: returns PIKAID_OK only when *timestamp was written. A system clock
 * failure returns PIKAID_ERR_CLOCK. Range checking belongs to the caller.
 */
pikaid_status pikaid_now(uint64_t *timestamp);

#endif /* PIKAID_INTERNAL_H_INCLUDED */
