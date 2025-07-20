// SPDX-License-Identifier: Apache-2.0 OR MIT

// The contents of this file come from the Rust rustc-demangle library, hosted
// in the <https://github.com/rust-lang/rustc-demangle> repository, licensed
// under "Apache-2.0 OR MIT". For copyright details, see
// <https://github.com/rust-lang/rustc-demangle/blob/main/README.md>.
// Please note that the file should be kept as close as possible to upstream.

// Code for demangling Rust symbols. This code is mostly
// a line-by-line translation of the Rust code in `rustc-demangle`.

// you can find the latest version of this code in https://github.com/rust-lang/rustc-demangle

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdbool.h>
#include <sys/param.h>
#include <stdio.h>

#include "demangle-rust-v0.h"

#if defined(__GNUC__) || defined(__clang__)
#define NODISCARD __attribute__((warn_unused_result))
#else
#define NODISCARD
#endif

#define MAX_DEPTH 500

typedef enum {
    DemangleOk,
    DemangleInvalid,
    DemangleRecursed,
    DemangleBug,
} demangle_status;

struct demangle_v0 {
    const char *mangled;
    size_t mangled_len;
};

struct demangle_legacy {
    const char *mangled;
    size_t mangled_len;
    size_t elements;
};

// private version of memrchr to avoid _GNU_SOURCE
static void *demangle_memrchr(const void *s, int c, size_t n) {
    const uint8_t *s_ = s;
    for (; n != 0; n--) {
        if (s_[n-1] == c) {
            return (void*)&s_[n-1];
        }
    }
    return NULL;
}


static bool unicode_iscontrol(uint32_t ch) {
    // this is *technically* a unicode table, but
    // some unicode properties are simpler than you might think
    return ch < 0x20 || (ch >= 0x7f && ch < 0xa0);
}

// "good enough" tables, the only consequence is that when printing
// *constant strings*, some characters are printed as `\u{abcd}` rather than themselves.
//
// I'm leaving these here to allow easily replacing them with actual
// tables if desired.
static bool unicode_isprint(uint32_t ch) {
    if (ch < 0x20) {
        return false;
    }
    if (ch < 0x7f) {
        return true;
    }
    return false;
}

static bool unicode_isgraphemextend(uint32_t ch) {
    (void)ch;
    return false;
}

static bool str_isascii(const char *s, size_t s_len) {
    for (size_t i = 0; i < s_len; i++) {
        if (s[i] & 0x80) {
            return false;
        }
    }

    return true;
}

typedef enum {
    PunycodeOk,
    PunycodeError
} punycode_status;

struct parser {
    // the parser assumes that `sym` has a safe "terminating byte". It might be NUL,
    // but it might also be something else if a symbol is "truncated".
    const char *sym;
    size_t sym_len;
    size_t next;
    uint32_t depth;
};

struct printer {
    demangle_status status; // if status == 0 parser is valid
    struct parser parser;
    char *out; // NULL for no output [in which case out_len is not decremented]
    size_t out_len;
    uint32_t bound_lifetime_depth;
    bool alternate;
};

static NODISCARD overflow_status printer_print_path(struct printer *printer, bool in_value);
static NODISCARD overflow_status printer_print_type(struct printer *printer);
static NODISCARD overflow_status printer_print_const(struct printer *printer, bool in_value);

static NODISCARD demangle_status try_parse_path(struct parser *parser) {
    struct printer printer = {
        DemangleOk,
        *parser,
        NULL,
        SIZE_MAX,
        0,
        false
    };
    overflow_status ignore = printer_print_path(&printer, false); // can't fail since no output
    (void)ignore;
    *parser = printer.parser;
    return printer.status;
}

NODISCARD static demangle_status rust_demangle_v0_demangle(const char *s, size_t s_len, struct demangle_v0 *res, const char **rest) {
    if (s_len > strlen(s)) {
        // s_len only exists to shorten the string, this is not a buffer API
        return DemangleInvalid;
    }

    const char *inner;
    size_t inner_len;
    if (s_len >= 2 && !strncmp(s, "_R", strlen("_R"))) {
        inner = s+2;
        inner_len = s_len - 2;
    } else if (s_len >= 1 && !strncmp(s, "R", strlen("R"))) {
        // On Windows, dbghelp strips leading underscores, so we accept "R..."
        // form too.
        inner = s+1;
        inner_len = s_len - 1;
    } else if (s_len >= 3 && !strncmp(s, "__R", strlen("__R"))) {
        // On OSX, symbols are prefixed with an extra _
        inner = s+3;
        inner_len = s_len - 3;
    } else {
        return DemangleInvalid;
    }

    // Paths always start with uppercase characters.
    if (*inner < 'A' || *inner > 'Z') {
        return DemangleInvalid;
    }

    if (!str_isascii(inner, inner_len)) {
        return DemangleInvalid;
    }

    struct parser parser = { inner, inner_len, 0, 0 };

    demangle_status status = try_parse_path(&parser);
    if (status != DemangleOk) return status;
    char next = parser.sym[parser.next];

    // Instantiating crate (paths always start with uppercase characters).
    if (parser.next < parser.sym_len && next >= 'A' && next <= 'Z') {
        status = try_parse_path(&parser);
        if (status != DemangleOk) return status;
    }

    res->mangled = inner;
    res->mangled_len = inner_len;
    if (rest) {
        *rest = parser.sym + parser.next;
    }

    return DemangleOk;
}

// This might require `len` to be up to 3 characters bigger than the real output len in case of utf-8
NODISCARD static overflow_status rust_demangle_v0_display_demangle(struct demangle_v0 res, char *out, size_t len, bool alternate) {
    struct printer printer = {
        DemangleOk,
        {
            res.mangled,
            res.mangled_len,
            0,
            0
        },
        out,
        len,
        0,
        alternate
    };
    if (printer_print_path(&printer, true) == OverflowOverflow) {
        return OverflowOverflow;
    }
    if (printer.out_len < OVERFLOW_MARGIN) {
        return OverflowOverflow;
    }
    *printer.out = '\0';
    return OverflowOk;
}

static size_t code_to_utf8(unsigned char *buffer, uint32_t code)
{
    if (code <= 0x7F) {
        buffer[0] = code;
        return 1;
    }
    if (code <= 0x7FF) {
        buffer[0] = 0xC0 | (code >> 6);            /* 110xxxxx */
        buffer[1] = 0x80 | (code & 0x3F);          /* 10xxxxxx */
        return 2;
    }
    if (code <= 0xFFFF) {
        buffer[0] = 0xE0 | (code >> 12);           /* 1110xxxx */
        buffer[1] = 0x80 | ((code >> 6) & 0x3F);   /* 10xxxxxx */
        buffer[2] = 0x80 | (code & 0x3F);          /* 10xxxxxx */
        return 3;
    }
    if (code <= 0x10FFFF) {
        buffer[0] = 0xF0 | (code >> 18);           /* 11110xxx */
        buffer[1] = 0x80 | ((code >> 12) & 0x3F);  /* 10xxxxxx */
        buffer[2] = 0x80 | ((code >> 6) & 0x3F);   /* 10xxxxxx */
        buffer[3] = 0x80 | (code & 0x3F);          /* 10xxxxxx */
        return 4;
    }
    return 0;
}


