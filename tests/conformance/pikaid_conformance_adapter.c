/*
 * pikaid — test-only JSON-Lines adapter for the official conformance suite.
 *
 * This program is not part of the installed library. It speaks protocol v1 of
 * the shared PikaID conformance suite and delegates every result to the real
 * public pikaid API. It deliberately contains no identifier codec, validator,
 * clock or randomness implementation of its own.
 *
 * SPDX-License-Identifier: MIT
 */

#include "pikaid.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ADAPTER_LINE_MAX (1024u * 1024u)
#define ADAPTER_STRING_MAX 4096u

typedef struct {
    char data[ADAPTER_STRING_MAX];
    size_t len;
    int present;
} adapter_string;

typedef struct {
    long long request_id;
    int has_request_id;
    adapter_string operation;

    adapter_string text;
    adapter_string binary_hex;
    adapter_string a;
    adapter_string b;
    adapter_string fault;

    int has_timestamp;
    int timestamp_negative;
    unsigned long long timestamp_value;

    int has_active;
    int active;
} adapter_request;

typedef struct {
    const char *cursor;
    const char *end;
    int error;
} parser;

static void parser_skip_space(parser *p)
{
    while (!p->error && p->cursor < p->end) {
        char c = *p->cursor;
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            p->cursor++;
        } else {
            break;
        }
    }
}

static int parser_take(parser *p, char expected)
{
    parser_skip_space(p);
    if (p->error || p->cursor >= p->end || *p->cursor != expected) {
        p->error = 1;
        return 0;
    }
    p->cursor++;
    return 1;
}

static void string_push(parser *p, adapter_string *s, unsigned char byte)
{
    if (s->len + 1u >= ADAPTER_STRING_MAX) {
        p->error = 1;
        return;
    }
    s->data[s->len] = (char)byte;
    s->len++;
}

static int parser_hex4(parser *p, unsigned *value)
{
    unsigned result = 0;
    unsigned i;

    for (i = 0; i < 4u; i++) {
        char c;
        unsigned digit;

        if (p->cursor >= p->end) {
            p->error = 1;
            return 0;
        }
        c = *p->cursor++;
        if (c >= '0' && c <= '9') {
            digit = (unsigned)(c - '0');
        } else if (c >= 'a' && c <= 'f') {
            digit = (unsigned)(c - 'a') + 10u;
        } else if (c >= 'A' && c <= 'F') {
            digit = (unsigned)(c - 'A') + 10u;
        } else {
            p->error = 1;
            return 0;
        }
        result = (result << 4) | digit;
    }
    *value = result;
    return 1;
}

static void parser_utf8(parser *p, adapter_string *s, unsigned codepoint)
{
    if (codepoint <= 0x7fu) {
        string_push(p, s, (unsigned char)codepoint);
    } else if (codepoint <= 0x7ffu) {
        string_push(p, s, (unsigned char)(0xc0u | (codepoint >> 6)));
        string_push(p, s, (unsigned char)(0x80u | (codepoint & 0x3fu)));
    } else if (codepoint <= 0xffffu) {
        string_push(p, s, (unsigned char)(0xe0u | (codepoint >> 12)));
        string_push(p, s, (unsigned char)(0x80u | ((codepoint >> 6) & 0x3fu)));
        string_push(p, s, (unsigned char)(0x80u | (codepoint & 0x3fu)));
    } else {
        string_push(p, s, (unsigned char)(0xf0u | (codepoint >> 18)));
        string_push(p, s, (unsigned char)(0x80u | ((codepoint >> 12) & 0x3fu)));
        string_push(p, s, (unsigned char)(0x80u | ((codepoint >> 6) & 0x3fu)));
        string_push(p, s, (unsigned char)(0x80u | (codepoint & 0x3fu)));
    }
}

