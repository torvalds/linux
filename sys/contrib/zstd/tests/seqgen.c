/*
 * Copyright (c) 2017-present, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under both the BSD-style license (found in the
 * LICENSE file in the root directory of this source tree) and the GPLv2 (found
 * in the COPYING file in the root directory of this source tree).
 * You may select, at your option, one of the above-listed licenses.
 */

#include "seqgen.h"
#include "mem.h"
#include <string.h>

#define MIN(a, b)  ((a) < (b) ? (a) : (b))

static const size_t kMatchBytes = 128;

#define SEQ_rotl32(x,r) ((x << r) | (x >> (32 - r)))
static BYTE SEQ_randByte(unsigned* src)
{
    static const U32 prime1 = 2654435761U;
    static const U32 prime2 = 2246822519U;
    U32 rand32 = *src;
    rand32 *= prime1;
    rand32 ^= prime2;
    rand32  = SEQ_rotl32(rand32, 13);
    *src = rand32;
    return (BYTE)(rand32 >> 5);
}

SEQ_stream SEQ_initStream(unsigned seed)
{
    SEQ_stream stream;
    stream.state = 0;
    XXH64_reset(&stream.xxh, 0);
    stream.seed = seed;
    return stream;
}

/* Generates a single guard byte, then match length + 1 of a different byte,
 * then another guard byte.
 */
static size_t SEQ_gen_matchLength(SEQ_stream* stream, unsigned value,
                                  SEQ_outBuffer* out)
{
    typedef enum {
        ml_first_byte = 0,
        ml_match_bytes,
        ml_last_byte,
    } ml_state;
    BYTE* const ostart = (BYTE*)out->dst;
    BYTE* const oend = ostart + out->size;
    BYTE* op = ostart + out->pos;

    switch ((ml_state)stream->state) {
    case ml_first_byte:
        /* Generate a single byte and pick a different byte for the match */
        if (op >= oend) {
            stream->bytesLeft = 1;
            break;
        }
        *op = SEQ_randByte(&stream->seed) & 0xFF;
        do {
            stream->saved = SEQ_randByte(&stream->seed) & 0xFF;
        } while (*op == stream->saved);
        ++op;
        /* State transition */
        stream->state = ml_match_bytes;
        stream->bytesLeft = value + 1;
    /* fall-through */
    case ml_match_bytes: {
        /* Copy matchLength + 1 bytes to the output buffer */
        size_t const setLength = MIN(stream->bytesLeft, (size_t)(oend - op));
        if (setLength > 0) {
            memset(op, stream->saved, setLength);
            op += setLength;
            stream->bytesLeft -= setLength;
        }
        if (stream->bytesLeft > 0)
            break;
        /* State transition */
        stream->state = ml_last_byte;
    }
    /* fall-through */
    case ml_last_byte:
        /* Generate a single byte and pick a different byte for the match */
        if (op >= oend) {
            stream->bytesLeft = 1;
            break;
        }
        do {
            *op = SEQ_randByte(&stream->seed) & 0xFF;
        } while (*op == stream->saved);
        ++op;
        /* State transition */
    /* fall-through */
    default:
        stream->state = 0;
        stream->bytesLeft = 0;
        break;
    }
    XXH64_update(&stream->xxh, ostart + out->pos, (op - ostart) - out->pos);
    out->pos = op - ostart;
    return stream->bytesLeft;
}

/* Saves the current seed then generates kMatchBytes random bytes >= 128.
 * Generates literal length - kMatchBytes random bytes < 128.
 * Generates another kMatchBytes using the saved seed to generate a match.
 * This way the match is easy to find for the compressors.
 */