// return length of char at byte, or SIZE_MAX if invalid. buf should have 4 valid characters
static NODISCARD size_t utf8_next_char(uint8_t *s, uint32_t *ch) {
    uint8_t byte = *s;
    // UTF8-1      = %x00-7F
    // UTF8-2      = %xC2-DF UTF8-tail
    // UTF8-3      = %xE0 %xA0-BF UTF8-tail / %xE1-EC 2( UTF8-tail ) /
    //               %xED %x80-9F UTF8-tail / %xEE-EF 2( UTF8-tail )
    // UTF8-4      = %xF0 %x90-BF 2( UTF8-tail ) / %xF1-F3 3( UTF8-tail ) /
    //               %xF4 %x80-8F 2( UTF8-tail )
    if (byte < 0x80) {
        *ch = byte;
        return 1;
    } else if (byte < 0xc2) {
        return SIZE_MAX;
    } else if (byte < 0xe0) {
        if (s[1] >= 0x80 && s[1] < 0xc0) {
            *ch = ((byte&0x1f)<<6) + (s[1] & 0x3f);
            return 2;
        }
        return SIZE_MAX;
    } if (byte < 0xf0) {
        if (!(s[1] >= 0x80 && s[1] < 0xc0) || !(s[2] >= 0x80 && s[2] < 0xc0)) {
            return SIZE_MAX; // basic validation
        }
        if (byte == 0xe0 && s[1] < 0xa0) {
            return SIZE_MAX; // overshort
        }
        if (byte == 0xed && s[1] >= 0xa0) {
            return SIZE_MAX; // surrogate
        }
        *ch = ((byte&0x0f)<<12) + ((s[1] & 0x3f)<<6) + (s[2] & 0x3f);
        return 3;
    } else if (byte < 0xf5) {
        if (!(s[1] >= 0x80 && s[1] < 0xc0) || !(s[2] >= 0x80 && s[2] < 0xc0) || !(s[3] >= 0x80 && s[3] < 0xc0)) {
            return SIZE_MAX; // basic validation
        }
        if (byte == 0xf0 && s[1] < 0x90) {
            return SIZE_MAX; // overshort
        }
        if (byte == 0xf4 && s[1] >= 0x90) {
            return SIZE_MAX; // over max
        }
        *ch = ((byte&0x07)<<18) + ((s[1] & 0x3f)<<12) + ((s[2] & 0x3f)<<6) + (s[3]&0x3f);
        return 4;
    } else {
        return SIZE_MAX;
    }
}

static NODISCARD bool validate_char(uint32_t n) {
    return ((n ^ 0xd800) - 0x800) < 0x110000 - 0x800;
}

#define SMALL_PUNYCODE_LEN 128

static NODISCARD punycode_status punycode_decode(const char *start, size_t ascii_len, const char *punycode_start, size_t punycode_len, uint32_t (*out_)[SMALL_PUNYCODE_LEN], size_t *out_len) {
    uint32_t *out = *out_;

    if (punycode_len == 0) {
        return PunycodeError;
    }

    if (ascii_len > SMALL_PUNYCODE_LEN) {
        return PunycodeError;
    }
    for (size_t i = 0; i < ascii_len; i++) {
        out[i] = start[i];
    }
    size_t len = ascii_len;

    size_t base = 36, t_min = 1, t_max = 26, skew = 38, damp = 700, bias = 72, i = 0, n = 0x80;
    for (;;) {
        size_t delta = 0, w = 1, k = 0;
        for (;;) {
            k += base;
            size_t biased = k < bias ? 0 : k - bias;
            size_t t = MIN(MAX(biased, t_min), t_max);
            size_t d;
            if (punycode_len == 0) {
                return PunycodeError;
            }
            char nx = *punycode_start++;
            punycode_len--;
            if ('a' <= nx && nx <= 'z') {
                d = nx - 'a';
            } else if ('0' <= nx && nx <= '9') {
                d = 26 + (nx - '0');
            } else {
                return PunycodeError;
            }
            if (w == 0 || d > SIZE_MAX / w || d*w > SIZE_MAX - delta) {
                return PunycodeError;
            }
            delta += d * w;
            if (d < t) {
                break;
            }
            if (base < t || w == 0 || (base - t) > SIZE_MAX / w) {
                return PunycodeError;
            }
            w *= (base - t);
        }

        len += 1;
        if (i > SIZE_MAX - delta) {
            return PunycodeError;
        }
        i += delta;
        if (n > SIZE_MAX - i / len) {
            return PunycodeError;
        }
        n += i / len;
        i %= len;

        // char validation
        if (n > UINT32_MAX || !validate_char((uint32_t)n)) {
            return PunycodeError;
        }

        // insert new character
        if (len > SMALL_PUNYCODE_LEN) {
            return PunycodeError;
        }
        memmove(out + i + 1, out + i, (len - i - 1) * sizeof(uint32_t));
        out[i] = (uint32_t)n;

        // start i index at incremented position
        i++;

        // If there are no more deltas, decoding is complete.
        if (punycode_len == 0) {
            *out_len = len;
            return PunycodeOk;
        }

        // Perform bias adaptation.
        delta /= damp;
        damp = 2;

        delta += delta / len;
        k = 0;
        while (delta > ((base - t_min) * t_max) / 2) {
            delta /= base - t_min;
            k += base;
        }
        bias = k + ((base - t_min + 1) * delta) / (delta + skew);
    }
}

struct ident {
    const char *ascii_start;
    size_t ascii_len;
    const char *punycode_start;
    size_t punycode_len;
};

static NODISCARD overflow_status display_ident(const char *ascii_start, size_t ascii_len, const char *punycode_start, size_t punycode_len, uint8_t *out, size_t *out_len) {
    uint32_t outbuf[SMALL_PUNYCODE_LEN];

    size_t wide_len;
    size_t out_buflen = *out_len;

    if (punycode_len == 0) {
        if (ascii_len > out_buflen) {
            return OverflowOverflow;
        }
        memcpy(out, ascii_start, ascii_len);
        *out_len = ascii_len;
    } else if (punycode_decode(ascii_start, ascii_len, punycode_start, punycode_len, &outbuf, &wide_len) == PunycodeOk) {
        size_t narrow_len = 0;
        for (size_t i = 0; i < wide_len; i++) {
            if (out_buflen - narrow_len < 4) {
                return OverflowOverflow;
            }
            unsigned char *pos = &out[narrow_len];
            narrow_len += code_to_utf8(pos, outbuf[i]);
        }
        *out_len = narrow_len;
    } else {
        size_t narrow_len = 0;
        if (out_buflen < strlen("punycode{")) {
            return OverflowOverflow;
        }
        memcpy(out, "punycode{", strlen("punycode{"));
        narrow_len = strlen("punycode{");
        if (ascii_len > 0) {
            if (out_buflen - narrow_len < ascii_len || out_buflen - narrow_len - ascii_len < 1) {
                return OverflowOverflow;
            }
            memcpy(out + narrow_len, ascii_start, ascii_len);
            narrow_len += ascii_len;
            out[narrow_len] = '-';
            narrow_len++;
        }
        if (out_buflen - narrow_len < punycode_len || out_buflen - narrow_len - punycode_len < 1) {
            return OverflowOverflow;
        }
        memcpy(out + narrow_len, punycode_start, punycode_len);
        narrow_len += punycode_len;
        out[narrow_len] = '}';
        narrow_len++;
        *out_len = narrow_len;
    }

    return OverflowOk;
}

static NODISCARD bool try_parse_uint(const char *buf, size_t len, uint64_t *result) {
    size_t cur = 0;
    for(;cur < len && buf[cur] == '0';cur++);
    uint64_t result_val = 0;
    if (len - cur > 16) return false;
    for(;cur < len;cur++) {
        char c = buf[cur];
        result_val <<= 4;
        if ('0' <= c && c <= '9') {
            result_val += c - '0';
        } else if ('a' <= c && c <= 'f') {
            result_val += 10 + (c - 'a');
        } else {
            return false;
        }
    }
    *result = result_val;
    return true;
}

static NODISCARD bool dinibble2int(const char *buf, uint8_t *result) {
    uint8_t result_val = 0;
    for (int i = 0; i < 2; i++) {
        char c = buf[i];
        result_val <<= 4;
        if ('0' <= c && c <= '9') {
            result_val += c - '0';
        } else if ('a' <= c && c <= 'f') {
            result_val += 10 + (c - 'a');
        } else {
            return false;
        }
    }
    *result = result_val;
    return true;
}