static int parser_string(parser *p, adapter_string *out)
{
    out->len = 0;
    out->present = 1;

    parser_skip_space(p);
    if (p->error || p->cursor >= p->end || *p->cursor != '"') {
        p->error = 1;
        return 0;
    }
    p->cursor++;

    while (p->cursor < p->end) {
        unsigned char c = (unsigned char)*p->cursor++;

        if (c == (unsigned char)'"') {
            if (out->len >= ADAPTER_STRING_MAX) {
                p->error = 1;
                return 0;
            }
            out->data[out->len] = '\0';
            return !p->error;
        }
        if (c == (unsigned char)'\\') {
            char escape;
            if (p->cursor >= p->end) {
                p->error = 1;
                return 0;
            }
            escape = *p->cursor++;
            switch (escape) {
            case '"': string_push(p, out, (unsigned char)'"'); break;
            case '\\': string_push(p, out, (unsigned char)'\\'); break;
            case '/': string_push(p, out, (unsigned char)'/'); break;
            case 'b': string_push(p, out, (unsigned char)'\b'); break;
            case 'f': string_push(p, out, (unsigned char)'\f'); break;
            case 'n': string_push(p, out, (unsigned char)'\n'); break;
            case 'r': string_push(p, out, (unsigned char)'\r'); break;
            case 't': string_push(p, out, (unsigned char)'\t'); break;
            case 'u': {
                unsigned first;
                unsigned codepoint;
                if (!parser_hex4(p, &first)) {
                    return 0;
                }
                codepoint = first;
                if (first >= 0xd800u && first <= 0xdbffu) {
                    unsigned second;
                    if (p->cursor + 2u > p->end ||
                        p->cursor[0] != '\\' || p->cursor[1] != 'u') {
                        p->error = 1;
                        return 0;
                    }
                    p->cursor += 2;
                    if (!parser_hex4(p, &second) ||
                        second < 0xdc00u || second > 0xdfffu) {
                        p->error = 1;
                        return 0;
                    }
                    codepoint = 0x10000u + ((first - 0xd800u) << 10)
                                + (second - 0xdc00u);
                } else if (first >= 0xdc00u && first <= 0xdfffu) {
                    p->error = 1;
                    return 0;
                }
                parser_utf8(p, out, codepoint);
                break;
            }
            default:
                p->error = 1;
                return 0;
            }
            if (p->error) {
                return 0;
            }
            continue;
        }
        if (c < 0x20u) {
            p->error = 1;
            return 0;
        }
        string_push(p, out, c);
        if (p->error) {
            return 0;
        }
    }

    p->error = 1;
    return 0;
}

static int key_is(const adapter_string *key, const char *name)
{
    size_t length = strlen(name);
    return key->len == length && memcmp(key->data, name, length) == 0;
}

static int parser_integer(parser *p, int *negative, unsigned long long *value)
{
    unsigned long long result = 0;
    const unsigned long long limit = ULLONG_MAX;
    int is_negative = 0;
    int saw_digit = 0;

    parser_skip_space(p);
    if (p->error) {
        return 0;
    }
    if (p->cursor < p->end && *p->cursor == '-') {
        is_negative = 1;
        p->cursor++;
    }
    while (p->cursor < p->end && *p->cursor >= '0' && *p->cursor <= '9') {
        unsigned digit = (unsigned)(*p->cursor - '0');
        if (result > (limit - (unsigned long long)digit) / 10ull) {
            p->error = 1;
            return 0;
        }
        result = result * 10ull + (unsigned long long)digit;
        p->cursor++;
        saw_digit = 1;
    }
    if (!saw_digit) {
        p->error = 1;
        return 0;
    }
    *negative = is_negative;
    *value = result;
    return 1;
}

static int parser_boolean(parser *p, int *value)
{
    parser_skip_space(p);
    if (p->error) {
        return 0;
    }
    if (p->end - p->cursor >= 4 && memcmp(p->cursor, "true", 4) == 0) {
        *value = 1;
        p->cursor += 4;
        return 1;
    }
    if (p->end - p->cursor >= 5 && memcmp(p->cursor, "false", 5) == 0) {
        *value = 0;
        p->cursor += 5;
        return 1;
    }
    p->error = 1;
    return 0;
}

static int parser_skip_value(parser *p);

static int parser_skip_object(parser *p)
{
    if (!parser_take(p, '{')) {
        return 0;
    }
    parser_skip_space(p);
    if (p->cursor < p->end && *p->cursor == '}') {
        p->cursor++;
        return 1;
    }
    for (;;) {
        adapter_string key;
        if (!parser_string(p, &key) || !parser_take(p, ':') ||
            !parser_skip_value(p)) {
            return 0;
        }
        parser_skip_space(p);
        if (p->cursor < p->end && *p->cursor == ',') {
            p->cursor++;
            continue;
        }
        return parser_take(p, '}');
    }
}

static int parser_skip_array(parser *p)
{
    if (!parser_take(p, '[')) {
        return 0;
    }
    parser_skip_space(p);
    if (p->cursor < p->end && *p->cursor == ']') {
        p->cursor++;
        return 1;
    }
    for (;;) {
        if (!parser_skip_value(p)) {
            return 0;
        }
        parser_skip_space(p);
        if (p->cursor < p->end && *p->cursor == ',') {
            p->cursor++;
            continue;
        }
        return parser_take(p, ']');
    }
}

