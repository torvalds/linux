/*
 * Copyright (c) 2016-present, Yann Collet, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under both the BSD-style license (found in the
 * LICENSE file in the root directory of this source tree) and the GPLv2 (found
 * in the COPYING file in the root directory of this source tree).
 * You may select, at your option, one of the above-listed licenses.
 */


#include <stdio.h>
#include "zstd_errors.h"
#define ZSTD_STATIC_LINKING_ONLY
#include "zstd.h"
#define ZBUFF_DISABLE_DEPRECATE_WARNINGS
#define ZBUFF_STATIC_LINKING_ONLY
#include "zbuff.h"
#define ZDICT_DISABLE_DEPRECATE_WARNINGS
#define ZDICT_STATIC_LINKING_ONLY
#include "zdict.h"

static const void *symbols[] = {
/* zstd.h */
  &ZSTD_versionNumber,
  &ZSTD_compress,
  &ZSTD_decompress,
  &ZSTD_getDecompressedSize,
  &ZSTD_findDecompressedSize,
  &ZSTD_findFrameCompressedSize,
  &ZSTD_getFrameContentSize,
  &ZSTD_maxCLevel,
  &ZSTD_compressBound,
  &ZSTD_isError,
  &ZSTD_getErrorName,
  &ZSTD_createCCtx,
  &ZSTD_freeCCtx,
  &ZSTD_compressCCtx,
  &ZSTD_createDCtx,
  &ZSTD_freeDCtx,
  &ZSTD_decompressDCtx,
  &ZSTD_compress_usingDict,
  &ZSTD_decompress_usingDict,
  &ZSTD_createCDict,
  &ZSTD_freeCDict,
  &ZSTD_compress_usingCDict,
  &ZSTD_createDDict,
  &ZSTD_freeDDict,
  &ZSTD_decompress_usingDDict,
  &ZSTD_createCStream,
  &ZSTD_freeCStream,
  &ZSTD_initCStream,
  &ZSTD_compressStream,
  &ZSTD_flushStream,
  &ZSTD_endStream,
  &ZSTD_CStreamInSize,
  &ZSTD_CStreamOutSize,
  &ZSTD_createDStream,
  &ZSTD_freeDStream,
  &ZSTD_initDStream,
  &ZSTD_decompressStream,
  &ZSTD_DStreamInSize,
  &ZSTD_DStreamOutSize,
/* zstd.h: advanced functions */
  &ZSTD_estimateCCtxSize,
  &ZSTD_createCCtx_advanced,
  &ZSTD_sizeof_CCtx,
  &ZSTD_createCDict_advanced,
  &ZSTD_sizeof_CDict,
  &ZSTD_getCParams,
  &ZSTD_getParams,
  &ZSTD_checkCParams,
  &ZSTD_adjustCParams,
  &ZSTD_compress_advanced,
  &ZSTD_isFrame,
  &ZSTD_estimateDCtxSize,
  &ZSTD_createDCtx_advanced,
  &ZSTD_sizeof_DCtx,
  &ZSTD_sizeof_DDict,
  &ZSTD_getDictID_fromDict,
  &ZSTD_getDictID_fromDDict,
  &ZSTD_getDictID_fromFrame,
  &ZSTD_createCStream_advanced,
  &ZSTD_initCStream_srcSize,
  &ZSTD_initCStream_usingDict,
  &ZSTD_initCStream_advanced,
  &ZSTD_initCStream_usingCDict,
  &ZSTD_resetCStream,
  &ZSTD_sizeof_CStream,
  &ZSTD_createDStream_advanced,
  &ZSTD_initDStream_usingDict,
  &ZSTD_initDStream_usingDDict,
  &ZSTD_resetDStream,
  &ZSTD_sizeof_DStream,
  &ZSTD_compressBegin,
  &ZSTD_compressBegin_usingDict,
  &ZSTD_compressBegin_advanced,
  &ZSTD_copyCCtx,
  &ZSTD_compressContinue,
  &ZSTD_compressEnd,
  &ZSTD_getFrameHeader,
  &ZSTD_decompressBegin,
  &ZSTD_decompressBegin_usingDict,
  &ZSTD_copyDCtx,
  &ZSTD_nextSrcSizeToDecompress,
  &ZSTD_decompressContinue,
  &ZSTD_nextInputType,
  &ZSTD_getBlockSize,
  &ZSTD_compressBlock,
  &ZSTD_decompressBlock,
  &ZSTD_insertBlock,
/* zstd_errors.h */
  &ZSTD_getErrorCode,
  &ZSTD_getErrorString,
/* zbuff.h */
  &ZBUFF_createCCtx,
  &ZBUFF_freeCCtx,
  &ZBUFF_compressInit,
  &ZBUFF_compressInitDictionary,
  &ZBUFF_compressContinue,
  &ZBUFF_compressFlush,
  &ZBUFF_compressEnd,
  &ZBUFF_createDCtx,
  &ZBUFF_freeDCtx,
  &ZBUFF_decompressInit,
  &ZBUFF_decompressInitDictionary,
  &ZBUFF_decompressContinue,
  &ZBUFF_isError,
  &ZBUFF_getErrorName,
  &ZBUFF_recommendedCInSize,
  &ZBUFF_recommendedCOutSize,
  &ZBUFF_recommendedDInSize,
  &ZBUFF_recommendedDOutSize,
/* zbuff.h: advanced functions */
  &ZBUFF_createCCtx_advanced,
  &ZBUFF_createDCtx_advanced,
  &ZBUFF_compressInit_advanced,
/* zdict.h */
  &ZDICT_trainFromBuffer,
  &ZDICT_getDictID,
  &ZDICT_isError,
  &ZDICT_getErrorName,
/* zdict.h: advanced functions */
  &ZDICT_trainFromBuffer_cover,
  &ZDICT_optimizeTrainFromBuffer_cover,
  &ZDICT_trainFromBuffer_fastCover,
  &ZDICT_optimizeTrainFromBuffer_fastCover,
  &ZDICT_finalizeDictionary,
  &ZDICT_trainFromBuffer_legacy,
  &ZDICT_addEntropyTablesFromBuffer,
  NULL,
};

int main(int argc, const char** argv) {
  const void **symbol;
  (void)argc;
  (void)argv;

  for (symbol = symbols; *symbol != NULL; ++symbol) {
    printf("%p\n", *symbol);
  }
  return 0;
}