typedef enum {
    NtsOk = 0,
    NtsOverflow = 1,
    NtsInvalid = 2
} nibbles_to_string_status;

// '\u{10ffff}', +margin
#define ESCAPED_SIZE 12

static NODISCARD size_t char_to_string(uint32_t ch, uint8_t quote, bool first, char (*buf)[ESCAPED_SIZE]) {
    // encode the character
    char *escaped_buf = *buf;
    escaped_buf[0] = '\\';
    size_t escaped_len = 2;
    switch (ch) {
        case '\0':
        escaped_buf[1] = '0';
        break;
        case '\t':
        escaped_buf[1] = 't';
        break;
        case '\r':
        escaped_buf[1] = 'r';
        break;
        case '\n':
        escaped_buf[1] = 'n';
        break;
        case '\\':
        escaped_buf[1] = '\\';
        break;
        default:
        if (ch == quote) {
            escaped_buf[1] = ch;
        } else if (!unicode_isprint(ch) || (first && unicode_isgraphemextend(ch))) {
            int hexlen = snprintf(escaped_buf, ESCAPED_SIZE, "\\u{%x}", (unsigned int)ch);
            if (hexlen < 0) {
                return 0; // (snprintf shouldn't fail!)
            }
            escaped_len = hexlen;
        } else {
            // printable character
            escaped_buf[0] = ch;
            escaped_len = 1;
        }
        break;
    }

    return escaped_len;
}

// convert nibbles to a single/double-quoted string
static NODISCARD nibbles_to_string_status nibbles_to_string(const char *buf, size_t len, uint8_t *out, size_t *out_len) {
    uint8_t quote = '"';
    bool first = true;

    if ((len % 2) != 0) {
        return NtsInvalid; // odd number of nibbles
    }

    size_t cur_out_len = 0;

    // write starting quote
    if (out != NULL) {
        cur_out_len = *out_len;
        if (cur_out_len == 0) {
            return NtsOverflow;
        }
        *out++ = quote;
        cur_out_len--;
    }

    uint8_t conv_buf[4] = {0};
    size_t conv_buf_len = 0;
    while (len > 1 || conv_buf_len > 0) {
        while (len > 1 && conv_buf_len < sizeof(conv_buf)) {
            if (!dinibble2int(buf, &conv_buf[conv_buf_len])) {
                return NtsInvalid;
            }
            conv_buf_len++;
            buf += 2;
            len -= 2;
        }

        // conv_buf is full here if possible, process 1 UTF-8 character
        uint32_t ch = 0;
        size_t consumed = utf8_next_char(conv_buf, &ch);
        if (consumed > conv_buf_len) {
            // either SIZE_MAX (invalid UTF-8) or finished input buffer and
            // there are still bytes remaining, in both cases invalid
            return NtsInvalid;
        }

        // "consume" the character
        memmove(conv_buf, conv_buf+consumed, conv_buf_len-consumed);
        conv_buf_len -= consumed;

        char escaped_buf[ESCAPED_SIZE];
        size_t escaped_len = char_to_string(ch, '"', first, &escaped_buf);
        if (out != NULL) {
            if (cur_out_len < escaped_len) {
                return NtsOverflow;
            }
            memcpy(out, escaped_buf, escaped_len);
            out += escaped_len;
            cur_out_len -= escaped_len;
        }
        first = false;
    }

    // write ending quote
    if (out != NULL) {
        if (cur_out_len == 0) {
            return NtsOverflow;
        }
        *out++ = quote;
        cur_out_len--;
        *out_len -= cur_out_len; // subtract remaining space to get used space
    }

    return NtsOk;
}

static const char* basic_type(uint8_t tag) {
    switch(tag) {
        case 'b':
        return "bool";
        case 'c':
        return "char";
        case 'e':
        return "str";
        case 'u':
        return "()";
        case 'a':
        return "i8";
        case 's':
        return "i16";
        case 'l':
        return "i32";
        case 'x':
        return "i64";
        case 'n':
        return "i128";
        case 'i':
        return "isize";
        case 'h':
        return "u8";
        case 't':
        return "u16";
        case 'm':
        return "u32";
        case 'y':
        return "u64";
        case 'o':
        return "u128";
        case 'j':
        return "usize";
        case 'f':
        return "f32";
        case 'd':
        return "f64";
        case 'z':
        return "!";
        case 'p':
        return "_";
        case 'v':
        return "...";
        default:
        return NULL;
    }
}

static NODISCARD demangle_status parser_push_depth(struct parser *parser) {
    parser->depth++;
    if (parser->depth > MAX_DEPTH) {
        return DemangleRecursed;
    } else {
        return DemangleOk;
    }
}

static demangle_status parser_pop_depth(struct parser *parser) {
    parser->depth--;
    return DemangleOk;
}

static uint8_t parser_peek(struct parser const *parser) {
    if (parser->next == parser->sym_len) {
        return 0; // add a "pseudo nul terminator" to avoid peeking past the end of a symbol
    } else {
        return parser->sym[parser->next];
    }
}

static bool parser_eat(struct parser *parser, uint8_t ch) {
    if (parser_peek(parser) == ch) {
        if (ch != 0) { // safety: make sure we don't skip past the NUL terminator
            parser->next++;
        }
        return true;
    } else {
        return false;
    }
}

static uint8_t parser_next(struct parser *parser) {
    // don't advance after end of input, and return an imaginary NUL terminator
    if (parser->next == parser->sym_len) {
        return 0;
    } else {
        return parser->sym[parser->next++];
    }
}

static NODISCARD demangle_status parser_ch(struct parser *parser, uint8_t *next) {
    // don't advance after end of input
    if (parser->next == parser->sym_len) {
        return DemangleInvalid;
    } else {
        *next = parser->sym[parser->next++];
        return DemangleOk;
    }
}

struct buf {
    const char *start;
    size_t len;
};

static NODISCARD demangle_status parser_hex_nibbles(struct parser *parser, struct buf *buf) {
    size_t start = parser->next;
    for (;;) {
        uint8_t ch = parser_next(parser);
        if (ch == '_') {
            break;
        }
        if (!(('0' <= ch && ch <= '9') || ('a' <= ch && ch <= 'f'))) {
            return DemangleInvalid;
        }
    }
    buf->start = parser->sym + start;
    buf->len = parser->next - start - 1; // skip final _
    return DemangleOk;
}

static NODISCARD demangle_status parser_digit_10(struct parser *parser, uint8_t *out) {
    uint8_t ch = parser_peek(parser);
    if ('0' <= ch && ch <= '9') {
        *out = ch - '0';
        parser->next++;
        return DemangleOk;
    } else {
        return DemangleInvalid;
    }
}

static NODISCARD demangle_status parser_digit_62(struct parser *parser, uint64_t *out) {
    uint8_t ch = parser_peek(parser);
    if ('0' <= ch && ch <= '9') {
        *out = ch - '0';
        parser->next++;
        return DemangleOk;
    } else if ('a' <= ch && ch <= 'z') {
        *out = 10 + (ch - 'a');
        parser->next++;
        return DemangleOk;
    } else if ('A' <= ch && ch <= 'Z') {
        *out = 10 + 26 + (ch - 'A');
        parser->next++;
        return DemangleOk;
    } else {
        return DemangleInvalid;
    }
}

static NODISCARD demangle_status parser_integer_62(struct parser *parser, uint64_t *out) {
    if (parser_eat(parser, '_')) {
        *out = 0;
        return DemangleOk;
    }

    uint64_t x = 0;
    demangle_status status;
    while (!parser_eat(parser, '_')) {
        uint64_t d;
        if ((status = parser_digit_62(parser, &d)) != DemangleOk) {
            return status;
        }
        if (x > UINT64_MAX / 62) {
            return DemangleInvalid;
        }
        x *= 62;
        if (x > UINT64_MAX - d) {
            return DemangleInvalid;
        }
        x += d;
    }
    if (x == UINT64_MAX) {
        return DemangleInvalid;
    }
    *out = x + 1;
    return DemangleOk;
}