static int parser_skip_value(parser *p)
{
    parser_skip_space(p);
    if (p->error || p->cursor >= p->end) {
        p->error = 1;
        return 0;
    }
    switch (*p->cursor) {
    case '"': {
        adapter_string ignored;
        return parser_string(p, &ignored);
    }
    case '{':
        return parser_skip_object(p);
    case '[':
        return parser_skip_array(p);
    case 't':
    case 'f': {
        int ignored;
        return parser_boolean(p, &ignored);
    }
    case 'n':
        if (p->end - p->cursor >= 4 && memcmp(p->cursor, "null", 4) == 0) {
            p->cursor += 4;
            return 1;
        }
        p->error = 1;
        return 0;
    default: {
        int ignored_negative;
        unsigned long long ignored_value;
        return parser_integer(p, &ignored_negative, &ignored_value);
    }
    }
}

static int parse_arguments(parser *p, adapter_request *request)
{
    if (!parser_take(p, '{')) {
        return 0;
    }
    parser_skip_space(p);
    if (p->cursor < p->end && *p->cursor == '}') {
        p->cursor++;
        return 1;
    }
    for (;;) {
        adapter_string key;
        if (!parser_string(p, &key) || !parser_take(p, ':')) {
            return 0;
        }
        parser_skip_space(p);
        if (p->error) {
            return 0;
        }
        if (key_is(&key, "text")) {
            if (!parser_string(p, &request->text)) {
                return 0;
            }
        } else if (key_is(&key, "binary_hex")) {
            if (!parser_string(p, &request->binary_hex)) {
                return 0;
            }
        } else if (key_is(&key, "a")) {
            if (!parser_string(p, &request->a)) {
                return 0;
            }
        } else if (key_is(&key, "b")) {
            if (!parser_string(p, &request->b)) {
                return 0;
            }
        } else if (key_is(&key, "fault")) {
            if (!parser_string(p, &request->fault)) {
                return 0;
            }
        } else if (key_is(&key, "timestamp_seconds")) {
            if (!parser_integer(p, &request->timestamp_negative,
                                &request->timestamp_value)) {
                return 0;
            }
            request->has_timestamp = 1;
        } else if (key_is(&key, "active")) {
            if (!parser_boolean(p, &request->active)) {
                return 0;
            }
            request->has_active = 1;
        } else if (!parser_skip_value(p)) {
            return 0;
        }
        parser_skip_space(p);
        if (p->cursor < p->end && *p->cursor == ',') {
            p->cursor++;
            continue;
        }
        return parser_take(p, '}');
    }
}

static int parse_request(const char *line, size_t length, adapter_request *request)
{
    parser p;
    adapter_string key;

    memset(request, 0, sizeof(*request));
    p.cursor = line;
    p.end = line + length;
    p.error = 0;

    if (!parser_take(&p, '{')) {
        return 0;
    }
    parser_skip_space(&p);
    if (p.cursor < p.end && *p.cursor == '}') {
        p.cursor++;
    } else {
        for (;;) {
            if (!parser_string(&p, &key) || !parser_take(&p, ':')) {
                return 0;
            }
            parser_skip_space(&p);
            if (p.error) {
                return 0;
            }
            if (key_is(&key, "request_id")) {
                int negative;
                unsigned long long value;
                if (!parser_integer(&p, &negative, &value) || negative ||
                    value > (unsigned long long)LLONG_MAX) {
                    return 0;
                }
                request->request_id = (long long)value;
                request->has_request_id = 1;
            } else if (key_is(&key, "operation")) {
                if (!parser_string(&p, &request->operation)) {
                    return 0;
                }
            } else if (key_is(&key, "arguments")) {
                if (!parse_arguments(&p, request)) {
                    return 0;
                }
            } else if (!parser_skip_value(&p)) {
                return 0;
            }
            parser_skip_space(&p);
            if (p.cursor < p.end && *p.cursor == ',') {
                p.cursor++;
                continue;
            }
            if (!parser_take(&p, '}')) {
                return 0;
            }
            break;
        }
    }
    parser_skip_space(&p);
    return !p.error && p.cursor == p.end && request->has_request_id &&
           request->operation.present;
}

static void respond_error(long long request_id, const char *category,
                          const char *message)
{
    printf("{\"request_id\":%lld,\"ok\":false,\"error\":{\"category\":\"%s\","
           "\"message\":\"%s\"}}\n",
           request_id, category, message);
    fflush(stdout);
}

static void respond_string(long long request_id, const char *value)
{
    printf("{\"request_id\":%lld,\"ok\":true,\"value\":\"%s\"}\n",
           request_id, value);
    fflush(stdout);
}

static void respond_bool(long long request_id, int value)
{
    printf("{\"request_id\":%lld,\"ok\":true,\"value\":%s}\n",
           request_id, value ? "true" : "false");
    fflush(stdout);
}