static size_t SEQ_gen_litLength(SEQ_stream* stream, unsigned value, SEQ_outBuffer* out)
{
    typedef enum {
        ll_start = 0,
        ll_run_bytes,
        ll_literals,
        ll_run_match,
    } ll_state;
    BYTE* const ostart = (BYTE*)out->dst;
    BYTE* const oend = ostart + out->size;
    BYTE* op = ostart + out->pos;

    switch ((ll_state)stream->state) {
    case ll_start:
        stream->state = ll_run_bytes;
        stream->saved = stream->seed;
        stream->bytesLeft = MIN(kMatchBytes, value);
    /* fall-through */
    case ll_run_bytes:
        while (stream->bytesLeft > 0 && op < oend) {
            *op++ = SEQ_randByte(&stream->seed) | 0x80;
            --stream->bytesLeft;
        }
        if (stream->bytesLeft > 0)
            break;
        /* State transition */
        stream->state = ll_literals;
        stream->bytesLeft = value - MIN(kMatchBytes, value);
    /* fall-through */
    case ll_literals:
        while (stream->bytesLeft > 0 && op < oend) {
            *op++ = SEQ_randByte(&stream->seed) & 0x7F;
            --stream->bytesLeft;
        }
        if (stream->bytesLeft > 0)
            break;
        /* State transition */
        stream->state = ll_run_match;
        stream->bytesLeft = MIN(kMatchBytes, value);
    /* fall-through */
    case ll_run_match: {
        while (stream->bytesLeft > 0 && op < oend) {
            *op++ = SEQ_randByte(&stream->saved) | 0x80;
            --stream->bytesLeft;
        }
        if (stream->bytesLeft > 0)
            break;
    }
    /* fall-through */
    default:
        stream->state = 0;
        stream->bytesLeft = 0;
        break;
    }
    XXH64_update(&stream->xxh, ostart + out->pos, (op - ostart) - out->pos);
    out->pos = op - ostart;
    return stream->bytesLeft;
}

/* Saves the current seed then generates kMatchBytes random bytes >= 128.
 * Generates offset - kMatchBytes of zeros to get a large offset without
 * polluting the hash tables.
 * Generates another kMatchBytes using the saved seed to generate a with the
 * required offset.
 */
static size_t SEQ_gen_offset(SEQ_stream* stream, unsigned value, SEQ_outBuffer* out)
{
    typedef enum {
        of_start = 0,
        of_run_bytes,
        of_offset,
        of_run_match,
    } of_state;
    BYTE* const ostart = (BYTE*)out->dst;
    BYTE* const oend = ostart + out->size;
    BYTE* op = ostart + out->pos;

    switch ((of_state)stream->state) {
    case of_start:
        stream->state = of_run_bytes;
        stream->saved = stream->seed;
        stream->bytesLeft = MIN(value, kMatchBytes);
    /* fall-through */
    case of_run_bytes: {
        while (stream->bytesLeft > 0 && op < oend) {
            *op++ = SEQ_randByte(&stream->seed) | 0x80;
            --stream->bytesLeft;
        }
        if (stream->bytesLeft > 0)
            break;
        /* State transition */
        stream->state = of_offset;
        stream->bytesLeft = value - MIN(value, kMatchBytes);
    }
    /* fall-through */
    case of_offset: {
        /* Copy matchLength + 1 bytes to the output buffer */
        size_t const setLength = MIN(stream->bytesLeft, (size_t)(oend - op));
        if (setLength > 0) {
            memset(op, 0, setLength);
            op += setLength;
            stream->bytesLeft -= setLength;
        }
        if (stream->bytesLeft > 0)
            break;
        /* State transition */
        stream->state = of_run_match;
        stream->bytesLeft = MIN(value, kMatchBytes);
    }
    /* fall-through */
    case of_run_match: {
        while (stream->bytesLeft > 0 && op < oend) {
            *op++ = SEQ_randByte(&stream->saved) | 0x80;
            --stream->bytesLeft;
        }
        if (stream->bytesLeft > 0)
            break;
    }
    /* fall-through */
    default:
        stream->state = 0;
        stream->bytesLeft = 0;
        break;
    }
    XXH64_update(&stream->xxh, ostart + out->pos, (op - ostart) - out->pos);
    out->pos = op - ostart;
    return stream->bytesLeft;
}

/* Returns the number of bytes left to generate.
 * Must pass the same type/value until it returns 0.
 */
size_t SEQ_gen(SEQ_stream* stream, SEQ_gen_type type, unsigned value, SEQ_outBuffer* out)
{
    switch (type) {
        case SEQ_gen_ml: return SEQ_gen_matchLength(stream, value, out);
        case SEQ_gen_ll: return SEQ_gen_litLength(stream, value, out);
        case SEQ_gen_of: return SEQ_gen_offset(stream, value, out);
        case SEQ_gen_max: /* fall-through */
        default: return 0;
    }
}

/* Returns the xxhash of the data produced so far */
XXH64_hash_t SEQ_digest(SEQ_stream const* stream)
{
    return XXH64_digest(&stream->xxh);
}