static NODISCARD demangle_status parser_opt_integer_62(struct parser *parser, uint8_t tag, uint64_t *out) {
    if (!parser_eat(parser, tag)) {
        *out = 0;
        return DemangleOk;
    }

    demangle_status status;
    if ((status = parser_integer_62(parser, out)) != DemangleOk) {
        return status;
    }
    if (*out == UINT64_MAX) {
        return DemangleInvalid;
    }
    *out = *out + 1;
    return DemangleOk;
}

static NODISCARD demangle_status parser_disambiguator(struct parser *parser, uint64_t *out) {
    return parser_opt_integer_62(parser, 's', out);
}

typedef uint8_t parser_namespace_type;

static NODISCARD demangle_status parser_namespace(struct parser *parser, parser_namespace_type *out) {
    uint8_t next = parser_next(parser);
    if ('A' <= next && next <= 'Z') {
        *out = next;
        return DemangleOk;
    } else if ('a' <= next && next <= 'z') {
        *out = 0;
        return DemangleOk;
    } else {
        return DemangleInvalid;
    }
}

static NODISCARD demangle_status parser_backref(struct parser *parser, struct parser *out) {
    size_t start = parser->next;
    if (start == 0) {
        return DemangleBug;
    }
    size_t s_start = start - 1;
    uint64_t i;
    demangle_status status = parser_integer_62(parser, &i);
    if (status != DemangleOk) {
        return status;
    }
    if (i >= s_start) {
        return DemangleInvalid;
    }
    struct parser res = {
        .sym = parser->sym,
        .sym_len = parser->sym_len,
        .next = (size_t)i,
        .depth = parser->depth
    };
    status = parser_push_depth(&res);
    if (status != DemangleOk) {
        return status;
    }
    *out = res;
    return DemangleOk;
}

static NODISCARD demangle_status parser_ident(struct parser *parser, struct ident *out) {
    bool is_punycode = parser_eat(parser, 'u');
    size_t len;
    uint8_t d;
    demangle_status status = parser_digit_10(parser, &d);
    len = d;
    if (status != DemangleOk) {
        return status;
    }
    if (len) {
        for (;;) {
            status = parser_digit_10(parser, &d);
            if (status != DemangleOk) {
                break;
            }
            if (len > SIZE_MAX / 10) {
                return DemangleInvalid;
            }
            len *= 10;
            if (len > SIZE_MAX - d) {
                return DemangleInvalid;
            }
            len += d;
        }
    }

    // Skip past the optional `_` separator.
    parser_eat(parser, '_');

    size_t start = parser->next;
    if (parser->sym_len - parser->next < len) {
        return DemangleInvalid;
    }
    parser->next += len;

    const char *ident = &parser->sym[start];

    if (is_punycode) {
        const char *underscore = demangle_memrchr(ident, '_', (size_t)len);
        if (underscore == NULL) {
            *out = (struct ident){
                .ascii_start="",
                .ascii_len=0,
                .punycode_start=ident,
                .punycode_len=len
            };
        } else {
            size_t ascii_len = underscore - ident;
            // ascii_len <= len - 1 since `_` is in the first len bytes
            size_t punycode_len = len - 1 - ascii_len;
            *out = (struct ident){
                .ascii_start=ident,
                .ascii_len=ascii_len,
                .punycode_start=underscore + 1,
                .punycode_len=punycode_len
            };
        }
        if (out->punycode_len == 0) {
            return DemangleInvalid;
        }
        return DemangleOk;
    } else {
        *out = (struct ident) {
            .ascii_start=ident,
            .ascii_len=(size_t)len,
            .punycode_start="",
            .punycode_len=0,
        };
        return DemangleOk;
    }
}

#define INVALID_SYNTAX "{invalid syntax}"

static const char *demangle_error_message(demangle_status status) {
    switch (status) {
        case DemangleInvalid:
        return INVALID_SYNTAX;
        case DemangleBug:
        return "{bug}";
        case DemangleRecursed:
        return "{recursion limit reached}";
        default:
        return "{unknown error}";
    }
}

#define PRINT(print_fn) \
 do { \
   if ((print_fn) == OverflowOverflow) { \
    return OverflowOverflow; \
   } \
 } while(0)

#define PRINT_CH(printer, s) PRINT(printer_print_ch((printer), (s)))
#define PRINT_STR(printer, s) PRINT(printer_print_str((printer), (s)))
#define PRINT_U64(printer, s) PRINT(printer_print_u64((printer), (s)))
#define PRINT_IDENT(printer, s) PRINT(printer_print_ident((printer), (s)))

#define INVALID(printer) \
  do { \
    PRINT_STR((printer), INVALID_SYNTAX); \
    (printer)->status = DemangleInvalid; \
    return OverflowOk; \
  } while(0)