static void respond_int(long long request_id, long long value)
{
    printf("{\"request_id\":%lld,\"ok\":true,\"value\":%lld}\n",
           request_id, value);
    fflush(stdout);
}

static void respond_parse(long long request_id, unsigned long long timestamp,
                          const char *randomness_hex)
{
    printf("{\"request_id\":%lld,\"ok\":true,\"value\":{\"timestamp_seconds\":"
           "%llu,\"randomness_hex\":\"%s\"}}\n",
           request_id, timestamp, randomness_hex);
    fflush(stdout);
}

static void respond_hello(long long request_id)
{
    printf("{\"request_id\":%lld,\"ok\":true,\"value\":{\"protocol_version\":1,"
           "\"capabilities\":[\"binary_to_text\",\"boolean_validation\","
           "\"format_only_validation\",\"generate_binary\","
           "\"generate_binary_with_timestamp\",\"generate_string\","
           "\"generate_with_timestamp\",\"parse_components\","
           "\"public_compare\",\"text_to_binary\"]}}\n",
           request_id);
    fflush(stdout);
}

static const char *category_for_status(pikaid_status status)
{
    if (status == PIKAID_ERR_ENTROPY) {
        return "entropy_unavailable";
    }
    if (status == PIKAID_ERR_CLOCK) {
        return "internal_error";
    }
    return "invalid_input";
}

static void respond_status(long long request_id, pikaid_status status)
{
    respond_error(request_id, category_for_status(status),
                  pikaid_status_string(status));
}

static int hex_value(char c)
{
    if (c >= '0' && c <= '9') {
        return (int)(c - '0');
    }
    if (c >= 'a' && c <= 'f') {
        return (int)(c - 'a') + 10;
    }
    return -1;
}

static int parse_hex_bytes(const adapter_string *hex, unsigned char *out,
                           size_t count)
{
    size_t i;

    if (!hex->present || hex->len != count * 2u) {
        return 0;
    }
    for (i = 0; i < count; i++) {
        int high = hex_value(hex->data[i * 2u]);
        int low = hex_value(hex->data[i * 2u + 1u]);
        if (high < 0 || low < 0) {
            return 0;
        }
        out[i] = (unsigned char)((high << 4) | low);
    }
    return 1;
}

static void bytes_to_hex(const unsigned char *bytes, size_t count, char *out)
{
    static const char digits[] = "0123456789abcdef";
    size_t i;

    for (i = 0; i < count; i++) {
        out[i * 2u] = digits[bytes[i] >> 4];
        out[i * 2u + 1u] = digits[bytes[i] & 0x0fu];
    }
    out[count * 2u] = '\0';
}

typedef struct {
    int checks;
    int failures;
} self_check_state;

static void self_check_line(self_check_state *state, const char *name,
                            int passed, const char *detail)
{
    state->checks += 1;
    if (passed) {
        printf("{\"request_id\":%d,\"ok\":true,\"check\":\"%s\"}\n",
               state->checks, name);
    } else {
        state->failures += 1;
        printf("{\"request_id\":%d,\"ok\":false,\"check\":\"%s\","
               "\"error\":{\"category\":\"self_check_failed\","
               "\"message\":\"%s\"}}\n",
               state->checks, name,
               detail != NULL ? detail : "check did not hold");
    }
    fflush(stdout);
}

static void self_check_status(self_check_state *state, const char *name,
                              pikaid_status actual, pikaid_status expected)
{
    char detail[96];

    if (actual == expected) {
        self_check_line(state, name, 1, NULL);
        return;
    }
    (void)snprintf(detail, sizeof(detail), "expected %s, got %s",
                   pikaid_status_string(expected),
                   pikaid_status_string(actual));
    self_check_line(state, name, 0, detail);
}

/*
 * Exercise every exported public entry point through this compiled test-only
 * CLI, including the functions the official protocol does not call. This is a
 * native API smoke check, not part of the wire protocol and not a substitute
 * for the native test suite.
 */
