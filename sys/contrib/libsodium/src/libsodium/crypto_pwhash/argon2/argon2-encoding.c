#include "argon2-encoding.h"
#include "argon2-core.h"
#include "utils.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Example code for a decoder and encoder of "hash strings", with Argon2
 * parameters.
 *
 * The code was originally written by Thomas Pornin <pornin@bolet.org>,
 * to whom comments and remarks may be sent. It is released under what
 * should amount to Public Domain or its closest equivalent; the
 * following mantra is supposed to incarnate that fact with all the
 * proper legal rituals:
 *
 * ---------------------------------------------------------------------
 * This file is provided under the terms of Creative Commons CC0 1.0
 * Public Domain Dedication. To the extent possible under law, the
 * author (Thomas Pornin) has waived all copyright and related or
 * neighboring rights to this file. This work is published from: Canada.
 * ---------------------------------------------------------------------
 *
 * Copyright (c) 2015 Thomas Pornin
 */

/* ==================================================================== */

/*
 * Decode decimal integer from 'str'; the value is written in '*v'.
 * Returned value is a pointer to the next non-decimal character in the
 * string. If there is no digit at all, or the value encoding is not
 * minimal (extra leading zeros), or the value does not fit in an
 * 'unsigned long', then NULL is returned.
 */
static const char *
decode_decimal(const char *str, unsigned long *v)
{
    const char    *orig;
    unsigned long  acc;

    acc = 0;
    for (orig = str;; str++) {
        int c;

        c = *str;
        if (c < '0' || c > '9') {
            break;
        }
        c -= '0';
        if (acc > (ULONG_MAX / 10)) {
            return NULL;
        }
        acc *= 10;
        if ((unsigned long) c > (ULONG_MAX - acc)) {
            return NULL;
        }
        acc += (unsigned long) c;
    }
    if (str == orig || (*orig == '0' && str != (orig + 1))) {
        return NULL;
    }
    *v = acc;
    return str;
}

/* ==================================================================== */
/*
 * Code specific to Argon2.
 *
 * The code below applies the following format:
 *
 *  $argon2<T>[$v=<num>]$m=<num>,t=<num>,p=<num>$<bin>$<bin>
 *
 * where <T> is either 'i', <num> is a decimal integer (positive, fits in an
 * 'unsigned long') and <bin> is Base64-encoded data (no '=' padding characters,
 * no newline or whitespace).
 *
 * The last two binary chunks (encoded in Base64) are, in that order,
 * the salt and the output. Both are required. The binary salt length and the
 * output length must be in the allowed ranges defined in argon2.h.
 *
 * The ctx struct must contain buffers large enough to hold the salt and pwd
 * when it is fed into decode_string.
 */

/*
 * Decode an Argon2i hash string into the provided structure 'ctx'.
 * Returned value is ARGON2_OK on success.
 */
int
decode_string(argon2_context *ctx, const char *str, argon2_type type)
{
/* Prefix checking */
#define CC(prefix)                               \
    do {                                         \
        size_t cc_len = strlen(prefix);          \
        if (strncmp(str, prefix, cc_len) != 0) { \
            return ARGON2_DECODING_FAIL;         \
        }                                        \
        str += cc_len;                           \
    } while ((void) 0, 0)

/* Optional prefix checking with supplied code */
#define CC_opt(prefix, code)                     \
    do {                                         \
        size_t cc_len = strlen(prefix);          \
        if (strncmp(str, prefix, cc_len) == 0) { \
            str += cc_len;                       \
            {                                    \
                code;                            \
            }                                    \
        }                                        \
    } while ((void) 0, 0)

/* Decoding prefix into decimal */
#define DECIMAL(x)                         \
    do {                                   \
        unsigned long dec_x;               \
        str = decode_decimal(str, &dec_x); \
        if (str == NULL) {                 \
            return ARGON2_DECODING_FAIL;   \
        }                                  \
        (x) = dec_x;                       \
    } while ((void) 0, 0)

/* Decoding prefix into uint32_t decimal */
#define DECIMAL_U32(x)                           \
    do {                                         \
        unsigned long dec_x;                     \
        str = decode_decimal(str, &dec_x);       \
        if (str == NULL || dec_x > UINT32_MAX) { \
            return ARGON2_DECODING_FAIL;         \
        }                                        \
        (x) = (uint32_t)dec_x;                   \
    } while ((void)0, 0)

/* Decoding base64 into a binary buffer */
#define BIN(buf, max_len, len)                                                   \
    do {                                                                         \
        size_t bin_len = (max_len);                                              \
        const char *str_end;                                                     \
        if (sodium_base642bin((buf), (max_len), str, strlen(str), NULL,          \
                              &bin_len, &str_end,                                \
                              sodium_base64_VARIANT_ORIGINAL_NO_PADDING) != 0 || \
            bin_len > UINT32_MAX) {                                              \
            return ARGON2_DECODING_FAIL;                                         \
        }                                                                        \
        (len) = (uint32_t) bin_len;                                              \
        str = str_end;                                                           \
    } while ((void) 0, 0)

    size_t        maxsaltlen = ctx->saltlen;
    size_t        maxoutlen  = ctx->outlen;
    int           validation_result;
    uint32_t      version = 0;

    ctx->saltlen = 0;
    ctx->outlen  = 0;

    if (type == Argon2_id) {
        CC("$argon2id");
    } else if (type == Argon2_i) {
        CC("$argon2i");
    } else {
        return ARGON2_INCORRECT_TYPE;
    }
    CC("$v=");
    DECIMAL_U32(version);
    if (version != ARGON2_VERSION_NUMBER) {
        return ARGON2_INCORRECT_TYPE;
    }
    CC("$m=");
    DECIMAL_U32(ctx->m_cost);
    if (ctx->m_cost > UINT32_MAX) {
        return ARGON2_INCORRECT_TYPE;
    }
    CC(",t=");
    DECIMAL_U32(ctx->t_cost);
    if (ctx->t_cost > UINT32_MAX) {
        return ARGON2_INCORRECT_TYPE;
    }
    CC(",p=");
    DECIMAL_U32(ctx->lanes);
    if (ctx->lanes > UINT32_MAX) {
        return ARGON2_INCORRECT_TYPE;
    }
    ctx->threads = ctx->lanes;

    CC("$");
    BIN(ctx->salt, maxsaltlen, ctx->saltlen);
    CC("$");
    BIN(ctx->out, maxoutlen, ctx->outlen);
    validation_result = validate_inputs(ctx);
    if (validation_result != ARGON2_OK) {
        return validation_result;
    }
    if (*str == 0) {
        return ARGON2_OK;
    }
    return ARGON2_DECODING_FAIL;

#undef CC
#undef CC_opt
#undef DECIMAL
#undef BIN
}

