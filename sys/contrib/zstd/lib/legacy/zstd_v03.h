/*
 * Copyright (c) 2016-present, Yann Collet, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under both the BSD-style license (found in the
 * LICENSE file in the root directory of this source tree) and the GPLv2 (found
 * in the COPYING file in the root directory of this source tree).
 * You may select, at your option, one of the above-listed licenses.
 */

#ifndef ZSTD_V03_H_298734209782
#define ZSTD_V03_H_298734209782

#if defined (__cplusplus)
extern "C" {
#endif

/* *************************************
*  Includes
***************************************/
#include <stddef.h>   /* size_t */


/* *************************************
*  Simple one-step function
***************************************/
/**
ZSTDv03_decompress() : decompress ZSTD frames compliant with v0.3.x format
    compressedSize : is the exact source size
    maxOriginalSize : is the size of the 'dst' buffer, which must be already allocated.
                      It must be equal or larger than originalSize, otherwise decompression will fail.
    return : the number of bytes decompressed into destination buffer (originalSize)
             or an errorCode if it fails (which can be tested using ZSTDv01_isError())
*/
size_t ZSTDv03_decompress( void* dst, size_t maxOriginalSize,
                     const void* src, size_t compressedSize);

/**
ZSTDv03_getFrameSrcSize() : get the source length of a ZSTD frame compliant with v0.3.x format
    compressedSize : The size of the 'src' buffer, at least as large as the frame pointed to by 'src'
    return : the number of bytes that would be read to decompress this frame
             or an errorCode if it fails (which can be tested using ZSTDv03_isError())
*/
size_t ZSTDv03_findFrameCompressedSize(const void* src, size_t compressedSize);

    /**
ZSTDv03_isError() : tells if the result of ZSTDv03_decompress() is an error
*/
unsigned ZSTDv03_isError(size_t code);


/* *************************************
*  Advanced functions
***************************************/
typedef struct ZSTDv03_Dctx_s ZSTDv03_Dctx;
ZSTDv03_Dctx* ZSTDv03_createDCtx(void);
size_t ZSTDv03_freeDCtx(ZSTDv03_Dctx* dctx);

size_t ZSTDv03_decompressDCtx(void* ctx,
                              void* dst, size_t maxOriginalSize,
                        const void* src, size_t compressedSize);

/* *************************************
*  Streaming functions
***************************************/
size_t ZSTDv03_resetDCtx(ZSTDv03_Dctx* dctx);

size_t ZSTDv03_nextSrcSizeToDecompress(ZSTDv03_Dctx* dctx);
size_t ZSTDv03_decompressContinue(ZSTDv03_Dctx* dctx, void* dst, size_t maxDstSize, const void* src, size_t srcSize);
/**
  Use above functions alternatively.
  ZSTD_nextSrcSizeToDecompress() tells how much bytes to provide as 'srcSize' to ZSTD_decompressContinue().
  ZSTD_decompressContinue() will use previous data blocks to improve compression if they are located prior to current block.
  Result is the number of bytes regenerated within 'dst'.
  It can be zero, which is not an error; it just means ZSTD_decompressContinue() has decoded some header.
*/

/* *************************************
*  Prefix - version detection
***************************************/
#define ZSTDv03_magicNumber 0xFD2FB523   /* v0.3 */


#if defined (__cplusplus)
}
#endif

#endif /* ZSTD_V03_H_298734209782 */