static int run_self_check(void)
{
    static const uint8_t randomness[PIKAID_RANDOMNESS_LENGTH] = {
        UINT8_C(0x00), UINT8_C(0x11), UINT8_C(0x22), UINT8_C(0x33),
        UINT8_C(0x44), UINT8_C(0x55), UINT8_C(0x66), UINT8_C(0x77),
        UINT8_C(0x88), UINT8_C(0x99), UINT8_C(0xaa), UINT8_C(0xbb)
    };
    static const char *const low = "00000000000000000000000000";
    static const char *const high = "00000000000000000000000001";
    static const uint64_t fixed_timestamp = UINT64_C(1700000000);
    self_check_state state = {0, 0};
    uint8_t randomness_out[PIKAID_RANDOMNESS_LENGTH];
    uint8_t binary[PIKAID_BINARY_LENGTH];
    pikaid_t parsed;
    pikaid_t decoded;
    char text[PIKAID_STRING_SIZE];
    char encoded[PIKAID_STRING_SIZE];
    char formatted[PIKAID_STRING_SIZE];
    uint64_t timestamp = 0;
    int comparison = 0;
    int code;
    int all_status_strings = 1;
    int all_valid = 1;

    self_check_line(&state, "status_string_ok",
                    strcmp(pikaid_status_string(PIKAID_OK), "ok") == 0, NULL);
    for (code = 0; code <= (int)PIKAID_ERR_CLOCK; ++code) {
        const char *message = pikaid_status_string((pikaid_status)code);
        if (message == NULL || message[0] == '\0') {
            all_status_strings = 0;
        }
    }
    self_check_line(&state, "status_string_all", all_status_strings, NULL);

    memset(text, 'x', sizeof(text));
    self_check_status(&state, "generate_string_status", pikaid_generate(text),
                      PIKAID_OK);
    self_check_line(&state, "generate_string_shape",
                    strlen(text) == (size_t)PIKAID_STRING_LENGTH, NULL);
    self_check_line(&state, "is_valid_true",
                    pikaid_is_valid(text, (size_t)PIKAID_STRING_LENGTH), NULL);
    self_check_line(&state, "is_well_formed_true",
                    pikaid_is_well_formed(text, (size_t)PIKAID_STRING_LENGTH),
                    NULL);
    self_check_line(&state, "is_valid_false",
                    !pikaid_is_valid(high, (size_t)PIKAID_STRING_LENGTH - 1u),
                    NULL);

    self_check_status(&state, "parse_status",
                      pikaid_parse(text, (size_t)PIKAID_STRING_LENGTH, &parsed),
                      PIKAID_OK);
    self_check_status(&state, "parse_cstr_status",
                      pikaid_parse_cstr(text, &parsed), PIKAID_OK);
    self_check_status(&state, "get_timestamp_status",
                      pikaid_get_timestamp(&parsed, &timestamp), PIKAID_OK);
    self_check_line(&state, "get_timestamp_range",
                    timestamp <= PIKAID_TIMESTAMP_MAX, NULL);
    self_check_status(&state, "get_randomness_status",
                      pikaid_get_randomness(&parsed, randomness_out),
                      PIKAID_OK);

    self_check_status(&state, "to_binary_status",
                      pikaid_to_binary(&parsed, binary), PIKAID_OK);
    self_check_status(&state, "format_status",
                      pikaid_format(&parsed, formatted), PIKAID_OK);
    self_check_line(&state, "format_matches_parse",
                    strcmp(text, formatted) == 0, NULL);

    self_check_status(&state, "encode_status",
                      pikaid_encode(fixed_timestamp, randomness, encoded),
                      PIKAID_OK);
    self_check_status(&state, "encode_parse_status",
                      pikaid_parse(encoded, (size_t)PIKAID_STRING_LENGTH,
                                   &decoded),
                      PIKAID_OK);
    self_check_status(&state, "encode_timestamp_status",
                      pikaid_get_timestamp(&decoded, &timestamp), PIKAID_OK);
    self_check_line(&state, "encode_timestamp_value",
                    timestamp == fixed_timestamp, NULL);
    self_check_status(&state, "encode_randomness_status",
                      pikaid_get_randomness(&decoded, randomness_out),
                      PIKAID_OK);
    self_check_line(&state, "encode_randomness_value",
                    memcmp(randomness_out, randomness,
                           PIKAID_RANDOMNESS_LENGTH) == 0, NULL);

    self_check_status(&state, "encode_binary_status",
                      pikaid_encode_binary(fixed_timestamp, randomness, binary),
                      PIKAID_OK);
    self_check_status(&state, "decode_binary_status",
                      pikaid_decode_binary(binary, (size_t)PIKAID_BINARY_LENGTH,
                                           &decoded),
                      PIKAID_OK);
    self_check_status(&state, "decode_binary_format",
                      pikaid_format(&decoded, formatted), PIKAID_OK);
    self_check_line(&state, "decode_binary_matches_encode",
                    strcmp(formatted, encoded) == 0, NULL);

    self_check_status(&state, "generate_binary_status",
                      pikaid_generate_binary(binary), PIKAID_OK);
    self_check_status(&state, "generate_binary_decode",
                      pikaid_decode_binary(binary, (size_t)PIKAID_BINARY_LENGTH,
                                           &decoded),
                      PIKAID_OK);
    self_check_status(&state, "generate_binary_format",
                      pikaid_format(&decoded, formatted), PIKAID_OK);
    self_check_line(&state, "generate_binary_shape",
                    strlen(formatted) == (size_t)PIKAID_STRING_LENGTH, NULL);

    self_check_status(&state, "generate_at_status",
                      pikaid_generate_at(fixed_timestamp, text), PIKAID_OK);
    self_check_status(&state, "generate_at_parse",
                      pikaid_parse(text, (size_t)PIKAID_STRING_LENGTH,
                                   &decoded),
                      PIKAID_OK);
    self_check_status(&state, "generate_at_timestamp_status",
                      pikaid_get_timestamp(&decoded, &timestamp), PIKAID_OK);
    self_check_line(&state, "generate_at_timestamp_value",
                    timestamp == fixed_timestamp, NULL);

    self_check_status(&state, "generate_binary_at_status",
                      pikaid_generate_binary_at(fixed_timestamp, binary),
                      PIKAID_OK);
    self_check_status(&state, "generate_binary_at_decode",
                      pikaid_decode_binary(binary, (size_t)PIKAID_BINARY_LENGTH,
                                           &decoded),
                      PIKAID_OK);
    self_check_status(&state, "generate_binary_at_timestamp_status",
                      pikaid_get_timestamp(&decoded, &timestamp), PIKAID_OK);
    self_check_line(&state, "generate_binary_at_timestamp_value",
                    timestamp == fixed_timestamp, NULL);

    comparison = 0;
    self_check_status(&state, "compare_strings_low",
                      pikaid_compare_strings(low, high, &comparison),
                      PIKAID_OK);
    self_check_line(&state, "compare_strings_low_value", comparison == -1,
                    NULL);
    self_check_status(&state, "compare_strings_high",
                      pikaid_compare_strings(high, low, &comparison),
                      PIKAID_OK);
    self_check_line(&state, "compare_strings_high_value", comparison == 1,
                    NULL);
    self_check_status(&state, "compare_strings_equal",
                      pikaid_compare_strings(low, low, &comparison),
                      PIKAID_OK);
    self_check_line(&state, "compare_strings_equal_value", comparison == 0,
                    NULL);

    self_check_status(&state, "compare_parse_a",
                      pikaid_parse_cstr(low, &parsed), PIKAID_OK);
    self_check_status(&state, "compare_parse_b",
                      pikaid_parse_cstr(high, &decoded), PIKAID_OK);
    comparison = 0;
    self_check_status(&state, "compare_status",
                      pikaid_compare(&parsed, &decoded, &comparison),
                      PIKAID_OK);
    self_check_line(&state, "compare_value", comparison == -1, NULL);

    self_check_status(&state, "parse_null",
                      pikaid_parse(NULL, 0, NULL), PIKAID_ERR_NULL_ARGUMENT);
    self_check_status(&state, "parse_length",
                      pikaid_parse(high, (size_t)PIKAID_STRING_LENGTH - 1u,
                                   &parsed),
                      PIKAID_ERR_LENGTH);
    self_check_status(&state, "parse_character",
                      pikaid_parse("AAAAAAAAAAAAAAAAAAAAAAAAAA",
                                   (size_t)PIKAID_STRING_LENGTH, &parsed),
                      PIKAID_ERR_CHARACTER);
    self_check_status(&state, "decode_binary_length",
                      pikaid_decode_binary(binary, (size_t)PIKAID_BINARY_LENGTH - 1u,
                                           &parsed),
                      PIKAID_ERR_LENGTH);
    self_check_status(&state, "encode_timestamp_range",
                      pikaid_encode(PIKAID_TIMESTAMP_MAX + UINT64_C(1),
                                    randomness, text),
                      PIKAID_ERR_TIMESTAMP);
    self_check_line(&state, "encode_timestamp_clears", text[0] == '\0', NULL);
    self_check_status(&state, "encode_null_randomness",
                      pikaid_encode(fixed_timestamp, NULL, text),
                      PIKAID_ERR_NULL_ARGUMENT);

    for (code = 0; code < PIKAID_STRING_LENGTH; ++code) {
        if (encoded[code] < '0' ||
            (encoded[code] > '9' && encoded[code] < 'a') ||
            encoded[code] > 'z') {
            all_valid = 0;
        }
    }
    self_check_line(&state, "encode_alphabet", all_valid, NULL);

    printf("{\"request_id\":%d,\"ok\":%s,\"value\":{\"checks\":%d,"
           "\"failures\":%d}}\n",
           state.checks + 1, state.failures == 0 ? "true" : "false",
           state.checks, state.failures);
    fflush(stdout);
    return state.failures == 0 ? 0 : 1;
}