#define PARSE(printer, method, ...) \
  do { \
    if ((printer)->status != DemangleOk) { \
      PRINT_STR((printer), "?"); \
      return OverflowOk; \
    } else { \
      demangle_status _parse_status = method(&(printer)->parser, ## __VA_ARGS__); \
      if (_parse_status != DemangleOk) { \
        PRINT_STR((printer), demangle_error_message(_parse_status)); \
        (printer)->status = _parse_status; \
        return OverflowOk; \
      } \
    } \
  } while(0)

#define PRINT_SEP_LIST(printer, body, sep) \
  do { \
    size_t _sep_list_i; \
    PRINT_SEP_LIST_COUNT(printer, _sep_list_i, body, sep); \
  } while(0)

#define PRINT_SEP_LIST_COUNT(printer, count, body, sep) \
  do { \
    count = 0; \
    while ((printer)->status == DemangleOk && !printer_eat((printer), 'E')) { \
      if (count > 0) { PRINT_STR(printer, sep); } \
      body; \
      count++; \
    } \
  } while(0)

static bool printer_eat(struct printer *printer, uint8_t b) {
    if (printer->status != DemangleOk) {
        return false;
    }

    return parser_eat(&printer->parser, b);
}

static void printer_pop_depth(struct printer *printer) {
    if (printer->status == DemangleOk) {
        parser_pop_depth(&printer->parser);
    }
}

static NODISCARD overflow_status printer_print_buf(struct printer *printer, const char *start, size_t len) {
    if (printer->out == NULL) {
        return OverflowOk;
    }
    if (printer->out_len < len) {
        return OverflowOverflow;
    }

    memcpy(printer->out, start, len);
    printer->out += len;
    printer->out_len -= len;
    return OverflowOk;
}

static NODISCARD overflow_status printer_print_str(struct printer *printer, const char *buf) {
    return printer_print_buf(printer, buf, strlen(buf));
}

static NODISCARD overflow_status printer_print_ch(struct printer *printer, char ch) {
    return printer_print_buf(printer, &ch, 1);
}

static NODISCARD overflow_status printer_print_u64(struct printer *printer, uint64_t n) {
    char buf[32] = {0};
    sprintf(buf, "%llu", (unsigned long long)n); // printing uint64 uses 21 < 32 chars
    return printer_print_str(printer, buf);
}

static NODISCARD overflow_status printer_print_ident(struct printer *printer, struct ident *ident) {
    if (printer->out == NULL) {
        return OverflowOk;
    }

    size_t out_len = printer->out_len;
    overflow_status status;
    if ((status = display_ident(ident->ascii_start, ident->ascii_len, ident->punycode_start, ident->punycode_len, (uint8_t*)printer->out, &out_len)) != OverflowOk) {
        return status;
    }
    printer->out += out_len;
    printer->out_len -= out_len;
    return OverflowOk;
}

typedef overflow_status (*printer_fn)(struct printer *printer);
typedef overflow_status (*backref_fn)(struct printer *printer, bool *arg);

static NODISCARD overflow_status printer_print_backref(struct printer *printer, backref_fn func, bool *arg) {
    struct parser backref;
    PARSE(printer, parser_backref, &backref);

    if (printer->out == NULL) {
        return OverflowOk;
    }

    struct parser orig_parser = printer->parser;
    demangle_status orig_status = printer->status; // fixme not sure this is needed match for Ok on the Rust side
    printer->parser = backref;
    printer->status = DemangleOk;
    overflow_status status = func(printer, arg);
    printer->parser = orig_parser;
    printer->status = orig_status;

    return status;
}

static NODISCARD overflow_status printer_print_lifetime_from_index(struct printer *printer, uint64_t lt) {
    // Bound lifetimes aren't tracked when skipping printing.
    if (printer->out == NULL) {
        return OverflowOk;
    }

    PRINT_STR(printer, "'");
    if (lt == 0) {
        PRINT_STR(printer, "_");
        return OverflowOk;
    }

    if (printer->bound_lifetime_depth < lt) {
        INVALID(printer);
    } else {
        uint64_t depth = printer->bound_lifetime_depth - lt;
        if (depth < 26) {
            PRINT_CH(printer, 'a' + depth);
        } else {
            PRINT_STR(printer, "_");
            PRINT_U64(printer, depth);
        }

        return OverflowOk;
    }
}

static NODISCARD overflow_status printer_in_binder(struct printer *printer, printer_fn func) {
    uint64_t bound_lifetimes;
    PARSE(printer, parser_opt_integer_62, 'G', &bound_lifetimes);

    // Don't track bound lifetimes when skipping printing.
    if (printer->out == NULL) {
        return func(printer);
    }

    if (bound_lifetimes > 0) {
        PRINT_STR(printer, "for<");
        for (uint64_t i = 0; i < bound_lifetimes; i++) {
            if (i > 0) {
                PRINT_STR(printer, ", ");
            }
            printer->bound_lifetime_depth++;
            PRINT(printer_print_lifetime_from_index(printer, 1));
        }
        PRINT_STR(printer, "> ");
    }

    overflow_status r = func(printer);
    printer->bound_lifetime_depth -= bound_lifetimes;

    return r;
}

static NODISCARD overflow_status printer_print_generic_arg(struct printer *printer) {
    if (printer_eat(printer, 'L')) {
        uint64_t lt;
        PARSE(printer, parser_integer_62, &lt);
        return printer_print_lifetime_from_index(printer, lt);
    } else if (printer_eat(printer, 'K')) {
        return printer_print_const(printer, false);
    } else {
        return printer_print_type(printer);
    }
}

static NODISCARD overflow_status printer_print_generic_args(struct printer *printer) {
    PRINT_STR(printer, "<");
    PRINT_SEP_LIST(printer, PRINT(printer_print_generic_arg(printer)), ", ");
    PRINT_STR(printer, ">");
    return OverflowOk;
}

static NODISCARD overflow_status printer_print_path_out_of_value(struct printer *printer, bool *_arg) {
    (void)_arg;
    return printer_print_path(printer, false);
}

static NODISCARD overflow_status printer_print_path_in_value(struct printer *printer, bool *_arg) {
    (void)_arg;
    return printer_print_path(printer, true);
}

static NODISCARD overflow_status printer_print_path(struct printer *printer, bool in_value) {
    PARSE(printer, parser_push_depth);
    uint8_t tag;
    PARSE(printer, parser_ch, &tag);

    overflow_status st;
    uint64_t dis;
    struct ident name;
    parser_namespace_type ns;
    char *orig_out;

    switch(tag) {
    case 'C':
        PARSE(printer, parser_disambiguator, &dis);
        PARSE(printer, parser_ident, &name);

        PRINT_IDENT(printer, &name);

        if (printer->out != NULL && !printer->alternate && dis != 0) {
            PRINT_STR(printer, "[");
            char buf[24] = {0};
            sprintf(buf, "%llx", (unsigned long long)dis);
            PRINT_STR(printer, buf);
            PRINT_STR(printer, "]");
        }
        break;
    case 'N':
        PARSE(printer, parser_namespace, &ns);
        if ((st = printer_print_path(printer, in_value)) != OverflowOk) {
            return st;
        }

        // HACK(eddyb) if the parser is already marked as having errored,
        // `parse!` below will print a `?` without its preceding `::`
        // (because printing the `::` is skipped in certain conditions,
        // i.e. a lowercase namespace with an empty identifier),
        // so in order to get `::?`, the `::` has to be printed here.
        if (printer->status != DemangleOk) {
            PRINT_STR(printer, "::");
        }

        PARSE(printer, parser_disambiguator, &dis);
        PARSE(printer, parser_ident, &name);
        // Special namespace, like closures and shims
        if (ns) {
            PRINT_STR(printer, "::{");
            if (ns == 'C') {
                PRINT_STR(printer, "closure");
            } else if (ns == 'S') {
                PRINT_STR(printer, "shim");
            } else {
                PRINT_CH(printer, ns);
            }
            if (name.ascii_len != 0 || name.punycode_len != 0) {
                PRINT_STR(printer, ":");
                PRINT_IDENT(printer, &name);
            }
            PRINT_STR(printer, "#");
            PRINT_U64(printer, dis);
            PRINT_STR(printer, "}");
        } else {
            // Implementation-specific/unspecified namespaces
            if (name.ascii_len != 0 || name.punycode_len != 0) {
                PRINT_STR(printer, "::");
                PRINT_IDENT(printer, &name);
            }
        }
        break;
    case 'M':
    case 'X':
    // for impls, ignore the impls own path
    PARSE(printer, parser_disambiguator, &dis);
    orig_out = printer->out;
    printer->out = NULL;
    PRINT(printer_print_path(printer, false));
    printer->out = orig_out;

    // fallthru
    case 'Y':
    PRINT_STR(printer, "<");
    PRINT(printer_print_type(printer));
    if (tag != 'M') {
        PRINT_STR(printer, " as ");
        PRINT(printer_print_path(printer, false));
    }
    PRINT_STR(printer, ">");
    break;
    case 'I':
    PRINT(printer_print_path(printer, in_value));
    if (in_value) {
        PRINT_STR(printer, "::");
    }
    PRINT(printer_print_generic_args(printer));
    break;
    case 'B':
    PRINT(printer_print_backref(printer, in_value ? printer_print_path_in_value : printer_print_path_out_of_value, NULL));
    break;
    default:
    INVALID(printer);
    break;
    }

    printer_pop_depth(printer);
    return OverflowOk;
}

static NODISCARD overflow_status printer_print_const_uint(struct printer *printer, uint8_t tag) {
    struct buf hex;
    PARSE(printer, parser_hex_nibbles, &hex);

    uint64_t val;
    if (try_parse_uint(hex.start, hex.len, &val)) {
        PRINT_U64(printer, val);
    } else {
        PRINT_STR(printer, "0x");
        PRINT(printer_print_buf(printer, hex.start, hex.len));
    }

    if (printer->out != NULL && !printer->alternate) {
        const char *ty = basic_type(tag);
        if (/* safety */ ty != NULL) {
            PRINT_STR(printer, ty);
        }
    }

    return OverflowOk;
}

static NODISCARD overflow_status printer_print_const_str_literal(struct printer *printer) {
    struct buf hex;
    PARSE(printer, parser_hex_nibbles, &hex);

    size_t out_len = SIZE_MAX;
    nibbles_to_string_status nts_status = nibbles_to_string(hex.start, hex.len, NULL, &out_len);
    switch (nts_status) {
    case NtsOk:
        if (printer->out != NULL) {
            out_len = printer->out_len;
            nts_status = nibbles_to_string(hex.start, hex.len, (uint8_t*)printer->out, &out_len);
            if (nts_status != NtsOk) {
                return OverflowOverflow;
            }
            printer->out += out_len;
            printer->out_len -= out_len;
        }
        return OverflowOk;
    case NtsOverflow:
        // technically if there is a string of size `SIZE_MAX/6` whose escaped version overflows
        // SIZE_MAX but has an invalid char, this will be a "fake" overflow. In practice,
        // that is not going to happen and a fuzzer will not generate strings of this length.
        return OverflowOverflow;
    case NtsInvalid:
    default:
        INVALID(printer);
    }
}

static NODISCARD overflow_status printer_print_const_struct(struct printer *printer) {
    uint64_t dis;
    struct ident name;
    PARSE(printer, parser_disambiguator, &dis);
    PARSE(printer, parser_ident, &name);
    PRINT_IDENT(printer, &name);
    PRINT_STR(printer, ": ");
    return printer_print_const(printer, true);
}

static NODISCARD overflow_status printer_print_const_out_of_value(struct printer *printer, bool *_arg) {
    (void)_arg;
    return printer_print_const(printer, false);
}

static NODISCARD overflow_status printer_print_const_in_value(struct printer *printer, bool *_arg) {
    (void)_arg;
    return printer_print_const(printer, true);
}

static NODISCARD overflow_status printer_print_const(struct printer *printer, bool in_value) {
    uint8_t tag;

    PARSE(printer, parser_ch, &tag);
    PARSE(printer, parser_push_depth);

    struct buf hex;
    uint64_t val;
    size_t count;

    bool opened_brace = false;
#define OPEN_BRACE_IF_OUTSIDE_EXPR \
        do { if (!in_value) { \
            opened_brace = true; \
            PRINT_STR(printer, "{"); \
        } } while(0)

    switch(tag) {
    case 'p':
        PRINT_STR(printer, "_");
        break;
    // Primitive leaves with hex-encoded values (see `basic_type`).
    case 'a':
    case 's':
    case 'l':
    case 'x':
    case 'n':
    case 'i':
        if (printer_eat(printer, 'n')) {
            PRINT_STR(printer, "-");
        }
        /* fallthrough */
    case 'h':
    case 't':
    case 'm':
    case 'y':
    case 'o':
    case 'j':
        PRINT(printer_print_const_uint(printer, tag));
        break;
    case 'b':
        PARSE(printer, parser_hex_nibbles, &hex);
        if (try_parse_uint(hex.start, hex.len, &val)) {
            if (val == 0) {
                PRINT_STR(printer, "false");
            } else if (val == 1) {
                PRINT_STR(printer, "true");
            } else {
                INVALID(printer);
            }
        } else {
            INVALID(printer);
        }
        break;
    case 'c':
        PARSE(printer, parser_hex_nibbles, &hex);
        if (try_parse_uint(hex.start, hex.len, &val)
            && val < UINT32_MAX
            && validate_char((uint32_t)val))
        {
            char escaped_buf[ESCAPED_SIZE];
            size_t escaped_size = char_to_string((uint32_t)val, '\'', true, &escaped_buf);

            PRINT_STR(printer, "'");
            PRINT(printer_print_buf(printer, escaped_buf, escaped_size));
            PRINT_STR(printer, "'");
        } else {
            INVALID(printer);
        }
        break;
    case 'e':
        OPEN_BRACE_IF_OUTSIDE_EXPR;
        PRINT_STR(printer, "*");
        PRINT(printer_print_const_str_literal(printer));
        break;
    case 'R':
    case 'Q':
        if (tag == 'R' && printer_eat(printer, 'e')) {
            PRINT(printer_print_const_str_literal(printer));
        } else {
            OPEN_BRACE_IF_OUTSIDE_EXPR;
            PRINT_STR(printer, "&");
            if (tag != 'R') {
                PRINT_STR(printer, "mut ");
            }
            PRINT(printer_print_const(printer, true));
        }
        break;
    case 'A':
        OPEN_BRACE_IF_OUTSIDE_EXPR;
        PRINT_STR(printer, "[");
        PRINT_SEP_LIST(printer, PRINT(printer_print_const(printer, true)), ", ");
        PRINT_STR(printer, "]");
        break;
    case 'T':
        OPEN_BRACE_IF_OUTSIDE_EXPR;
        PRINT_STR(printer, "(");
        PRINT_SEP_LIST_COUNT(printer, count, PRINT(printer_print_const(printer, true)), ", ");
        if (count == 1) {
            PRINT_STR(printer, ",");
        }
        PRINT_STR(printer, ")");
        break;
    case 'V':
        OPEN_BRACE_IF_OUTSIDE_EXPR;
        PRINT(printer_print_path(printer, true));
        PARSE(printer, parser_ch, &tag);
        switch(tag) {
        case 'U':
        break;
        case 'T':
        PRINT_STR(printer, "(");
        PRINT_SEP_LIST(printer, PRINT(printer_print_const(printer, true)), ", ");
        PRINT_STR(printer, ")");
        break;
        case 'S':
        PRINT_STR(printer, " { ");
        PRINT_SEP_LIST(printer, PRINT(printer_print_const_struct(printer)), ", ");
        PRINT_STR(printer, " }");
        break;
        default:
        INVALID(printer);
        }
        break;
    case 'B':
        PRINT(printer_print_backref(printer, in_value ? printer_print_const_in_value : printer_print_const_out_of_value, NULL));
        break;
    default:
        INVALID(printer);
    }
#undef OPEN_BRACE_IF_OUTSIDE_EXPR

    if (opened_brace) {
        PRINT_STR(printer, "}");
    }
    printer_pop_depth(printer);

    return OverflowOk;
}

/// A trait in a trait object may have some "existential projections"
/// (i.e. associated type bindings) after it, which should be printed
/// in the `<...>` of the trait, e.g. `dyn Trait<T, U, Assoc=X>`.
/// To this end, this method will keep the `<...>` of an 'I' path
/// open, by omitting the `>`, and return `Ok(true)` in that case.
static NODISCARD overflow_status printer_print_maybe_open_generics(struct printer *printer, bool *open) {
    if (printer_eat(printer, 'B')) {
        // NOTE(eddyb) the closure may not run if printing is being skipped,
        // but in that case the returned boolean doesn't matter.
        *open = false;
        return printer_print_backref(printer, printer_print_maybe_open_generics, open);
    } else if(printer_eat(printer, 'I')) {
        PRINT(printer_print_path(printer, false));
        PRINT_STR(printer, "<");
        PRINT_SEP_LIST(printer, PRINT(printer_print_generic_arg(printer)), ", ");
        *open = true;
        return OverflowOk;
    } else {
        PRINT(printer_print_path(printer, false));
        *open = false;
        return OverflowOk;
    }
}

static NODISCARD overflow_status printer_print_dyn_trait(struct printer *printer) {
    bool open;
    PRINT(printer_print_maybe_open_generics(printer, &open));

    while (printer_eat(printer, 'p')) {
        if (!open) {
            PRINT_STR(printer, "<");
            open = true;
        } else {
            PRINT_STR(printer, ", ");
        }

        struct ident name;
        PARSE(printer, parser_ident, &name);

        PRINT_IDENT(printer, &name);
        PRINT_STR(printer, " = ");
        PRINT(printer_print_type(printer));
    }

    if (open) {
        PRINT_STR(printer, ">");
    }

    return OverflowOk;
}

static NODISCARD overflow_status printer_print_object_bounds(struct printer *printer) {
    PRINT_SEP_LIST(printer, PRINT(printer_print_dyn_trait(printer)), " + ");
    return OverflowOk;
}

static NODISCARD overflow_status printer_print_function_type(struct printer *printer) {
    bool is_unsafe = printer_eat(printer, 'U');
    const char *abi;
    size_t abi_len;
    if (printer_eat(printer, 'K')) {
        if (printer_eat(printer, 'C')) {
            abi = "C";
            abi_len = 1;
        } else {
            struct ident abi_ident;
            PARSE(printer, parser_ident, &abi_ident);
            if (abi_ident.ascii_len == 0 || abi_ident.punycode_len != 0) {
                INVALID(printer);
            }
            abi = abi_ident.ascii_start;
            abi_len = abi_ident.ascii_len;
        }
    } else {
        abi = NULL;
        abi_len = 0;
    }

    if (is_unsafe) {
        PRINT_STR(printer, "unsafe ");
    }

    if (abi != NULL) {
        PRINT_STR(printer, "extern \"");

        // replace _ with -
        while (abi_len > 0) {
            const char *minus = memchr(abi, '_', abi_len);
            if (minus == NULL) {
                PRINT(printer_print_buf(printer, (const char*)abi, abi_len));
                break;
            } else {
                size_t space_to_minus = minus - abi;
                PRINT(printer_print_buf(printer, (const char*)abi, space_to_minus));
                PRINT_STR(printer, "-");
                abi = minus + 1;
                abi_len -= (space_to_minus + 1);
            }
        }

        PRINT_STR(printer, "\" ");
    }

    PRINT_STR(printer, "fn(");
    PRINT_SEP_LIST(printer, PRINT(printer_print_type(printer)), ", ");
    PRINT_STR(printer, ")");

    if (printer_eat(printer, 'u')) {
        // Skip printing the return type if it's 'u', i.e. `()`.
    } else {
        PRINT_STR(printer, " -> ");
        PRINT(printer_print_type(printer));
    }

    return OverflowOk;
}

static NODISCARD overflow_status printer_print_type_backref(struct printer *printer, bool *_arg) {
    (void)_arg;
    return printer_print_type(printer);
}

static NODISCARD overflow_status printer_print_type(struct printer *printer) {
    uint8_t tag;
    PARSE(printer, parser_ch, &tag);

    const char *basic_ty = basic_type(tag);
    if (basic_ty) {
        return printer_print_str(printer, basic_ty);
    }

    uint64_t count;
    uint64_t lt;

    PARSE(printer, parser_push_depth);
    switch (tag) {
    case 'R':
    case 'Q':
        PRINT_STR(printer, "&");
        if (printer_eat(printer, 'L')) {
            PARSE(printer, parser_integer_62, &lt);
            if (lt != 0) {
                PRINT(printer_print_lifetime_from_index(printer, lt));
                PRINT_STR(printer, " ");
            }
        }
        if (tag != 'R') {
            PRINT_STR(printer, "mut ");
        }
        PRINT(printer_print_type(printer));
        break;
    case 'P':
    case 'O':
        PRINT_STR(printer, "*");
        if (tag != 'P') {
            PRINT_STR(printer, "mut ");
        } else {
            PRINT_STR(printer, "const ");
        }
        PRINT(printer_print_type(printer));
        break;
    case 'A':
    case 'S':
        PRINT_STR(printer, "[");
        PRINT(printer_print_type(printer));
        if (tag == 'A') {
            PRINT_STR(printer, "; ");
            PRINT(printer_print_const(printer, true));
        }
        PRINT_STR(printer, "]");
        break;
    case 'T':
        PRINT_STR(printer, "(");
        PRINT_SEP_LIST_COUNT(printer, count, PRINT(printer_print_type(printer)), ", ");
        if (count == 1) {
            PRINT_STR(printer, ",");
        }
        PRINT_STR(printer, ")");
        break;
    case 'F':
        PRINT(printer_in_binder(printer, printer_print_function_type));
        break;
    case 'D':
        PRINT_STR(printer, "dyn ");
        PRINT(printer_in_binder(printer, printer_print_object_bounds));

        if (!printer_eat(printer, 'L')) {
            INVALID(printer);
        }
        PARSE(printer, parser_integer_62, &lt);

        if (lt != 0) {
            PRINT_STR(printer, " + ");
            PRINT(printer_print_lifetime_from_index(printer, lt));
        }
        break;
    case 'B':
        PRINT(printer_print_backref(printer, printer_print_type_backref, NULL));
        break;
    default:
        // Go back to the tag, so `print_path` also sees it.
        if (printer->status == DemangleOk && /* safety */ printer->parser.next > 0) {
            printer->parser.next--;
        }
        PRINT(printer_print_path(printer, false));
    }

    printer_pop_depth(printer);
    return OverflowOk;
}

NODISCARD static demangle_status rust_demangle_legacy_demangle(const char *s, size_t s_len, struct demangle_legacy *res, const char **rest)
{
    if (s_len > strlen(s)) {
        // s_len only exists to shorten the string, this is not a buffer API
        return DemangleInvalid;
    }

    const char *inner;
    size_t inner_len;
    if (s_len >= 3 && !strncmp(s, "_ZN", 3)) {
        inner = s + 3;
        inner_len = s_len - 3;
    } else if (s_len >= 2 && !strncmp(s, "ZN", 2)) {
        // On Windows, dbghelp strips leading underscores, so we accept "ZN...E"
        // form too.
        inner = s + 2;
        inner_len = s_len - 2;
    } else if (s_len >= 4 && !strncmp(s, "__ZN", 4)) {
        // On OSX, symbols are prefixed with an extra _
        inner = s + 4;
        inner_len = s_len - 4;
    } else {
        return DemangleInvalid;
    }

    if (!str_isascii(inner, inner_len)) {
        return DemangleInvalid;
    }

    size_t elements = 0;
    const char *chars = inner;
    size_t chars_len = inner_len;
    if (chars_len == 0) {
        return DemangleInvalid;
    }
    char c;
    while ((c = *chars) != 'E') {
        // Decode an identifier element's length
        if (c < '0' || c > '9') {
            return DemangleInvalid;
        }
        size_t len = 0;
        while (c >= '0' && c <= '9') {
            size_t d = c - '0';
            if (len > SIZE_MAX / 10) {
                return DemangleInvalid;
            }
            len *= 10;
            if (len > SIZE_MAX - d) {
                return DemangleInvalid;
            }
            len += d;

            chars++;
            chars_len--;
            if (chars_len == 0) {
                return DemangleInvalid;
            }
            c = *chars;
        }

        // Advance by the length
        if (chars_len <= len) {
            return DemangleInvalid;
        }
        chars += len;
        chars_len -= len;
        elements++;
    }
    *res = (struct demangle_legacy) { inner, inner_len, elements };
    *rest = chars + 1;
    return DemangleOk;
}

static bool is_rust_hash(const char *s, size_t len) {
    if (len == 0 || s[0] != 'h') {
        return false;
    }

    for (size_t i = 1; i < len; i++) {
        if (!((s[i] >= '0' && s[i] <= '9') || (s[i] >= 'a' && s[i] <= 'f') || (s[i] >= 'A' && s[i] <= 'F'))) {
            return false;
        }
    }

    return true;
}

NODISCARD static overflow_status rust_demangle_legacy_display_demangle(struct demangle_legacy res, char *out, size_t len, bool alternate)
{
    struct printer printer = {
        // not actually using the parser part of the printer, just keeping it to share the format functions
        DemangleOk,
        { NULL },
        out,
        len,
        0,
        alternate
    };
    const char *inner = res.mangled;
    for (size_t element = 0; element < res.elements; element++) {
        size_t i = 0;
        const char *rest;
        for (rest = inner; rest < res.mangled + res.mangled_len && *rest >= '0' && *rest <= '9'; rest++) {
            i *= 10;
            i += *rest - '0';
        }
        if ((size_t)(res.mangled + res.mangled_len - rest) < i) {
            // safety: shouldn't reach this place if the input string is validated. bail out.
            // safety: we knwo rest <= res.mangled + res.mangled_len from the for-loop above
            break;
        }

        size_t len = i;
        inner = rest + len;

        // From here on, inner contains a pointer to the next element, rest[:len] to the current one
        if (alternate && element + 1 == res.elements && is_rust_hash(rest, i)) {
            break;
        }
        if (element != 0) {
            PRINT_STR(&printer, "::");
        }

        if (len >= 2 && !strncmp(rest, "_$", 2)) {
            rest++;
            len--;
        }

        while (len > 0) {
            if (rest[0] == '.') {
                if (len >= 2 && rest[1] == '.') {
                    PRINT_STR(&printer, "::");
                    rest += 2;
                    len -= 2;
                } else {
                    PRINT_STR(&printer, ".");
                    rest += 1;
                    len -= 1;
                }
            } else if (rest[0] == '$') {
                const char *escape = memchr(rest + 1, '$', len - 1);
                if (escape == NULL) {
                    break;
                }
                const char *escape_start = rest + 1;
                size_t escape_len = escape - (rest + 1);

                size_t next_len = len - (escape + 1 - rest);
                const char *next_rest = escape + 1;

                char ch;
                if ((escape_len == 2 && escape_start[0] == 'S' && escape_start[1] == 'P')) {
                    ch = '@';
                } else if ((escape_len == 2 && escape_start[0] == 'B' && escape_start[1] == 'P')) {
                    ch = '*';
                } else if ((escape_len == 2 && escape_start[0] == 'R' && escape_start[1] == 'F')) {
                    ch = '&';
                } else if ((escape_len == 2 && escape_start[0] == 'L' && escape_start[1] == 'T')) {
                    ch = '<';
                } else if ((escape_len == 2 && escape_start[0] == 'G' && escape_start[1] == 'T')) {
                    ch = '>';
                } else if ((escape_len == 2 && escape_start[0] == 'L' && escape_start[1] == 'P')) {
                    ch = '(';
                } else if ((escape_len == 2 && escape_start[0] == 'R' && escape_start[1] == 'P')) {
                    ch = ')';
                } else if ((escape_len == 1 && escape_start[0] == 'C')) {
                    ch = ',';
                } else {
                    if (escape_len > 1 && escape_start[0] == 'u') {
                        escape_start++;
                        escape_len--;
                        uint64_t val;
                        if (try_parse_uint(escape_start, escape_len, &val)
                            && val < UINT32_MAX
                            && validate_char((uint32_t)val))
                        {
                            if (!unicode_iscontrol(val)) {
                                uint8_t wchr[4];
                                size_t wchr_len = code_to_utf8(wchr, (uint32_t)val);
                                PRINT(printer_print_buf(&printer, (const char*)wchr, wchr_len));
                                len = next_len;
                                rest = next_rest;
                                continue;
                            }
                        }
                    }
                    break; // print the rest of this element raw
                }
                PRINT_CH(&printer, ch);
                len = next_len;
                rest = next_rest;
            } else {
                size_t j = 0;
                for (;j < len && rest[j] != '$' && rest[j] != '.';j++);
                if (j == len) {
                    break;
                }
                PRINT(printer_print_buf(&printer, rest, j));
                rest += j;
                len -= j;
            }
        }
        PRINT(printer_print_buf(&printer, rest, len));
    }

    if (printer.out_len < OVERFLOW_MARGIN) {
        return OverflowOverflow;
    }
    *printer.out = '\0';
    return OverflowOk;
}

static bool is_symbol_like(const char *s, size_t len) {
    // rust-demangle definition of symbol like: control characters and space are not symbol-like, all else is
    for (size_t i = 0; i < len; i++) {
        char ch = s[i];
        if (!(ch >= 0x21 && ch <= 0x7e)) {
            return false;
        }
    }
    return true;
}

void rust_demangle_demangle(const char *s, struct demangle *res)
{
    // During ThinLTO LLVM may import and rename internal symbols, so strip out
    // those endings first as they're one of the last manglings applied to symbol
    // names.
    const char *llvm = ".llvm.";
    const char *found_llvm = strstr(s, llvm);
    size_t s_len = strlen(s);
    if (found_llvm) {
        const char *all_hex_ptr = found_llvm + strlen(".llvm.");
        bool all_hex = true;
        for (;*all_hex_ptr;all_hex_ptr++) {
            if (!(('0' <= *all_hex_ptr && *all_hex_ptr <= '9') ||
                  ('A' <= *all_hex_ptr && *all_hex_ptr <= 'F') ||
                  *all_hex_ptr == '@')) {
                all_hex = false;
                break;
            }
        }

        if (all_hex) {
            s_len = found_llvm - s;
        }
    }

    const char *suffix;
    struct demangle_legacy legacy;
    demangle_status st = rust_demangle_legacy_demangle(s, s_len, &legacy, &suffix);
    if (st == DemangleOk) {
        *res = (struct demangle) {
            .style=DemangleStyleLegacy,
            .mangled=legacy.mangled,
            .mangled_len=legacy.mangled_len,
            .elements=legacy.elements,
            .original=s,
            .original_len=s_len,
            .suffix=suffix,
            .suffix_len=s_len - (suffix - s),
        };
    } else {
        struct demangle_v0 v0;
        st = rust_demangle_v0_demangle(s, s_len, &v0, &suffix);
        if (st == DemangleOk) {
            *res = (struct demangle) {
                .style=DemangleStyleV0,
                .mangled=v0.mangled,
                .mangled_len=v0.mangled_len,
                .elements=0,
                .original=s,
                .original_len=s_len,
                .suffix=suffix,
                .suffix_len=s_len - (suffix - s),
            };
        } else {
            *res = (struct demangle) {
                .style=DemangleStyleUnknown,
                .mangled=NULL,
                .mangled_len=0,
                .elements=0,
                .original=s,
                .original_len=s_len,
                .suffix=s,
                .suffix_len=0,
            };
        }
    }

    // Output like LLVM IR adds extra period-delimited words. See if
    // we are in that case and save the trailing words if so.
    if (res->suffix_len) {
        if (res->suffix[0] == '.' && is_symbol_like(res->suffix, res->suffix_len)) {
            // Keep the suffix
        } else {
            // Reset the suffix and invalidate the demangling
            res->style = DemangleStyleUnknown;
            res->suffix_len = 0;
        }
    }
}

bool rust_demangle_is_known(struct demangle *res) {
    return res->style != DemangleStyleUnknown;
}

overflow_status rust_demangle_display_demangle(struct demangle const *res, char *out, size_t len, bool alternate) {
    size_t original_len = res->original_len;
    size_t out_len;
    switch (res->style) {
    case DemangleStyleUnknown:
    if (len < original_len) {
        return OverflowOverflow;
    } else {
        memcpy(out, res->original, original_len);
        out += original_len;
        len -= original_len;
        break;
    }
    break;
    case DemangleStyleLegacy: {
        struct demangle_legacy legacy = {
            res->mangled,
            res->mangled_len,
            res->elements
        };
        if (rust_demangle_legacy_display_demangle(legacy, out, len, alternate) == OverflowOverflow) {
            return OverflowOverflow;
        }
        out_len = strlen(out);
        out += out_len;
        len -= out_len;
        break;
    }
    case DemangleStyleV0: {
        struct demangle_v0 v0 = {
            res->mangled,
            res->mangled_len
        };
        if (rust_demangle_v0_display_demangle(v0, out, len, alternate) == OverflowOverflow) {
            return OverflowOverflow;
        }
        out_len = strlen(out);
        out += out_len;
        len -= out_len;
        break;
    }
    }
    size_t suffix_len = res->suffix_len;
    if (len < suffix_len || len - suffix_len < OVERFLOW_MARGIN) {
        return OverflowOverflow;
    }
    memcpy(out, res->suffix, suffix_len);
    out[suffix_len] = 0;
    return OverflowOk;
}
