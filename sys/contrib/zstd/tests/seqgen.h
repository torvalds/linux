/*
 * Copyright (c) 2017-present, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under both the BSD-style license (found in the
 * LICENSE file in the root directory of this source tree) and the GPLv2 (found
 * in the COPYING file in the root directory of this source tree).
 * You may select, at your option, one of the above-listed licenses.
 */

#ifndef SEQGEN_H
#define SEQGEN_H

#define XXH_STATIC_LINKING_ONLY

#include "xxhash.h"
#include <stddef.h>   /* size_t */

typedef enum {
    SEQ_gen_ml = 0,
    SEQ_gen_ll,
    SEQ_gen_of,
    SEQ_gen_max /* Must be the last value */
} SEQ_gen_type;

/* Internal state, do not use */
typedef struct {
    XXH64_state_t xxh; /* xxh state for all the data produced so far (seed=0) */
    unsigned seed;
    int state; /* enum to control state machine (clean=0) */
    unsigned saved;
    size_t bytesLeft;
} SEQ_stream;

SEQ_stream SEQ_initStream(unsigned seed);

typedef struct {
    void* dst;
    size_t size;
    size_t pos;
} SEQ_outBuffer;

/* Returns non-zero until the current type/value has been generated.
 * Must pass the same type/value until it returns 0.
 *
 * Recommended to pick a value in the middle of the range you want, since there
 * may be some noise that causes actual results to be slightly different.
 * We try to be more accurate for smaller values.
 *
 * NOTE: Very small values don't work well (< 6).
 */
size_t SEQ_gen(SEQ_stream* stream, SEQ_gen_type type, unsigned value,
               SEQ_outBuffer* out);

/* Returns the xxhash of the data produced so far */
XXH64_hash_t SEQ_digest(SEQ_stream const* stream);

#endif /* SEQGEN_H */