static int operation_is(const adapter_request *request, const char *name)
{
    size_t length = strlen(name);
    return request->operation.present &&
           request->operation.len == length &&
           memcmp(request->operation.data, name, length) == 0;
}

static int checked_timestamp(const adapter_request *request,
                             uint64_t *timestamp)
{
    if (!request->has_timestamp || request->timestamp_negative) {
        return 0;
    }
    if (request->timestamp_value > (unsigned long long)PIKAID_TIMESTAMP_MAX) {
        return 0;
    }
    *timestamp = (uint64_t)request->timestamp_value;
    return 1;
}

static void handle_text_to_binary(long long request_id,
                                  const adapter_request *request)
{
    pikaid_t parsed;
    pikaid_status status;
    unsigned char binary[PIKAID_BINARY_LENGTH];
    char hex[PIKAID_BINARY_LENGTH * 2u + 1u];

    if (!request->text.present) {
        respond_error(request_id, "invalid_input", "missing text argument");
        return;
    }
    status = pikaid_parse(request->text.data, request->text.len, &parsed);
    if (status != PIKAID_OK) {
        respond_status(request_id, status);
        return;
    }
    status = pikaid_to_binary(&parsed, binary);
    if (status != PIKAID_OK) {
        respond_status(request_id, status);
        return;
    }
    bytes_to_hex(binary, PIKAID_BINARY_LENGTH, hex);
#ifdef PIKAID_CONFORMANCE_MUTANT
    /* Deliberate control: the harness must reject a wrong wire result. */
    hex[0] = (hex[0] == '0') ? '1' : '0';
#endif
    respond_string(request_id, hex);
}

