/*
 * Copyright (c) 2016-present, Facebook, Inc.
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

static size_t const kBufSize = ZSTD_BLOCKSIZE_MAX;

static ZSTD_DStream *dstream = NULL;
static void* buf = NULL;
uint32_t seed;

static ZSTD_outBuffer makeOutBuffer(void)
{
  ZSTD_outBuffer buffer = { buf, 0, 0 };

  buffer.size = (FUZZ_rand(&seed) % kBufSize) + 1;
  FUZZ_ASSERT(buffer.size <= kBufSize);

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

int LLVMFuzzerTestOneInput(const uint8_t *src, size_t size)
{
    seed = FUZZ_seed(&src, &size);

    /* Allocate all buffers and contexts if not already allocated */
    if (!buf) {
      buf = malloc(kBufSize);
      FUZZ_ASSERT(buf);
    }

    if (!dstream) {
        dstream = ZSTD_createDStream();
        FUZZ_ASSERT(dstream);
        FUZZ_ASSERT(!ZSTD_isError(ZSTD_initDStream(dstream)));
    } else {
        FUZZ_ASSERT(!ZSTD_isError(ZSTD_resetDStream(dstream)));
    }

    while (size > 0) {
        ZSTD_inBuffer in = makeInBuffer(&src, &size);
        while (in.pos != in.size) {
            ZSTD_outBuffer out = makeOutBuffer();
            size_t const rc = ZSTD_decompressStream(dstream, &out, &in);
            if (ZSTD_isError(rc)) goto error;
            if (rc == 0) FUZZ_ASSERT(!ZSTD_isError(ZSTD_resetDStream(dstream)));
        }
    }

error:
#ifndef STATEFUL_FUZZING
    ZSTD_freeDStream(dstream); dstream = NULL;
#endif
    return 0;
}
