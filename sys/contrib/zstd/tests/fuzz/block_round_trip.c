/**
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
#include "zstd.h"

static const int kMaxClevel = 19;

static ZSTD_CCtx *cctx = NULL;
static ZSTD_DCtx *dctx = NULL;
static void* cBuf = NULL;
static void* rBuf = NULL;
static size_t bufSize = 0;
static uint32_t seed;

static size_t roundTripTest(void *result, size_t resultCapacity,
                            void *compressed, size_t compressedCapacity,
                            const void *src, size_t srcSize)
{
    int const cLevel = FUZZ_rand(&seed) % kMaxClevel;
    ZSTD_parameters const params = ZSTD_getParams(cLevel, srcSize, 0);
    size_t ret = ZSTD_compressBegin_advanced(cctx, NULL, 0, params, srcSize);
    FUZZ_ZASSERT(ret);

    ret = ZSTD_compressBlock(cctx, compressed, compressedCapacity, src, srcSize);
    FUZZ_ZASSERT(ret);
    if (ret == 0) {
        FUZZ_ASSERT(resultCapacity >= srcSize);
        memcpy(result, src, srcSize);
        return srcSize;
    }
    ZSTD_decompressBegin(dctx);
    return ZSTD_decompressBlock(dctx, result, resultCapacity, compressed, ret);
}

int LLVMFuzzerTestOneInput(const uint8_t *src, size_t size)
{
    size_t neededBufSize;

    seed = FUZZ_seed(&src, &size);
    neededBufSize = size;
    if (size > ZSTD_BLOCKSIZE_MAX)
        return 0;

    /* Allocate all buffers and contexts if not already allocated */
    if (neededBufSize > bufSize || !cBuf || !rBuf) {
        free(cBuf);
        free(rBuf);
        cBuf = malloc(neededBufSize);
        rBuf = malloc(neededBufSize);
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
        size_t const result =
            roundTripTest(rBuf, neededBufSize, cBuf, neededBufSize, src, size);
        FUZZ_ZASSERT(result);
        FUZZ_ASSERT_MSG(result == size, "Incorrect regenerated size");
        FUZZ_ASSERT_MSG(!memcmp(src, rBuf, size), "Corruption!");
    }
#ifndef STATEFUL_FUZZING
    ZSTD_freeCCtx(cctx); cctx = NULL;
    ZSTD_freeDCtx(dctx); dctx = NULL;
#endif
    return 0;
}