static void handle_binary_to_text(long long request_id,
                                  const adapter_request *request)
{
    pikaid_t parsed;
    pikaid_status status;
    unsigned char binary[PIKAID_BINARY_LENGTH];
    char text[PIKAID_STRING_SIZE];

    if (!parse_hex_bytes(&request->binary_hex, binary, PIKAID_BINARY_LENGTH)) {
        respond_error(request_id, "invalid_input",
                      "invalid 17-byte lowercase hex binary");
        return;
    }
    status = pikaid_decode_binary(binary, PIKAID_BINARY_LENGTH, &parsed);
    if (status != PIKAID_OK) {
        respond_status(request_id, status);
        return;
    }
    status = pikaid_format(&parsed, text);
    if (status != PIKAID_OK) {
        respond_status(request_id, status);
        return;
    }
    respond_string(request_id, text);
}

static void handle_parse_components(long long request_id,
                                    const adapter_request *request)
{
    pikaid_t parsed;
    pikaid_status status;
    uint64_t timestamp;
    unsigned char randomness[PIKAID_RANDOMNESS_LENGTH];
    char hex[PIKAID_RANDOMNESS_LENGTH * 2u + 1u];

    if (!request->text.present) {
        respond_error(request_id, "invalid_input", "missing text argument");
        return;
    }
    status = pikaid_parse(request->text.data, request->text.len, &parsed);
    if (status != PIKAID_OK) {
        respond_status(request_id, status);
        return;
    }
    status = pikaid_get_timestamp(&parsed, &timestamp);
    if (status != PIKAID_OK) {
        respond_status(request_id, status);
        return;
    }
    status = pikaid_get_randomness(&parsed, randomness);
    if (status != PIKAID_OK) {
        respond_status(request_id, status);
        return;
    }
    bytes_to_hex(randomness, PIKAID_RANDOMNESS_LENGTH, hex);
    respond_parse(request_id, (unsigned long long)timestamp, hex);
}

static void handle_public_compare(long long request_id,
                                  const adapter_request *request)
{
    pikaid_t first;
    pikaid_t second;
    pikaid_status status;
    int comparison = 0;

    if (!request->a.present || !request->b.present) {
        respond_error(request_id, "invalid_input", "missing comparison operands");
        return;
    }
    status = pikaid_parse(request->a.data, request->a.len, &first);
    if (status != PIKAID_OK) {
        respond_status(request_id, status);
        return;
    }
    status = pikaid_parse(request->b.data, request->b.len, &second);
    if (status != PIKAID_OK) {
        respond_status(request_id, status);
        return;
    }
    status = pikaid_compare(&first, &second, &comparison);
    if (status != PIKAID_OK) {
        respond_status(request_id, status);
        return;
    }
    respond_int(request_id, (long long)comparison);
}

