/*
 * Copyright (c) 2017-present, Yann Collet, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under both the BSD-style license (found in the
 * LICENSE file in the root directory of this source tree) and the GPLv2 (found
 * in the COPYING file in the root directory of this source tree).
 * You may select, at your option, one of the above-listed licenses.
 */


#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdint.h>
#include "mem.h"
#define ZSTD_STATIC_LINKING_ONLY
#include "zstd.h"

static int
compress(ZSTD_CStream *ctx, ZSTD_outBuffer out, const void *data, size_t size)
{
  ZSTD_inBuffer in = { data, size, 0 };
  while (in.pos < in.size) {
    ZSTD_outBuffer tmp = out;
    const size_t rc = ZSTD_compressStream(ctx, &tmp, &in);
    if (ZSTD_isError(rc)) return 1;
  }
  { ZSTD_outBuffer tmp = out;
    const size_t rc = ZSTD_flushStream(ctx, &tmp);
    if (rc != 0) { return 1; }
  }
  return 0;
}

int main(int argc, const char** argv)
{
  ZSTD_CStream* ctx;
  ZSTD_parameters params;
  size_t rc;
  unsigned windowLog;
  (void)argc;
  (void)argv;
  /* Create stream */
  ctx = ZSTD_createCStream();
  if (!ctx) { return 1; }
  /* Set parameters */
  memset(&params, 0, sizeof(params));
  params.cParams.windowLog = 18;
  params.cParams.chainLog = 13;
  params.cParams.hashLog = 14;
  params.cParams.searchLog = 1;
  params.cParams.minMatch = 7;
  params.cParams.targetLength = 16;
  params.cParams.strategy = ZSTD_fast;
  windowLog = params.cParams.windowLog;
  /* Initialize stream */
  rc = ZSTD_initCStream_advanced(ctx, NULL, 0, params, 0);
  if (ZSTD_isError(rc)) { return 2; }
  {
    U64 compressed = 0;
    const U64 toCompress = ((U64)1) << 33;
    const size_t size = 1 << windowLog;
    size_t pos = 0;
    char *srcBuffer = (char*) malloc(1 << windowLog);
    char *dstBuffer = (char*) malloc(ZSTD_compressBound(1 << windowLog));
    ZSTD_outBuffer out = { dstBuffer, ZSTD_compressBound(1 << windowLog), 0 };
    const char match[] = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
    const size_t randomData = (1 << windowLog) - 2*sizeof(match);
    size_t i;
    printf("\n ===   Long Match Test   === \n");
    printf("Creating random data to produce long matches \n");
    for (i = 0; i < sizeof(match); ++i) {
      srcBuffer[i] = match[i];
    }
    for (i = 0; i < randomData; ++i) {
      srcBuffer[sizeof(match) + i] = (char)(rand() & 0xFF);
    }
    for (i = 0; i < sizeof(match); ++i) {
      srcBuffer[sizeof(match) + randomData + i] = match[i];
    }
    printf("Compressing, trying to generate a segfault \n");
    if (compress(ctx, out, srcBuffer, size)) {
      return 1;
    }
    compressed += size;
    while (compressed < toCompress) {
      const size_t block = rand() % (size - pos + 1);
      if (pos == size) { pos = 0; }
      if (compress(ctx, out, srcBuffer + pos, block)) {
        return 1;
      }
      pos += block;
      compressed += block;
    }
    printf("Compression completed successfully (no error triggered)\n");
    free(srcBuffer);
    free(dstBuffer);
  }
  return 0;
}
