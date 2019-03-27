/*
 * Copyright (c) 2016-present, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under both the BSD-style license (found in the
 * LICENSE file in the root directory of this source tree) and the GPLv2 (found
 * in the COPYING file in the root directory of this source tree).
 */

/**
 * This fuzz target performs a zstd round-trip test (compress & decompress),
 * compares the result with the original, and calls abort() on corruption.
 */

#define ZSTD_STATIC_LINKING_ONLY

#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "fuzz_helpers.h"
#include "zstd_helpers.h"

ZSTD_CCtx *cctx = NULL;
static ZSTD_DCtx *dctx = NULL;
static uint8_t* cBuf = NULL;
static uint8_t* rBuf = NULL;
static size_t bufSize = 0;
static uint32_t seed;

static ZSTD_outBuffer makeOutBuffer(uint8_t *dst, size_t capacity)
{
    ZSTD_outBuffer buffer = { dst, 0, 0 };

    FUZZ_ASSERT(capacity > 0);
    buffer.size = (FUZZ_rand(&seed) % capacity) + 1;
    FUZZ_ASSERT(buffer.size <= capacity);

    return buffer;
}

static ZSTD_inBuffer makeInBuffer(const uint8_t **src, size_t *size)
{
    ZSTD_inBuffer buffer = { *src, 0, 0 };

    FUZZ_ASSERT(*size > 0);
    buffer.size = (FUZZ_rand(&seed) % *size) + 1;
    FUZZ_ASSERT(buffer.size <= *size);
    *src += buffer.size;
    *size -= buffer.size;

    return buffer;
}

static size_t compress(uint8_t *dst, size_t capacity,
                       const uint8_t *src, size_t srcSize)
{
    size_t dstSize = 0;
    ZSTD_CCtx_reset(cctx, ZSTD_reset_session_only);
    FUZZ_setRandomParameters(cctx, srcSize, &seed);

    while (srcSize > 0) {
        ZSTD_inBuffer in = makeInBuffer(&src, &srcSize);
        /* Mode controls the action. If mode == -1 we pick a new mode */
        int mode = -1;
        while (in.pos < in.size) {
            ZSTD_outBuffer out = makeOutBuffer(dst, capacity);
            /* Previous action finished, pick a new mode. */
            if (mode == -1) mode = FUZZ_rand(&seed) % 10;
            switch (mode) {
                case 0: /* fall-though */
                case 1: /* fall-though */
                case 2: {
                    size_t const ret =
                        ZSTD_compressStream2(cctx, &out, &in, ZSTD_e_flush);
                    FUZZ_ZASSERT(ret);
                    if (ret == 0)
                        mode = -1;
                    break;
                }
                case 3: {
                    size_t ret =
                        ZSTD_compressStream2(cctx, &out, &in, ZSTD_e_end);
                    FUZZ_ZASSERT(ret);
                    /* Reset the compressor when the frame is finished */
                    if (ret == 0) {
                        ZSTD_CCtx_reset(cctx, ZSTD_reset_session_only);
                        if ((FUZZ_rand(&seed) & 7) == 0) {
                            size_t const remaining = in.size - in.pos;
                            FUZZ_setRandomParameters(cctx, remaining, &seed);
                        }
                        mode = -1;
                    }
                    break;
                }
                default: {
                    size_t const ret =
                        ZSTD_compressStream2(cctx, &out, &in, ZSTD_e_continue);
                    FUZZ_ZASSERT(ret);
                    mode = -1;
                }
            }
            dst += out.pos;
            dstSize += out.pos;
            capacity -= out.pos;
        }
    }
    for (;;) {
        ZSTD_inBuffer in = {NULL, 0, 0};
        ZSTD_outBuffer out = makeOutBuffer(dst, capacity);
        size_t const ret = ZSTD_compressStream2(cctx, &out, &in, ZSTD_e_end);
        FUZZ_ZASSERT(ret);

        dst += out.pos;
        dstSize += out.pos;
        capacity -= out.pos;
        if (ret == 0)
            break;
    }
    return dstSize;
}

int LLVMFuzzerTestOneInput(const uint8_t *src, size_t size)
{
    size_t neededBufSize;

    seed = FUZZ_seed(&src, &size);
    neededBufSize = ZSTD_compressBound(size) * 2;

    /* Allocate all buffers and contexts if not already allocated */
    if (neededBufSize > bufSize) {
        free(cBuf);
        free(rBuf);
        cBuf = (uint8_t*)malloc(neededBufSize);
        rBuf = (uint8_t*)malloc(neededBufSize);
        bufSize = neededBufSize;
        FUZZ_ASSERT(cBuf && rBuf);
    }
    if (!cctx) {
        cctx = ZSTD_createCCtx();
        FUZZ_ASSERT(cctx);
    }
    if (!dctx) {
        dctx = ZSTD_createDCtx();
        FUZZ_ASSERT(dctx);
    }

    {
        size_t const cSize = compress(cBuf, neededBufSize, src, size);
        size_t const rSize =
            ZSTD_decompressDCtx(dctx, rBuf, neededBufSize, cBuf, cSize);
        FUZZ_ZASSERT(rSize);
        FUZZ_ASSERT_MSG(rSize == size, "Incorrect regenerated size");
        FUZZ_ASSERT_MSG(!memcmp(src, rBuf, size), "Corruption!");
    }

#ifndef STATEFUL_FUZZING
    ZSTD_freeCCtx(cctx); cctx = NULL;
    ZSTD_freeDCtx(dctx); dctx = NULL;
#endif
    return 0;
}