static void handle_request(const adapter_request *request)
{
    long long request_id = request->request_id;

    if (operation_is(request, "hello")) {
        respond_hello(request_id);
        return;
    }
    if (operation_is(request, "text_to_binary")) {
        handle_text_to_binary(request_id, request);
        return;
    }
    if (operation_is(request, "binary_to_text")) {
        handle_binary_to_text(request_id, request);
        return;
    }
    if (operation_is(request, "boolean_validation")) {
        if (!request->text.present) {
            respond_error(request_id, "invalid_input", "missing text argument");
            return;
        }
        respond_bool(request_id,
                     pikaid_is_valid(request->text.data, request->text.len));
        return;
    }
    if (operation_is(request, "format_only_validation")) {
        if (!request->text.present) {
            respond_error(request_id, "invalid_input", "missing text argument");
            return;
        }
        respond_bool(request_id,
                     pikaid_is_well_formed(request->text.data, request->text.len));
        return;
    }
    if (operation_is(request, "parse_components")) {
        handle_parse_components(request_id, request);
        return;
    }
    if (operation_is(request, "generate_string")) {
        char out[PIKAID_STRING_SIZE];
        pikaid_status status = pikaid_generate(out);
        if (status != PIKAID_OK) {
            respond_status(request_id, status);
            return;
        }
        respond_string(request_id, out);
        return;
    }
    if (operation_is(request, "generate_with_timestamp")) {
        pikaid_status status;
        uint64_t timestamp;
        char out[PIKAID_STRING_SIZE];
        if (!checked_timestamp(request, &timestamp)) {
            respond_error(request_id, "invalid_input",
                          "timestamp out of range");
            return;
        }
        status = pikaid_generate_at(timestamp, out);
        if (status != PIKAID_OK) {
            respond_status(request_id, status);
            return;
        }
        respond_string(request_id, out);
        return;
    }
    if (operation_is(request, "generate_binary")) {
        unsigned char binary[PIKAID_BINARY_LENGTH];
        char hex[PIKAID_BINARY_LENGTH * 2u + 1u];
        pikaid_status status = pikaid_generate_binary(binary);
        if (status != PIKAID_OK) {
            respond_status(request_id, status);
            return;
        }
        bytes_to_hex(binary, PIKAID_BINARY_LENGTH, hex);
        respond_string(request_id, hex);
        return;
    }
    if (operation_is(request, "generate_binary_with_timestamp")) {
        unsigned char binary[PIKAID_BINARY_LENGTH];
        char hex[PIKAID_BINARY_LENGTH * 2u + 1u];
        pikaid_status status;
        uint64_t timestamp;
        if (!checked_timestamp(request, &timestamp)) {
            respond_error(request_id, "invalid_input",
                          "timestamp out of range");
            return;
        }
        status = pikaid_generate_binary_at(timestamp, binary);
        if (status != PIKAID_OK) {
            respond_status(request_id, status);
            return;
        }
        bytes_to_hex(binary, PIKAID_BINARY_LENGTH, hex);
        respond_string(request_id, hex);
        return;
    }
    if (operation_is(request, "public_compare")) {
        handle_public_compare(request_id, request);
        return;
    }
    respond_error(request_id, "unsupported_operation",
                  "operation is not advertised by this adapter");
}

/* Returns 1 for a line, 0 at clean end of input, -1 when the line is too long. */
static int read_line(char *buffer, size_t capacity, size_t *length)
{
    size_t used = 0;
    int c;

    while ((c = fgetc(stdin)) != EOF) {
        if (c == '\n') {
            buffer[used] = '\0';
            *length = used;
            return 1;
        }
        if (c == '\r') {
            continue;
        }
        if (used + 1u >= capacity) {
            return -1;
        }
        buffer[used] = (char)c;
        used++;
    }
    if (used == 0) {
        return 0;
    }
    buffer[used] = '\0';
    *length = used;
    return 1;
}

int main(int argc, char **argv)
{
    char *line;

    if (argc == 2 && strcmp(argv[1], "--self-check") == 0) {
        return run_self_check();
    }
    if (argc != 1) {
        fprintf(stderr, "pikaid_conformance_adapter: no arguments expected\n");
        return 2;
    }

    line = (char *)malloc(ADAPTER_LINE_MAX);
    if (line == NULL) {
        fprintf(stderr, "pikaid_conformance_adapter: out of memory\n");
        return 2;
    }

    for (;;) {
        size_t length = 0;
        int result = read_line(line, ADAPTER_LINE_MAX, &length);
        adapter_request request;

        if (result == 0) {
            break;
        }
        if (result < 0) {
            fprintf(stderr, "pikaid_conformance_adapter: request line too long\n");
            free(line);
            return 2;
        }
        if (length == 0) {
            continue;
        }
        if (!parse_request(line, length, &request)) {
            fprintf(stderr, "pikaid_conformance_adapter: malformed request\n");
            free(line);
            return 2;
        }
        handle_request(&request);
    }

    free(line);
    return 0;
}
