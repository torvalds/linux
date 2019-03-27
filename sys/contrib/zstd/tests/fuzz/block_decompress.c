/**
 * Copyright (c) 2016-present, Yann Collet, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under both the BSD-style license (found in the
 * LICENSE file in the root directory of this source tree) and the GPLv2 (found
 * in the COPYING file in the root directory of this source tree).
 */

/**
 * This fuzz target attempts to decompress the fuzzed data with the simple
 * decompression function to ensure the decompressor never crashes.
 */

#define ZSTD_STATIC_LINKING_ONLY

#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include "fuzz_helpers.h"
#include "zstd.h"

static ZSTD_DCtx *dctx = NULL;
static void* rBuf = NULL;
static size_t bufSize = 0;

int LLVMFuzzerTestOneInput(const uint8_t *src, size_t size)
{
    size_t const neededBufSize = ZSTD_BLOCKSIZE_MAX;

    FUZZ_seed(&src, &size);

    /* Allocate all buffers and contexts if not already allocated */
    if (neededBufSize > bufSize) {
        free(rBuf);
        rBuf = malloc(neededBufSize);
        bufSize = neededBufSize;
        FUZZ_ASSERT(rBuf);
    }
    if (!dctx) {
        dctx = ZSTD_createDCtx();
        FUZZ_ASSERT(dctx);
    }
    ZSTD_decompressBegin(dctx);
    ZSTD_decompressBlock(dctx, rBuf, neededBufSize, src, size);

#ifndef STATEFUL_FUZZING
    ZSTD_freeDCtx(dctx); dctx = NULL;
#endif
    return 0;
}