#define U32_STR_MAXSIZE 11U

static void
u32_to_string(char *str, uint32_t x)
{
    char   tmp[U32_STR_MAXSIZE - 1U];
    size_t i;

    i = sizeof tmp;
    do {
        tmp[--i] = (x % (uint32_t) 10U) + '0';
        x /= (uint32_t) 10U;
    } while (x != 0U && i != 0U);
    memcpy(str, &tmp[i], (sizeof tmp) - i);
    str[(sizeof tmp) - i] = 0;
}

/*
 * Encode an argon2i hash string into the provided buffer. 'dst_len'
 * contains the size, in characters, of the 'dst' buffer; if 'dst_len'
 * is less than the number of required characters (including the
 * terminating 0), then this function returns 0.
 *
 * If pp->output_len is 0, then the hash string will be a salt string
 * (no output). if pp->salt_len is also 0, then the string will be a
 * parameter-only string (no salt and no output).
 *
 * On success, ARGON2_OK is returned.
 */
int
encode_string(char *dst, size_t dst_len, argon2_context *ctx, argon2_type type)
{
#define SS(str)                          \
    do {                                 \
        size_t pp_len = strlen(str);     \
        if (pp_len >= dst_len) {         \
            return ARGON2_ENCODING_FAIL; \
        }                                \
        memcpy(dst, str, pp_len + 1);    \
        dst += pp_len;                   \
        dst_len -= pp_len;               \
    } while ((void) 0, 0)

#define SX(x)                      \
    do {                           \
        char tmp[U32_STR_MAXSIZE]; \
        u32_to_string(tmp, x);     \
        SS(tmp);                   \
    } while ((void) 0, 0)

#define SB(buf, len)                                                                \
    do {                                                                            \
        size_t sb_len;                                                              \
        if (sodium_bin2base64(dst, dst_len, (buf), (len),                           \
                              sodium_base64_VARIANT_ORIGINAL_NO_PADDING) == NULL) { \
            return ARGON2_ENCODING_FAIL;                                            \
        }                                                                           \
        sb_len = strlen(dst);                                                       \
        dst += sb_len;                                                              \
        dst_len -= sb_len;                                                          \
    } while ((void) 0, 0)

    int validation_result;

    switch (type) {
    case Argon2_id:
        SS("$argon2id$v="); break;
    case Argon2_i:
        SS("$argon2i$v="); break;
    default:
        return ARGON2_ENCODING_FAIL;
    }
    validation_result = validate_inputs(ctx);
    if (validation_result != ARGON2_OK) {
        return validation_result;
    }
    SX(ARGON2_VERSION_NUMBER);
    SS("$m=");
    SX(ctx->m_cost);
    SS(",t=");
    SX(ctx->t_cost);
    SS(",p=");
    SX(ctx->lanes);

    SS("$");
    SB(ctx->salt, ctx->saltlen);

    SS("$");
    SB(ctx->out, ctx->outlen);
    return ARGON2_OK;

#undef SS
#undef SX
#undef SB
}
