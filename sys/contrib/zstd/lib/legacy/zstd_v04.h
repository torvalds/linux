/*
 * Copyright (c) 2016-present, Yann Collet, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under both the BSD-style license (found in the
 * LICENSE file in the root directory of this source tree) and the GPLv2 (found
 * in the COPYING file in the root directory of this source tree).
 * You may select, at your option, one of the above-listed licenses.
 */

#ifndef ZSTD_V04_H_91868324769238
#define ZSTD_V04_H_91868324769238

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
ZSTDv04_decompress() : decompress ZSTD frames compliant with v0.4.x format
    compressedSize : is the exact source size
    maxOriginalSize : is the size of the 'dst' buffer, which must be already allocated.
                      It must be equal or larger than originalSize, otherwise decompression will fail.
    return : the number of bytes decompressed into destination buffer (originalSize)
             or an errorCode if it fails (which can be tested using ZSTDv01_isError())
*/
size_t ZSTDv04_decompress( void* dst, size_t maxOriginalSize,
                     const void* src, size_t compressedSize);

/**
ZSTDv04_getFrameSrcSize() : get the source length of a ZSTD frame compliant with v0.4.x format
    compressedSize : The size of the 'src' buffer, at least as large as the frame pointed to by 'src'
    return : the number of bytes that would be read to decompress this frame
             or an errorCode if it fails (which can be tested using ZSTDv04_isError())
*/
size_t ZSTDv04_findFrameCompressedSize(const void* src, size_t compressedSize);

/**
ZSTDv04_isError() : tells if the result of ZSTDv04_decompress() is an error
*/
unsigned ZSTDv04_isError(size_t code);


/* *************************************
*  Advanced functions
***************************************/
typedef struct ZSTDv04_Dctx_s ZSTDv04_Dctx;
ZSTDv04_Dctx* ZSTDv04_createDCtx(void);
size_t ZSTDv04_freeDCtx(ZSTDv04_Dctx* dctx);

size_t ZSTDv04_decompressDCtx(ZSTDv04_Dctx* dctx,
                              void* dst, size_t maxOriginalSize,
                        const void* src, size_t compressedSize);


/* *************************************
*  Direct Streaming
***************************************/
size_t ZSTDv04_resetDCtx(ZSTDv04_Dctx* dctx);

size_t ZSTDv04_nextSrcSizeToDecompress(ZSTDv04_Dctx* dctx);
size_t ZSTDv04_decompressContinue(ZSTDv04_Dctx* dctx, void* dst, size_t maxDstSize, const void* src, size_t srcSize);
/**
  Use above functions alternatively.
  ZSTD_nextSrcSizeToDecompress() tells how much bytes to provide as 'srcSize' to ZSTD_decompressContinue().
  ZSTD_decompressContinue() will use previous data blocks to improve compression if they are located prior to current block.
  Result is the number of bytes regenerated within 'dst'.
  It can be zero, which is not an error; it just means ZSTD_decompressContinue() has decoded some header.
*/


/* *************************************
*  Buffered Streaming
***************************************/
typedef struct ZBUFFv04_DCtx_s ZBUFFv04_DCtx;
ZBUFFv04_DCtx* ZBUFFv04_createDCtx(void);
size_t         ZBUFFv04_freeDCtx(ZBUFFv04_DCtx* dctx);

size_t ZBUFFv04_decompressInit(ZBUFFv04_DCtx* dctx);
size_t ZBUFFv04_decompressWithDictionary(ZBUFFv04_DCtx* dctx, const void* dict, size_t dictSize);

size_t ZBUFFv04_decompressContinue(ZBUFFv04_DCtx* dctx, void* dst, size_t* maxDstSizePtr, const void* src, size_t* srcSizePtr);

/** ************************************************
*  Streaming decompression
*
*  A ZBUFF_DCtx object is required to track streaming operation.
*  Use ZBUFF_createDCtx() and ZBUFF_freeDCtx() to create/release resources.
*  Use ZBUFF_decompressInit() to start a new decompression operation.
*  ZBUFF_DCtx objects can be reused multiple times.
*
*  Optionally, a reference to a static dictionary can be set, using ZBUFF_decompressWithDictionary()
*  It must be the same content as the one set during compression phase.
*  Dictionary content must remain accessible during the decompression process.
*
*  Use ZBUFF_decompressContinue() repetitively to consume your input.
*  *srcSizePtr and *maxDstSizePtr can be any size.
*  The function will report how many bytes were read or written by modifying *srcSizePtr and *maxDstSizePtr.
*  Note that it may not consume the entire input, in which case it's up to the caller to present remaining input again.
*  The content of dst will be overwritten (up to *maxDstSizePtr) at each function call, so save its content if it matters or change dst.
*  @return : a hint to preferred nb of bytes to use as input for next function call (it's only a hint, to improve latency)
*            or 0 when a frame is completely decoded
*            or an error code, which can be tested using ZBUFF_isError().
*
*  Hint : recommended buffer sizes (not compulsory) : ZBUFF_recommendedDInSize / ZBUFF_recommendedDOutSize
*  output : ZBUFF_recommendedDOutSize==128 KB block size is the internal unit, it ensures it's always possible to write a full block when it's decoded.
*  input : ZBUFF_recommendedDInSize==128Kb+3; just follow indications from ZBUFF_decompressContinue() to minimize latency. It should always be <= 128 KB + 3 .
* **************************************************/
unsigned ZBUFFv04_isError(size_t errorCode);
const char* ZBUFFv04_getErrorName(size_t errorCode);


/** The below functions provide recommended buffer sizes for Compression or Decompression operations.
*   These sizes are not compulsory, they just tend to offer better latency */
size_t ZBUFFv04_recommendedDInSize(void);
size_t ZBUFFv04_recommendedDOutSize(void);


/* *************************************
*  Prefix - version detection
***************************************/
#define ZSTDv04_magicNumber 0xFD2FB524   /* v0.4 */


#if defined (__cplusplus)
}
#endif

#endif /* ZSTD_V04_H_91868324769238 */
