/*
 * Copyright (c) 2016-present, Yann Collet, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under both the BSD-style license (found in the
 * LICENSE file in the root directory of this source tree) and the GPLv2 (found
 * in the COPYING file in the root directory of this source tree).
 * You may select, at your option, one of the above-listed licenses.
 */

/* ***************************************************************
*  NOTES/WARNINGS
******************************************************************/
/* The streaming API defined here is deprecated.
 * Consider migrating towards ZSTD_compressStream() API in `zstd.h`
 * See 'lib/README.md'.
 *****************************************************************/


#if defined (__cplusplus)
extern "C" {
#endif

#ifndef ZSTD_BUFFERED_H_23987
#define ZSTD_BUFFERED_H_23987

/* *************************************
*  Dependencies
***************************************/
#include <stddef.h>      /* size_t */
#include "zstd.h"        /* ZSTD_CStream, ZSTD_DStream, ZSTDLIB_API */


/* ***************************************************************
*  Compiler specifics
*****************************************************************/
/* Deprecation warnings */
/* Should these warnings be a problem,
   it is generally possible to disable them,
   typically with -Wno-deprecated-declarations for gcc
   or _CRT_SECURE_NO_WARNINGS in Visual.
   Otherwise, it's also possible to define ZBUFF_DISABLE_DEPRECATE_WARNINGS */
#ifdef ZBUFF_DISABLE_DEPRECATE_WARNINGS
#  define ZBUFF_DEPRECATED(message) ZSTDLIB_API  /* disable deprecation warnings */
#else
#  if defined (__cplusplus) && (__cplusplus >= 201402) /* C++14 or greater */
#    define ZBUFF_DEPRECATED(message) [[deprecated(message)]] ZSTDLIB_API
#  elif (defined(__GNUC__) && (__GNUC__ >= 5)) || defined(__clang__)
#    define ZBUFF_DEPRECATED(message) ZSTDLIB_API __attribute__((deprecated(message)))
#  elif defined(__GNUC__) && (__GNUC__ >= 3)
#    define ZBUFF_DEPRECATED(message) ZSTDLIB_API __attribute__((deprecated))
#  elif defined(_MSC_VER)
#    define ZBUFF_DEPRECATED(message) ZSTDLIB_API __declspec(deprecated(message))
#  else
#    pragma message("WARNING: You need to implement ZBUFF_DEPRECATED for this compiler")
#    define ZBUFF_DEPRECATED(message) ZSTDLIB_API
#  endif
#endif /* ZBUFF_DISABLE_DEPRECATE_WARNINGS */


/* *************************************
*  Streaming functions
***************************************/
/* This is the easier "buffered" streaming API,
*  using an internal buffer to lift all restrictions on user-provided buffers
*  which can be any size, any place, for both input and output.
*  ZBUFF and ZSTD are 100% interoperable,
*  frames created by one can be decoded by the other one */

typedef ZSTD_CStream ZBUFF_CCtx;
ZBUFF_DEPRECATED("use ZSTD_createCStream") ZBUFF_CCtx* ZBUFF_createCCtx(void);
ZBUFF_DEPRECATED("use ZSTD_freeCStream")   size_t      ZBUFF_freeCCtx(ZBUFF_CCtx* cctx);

ZBUFF_DEPRECATED("use ZSTD_initCStream")           size_t ZBUFF_compressInit(ZBUFF_CCtx* cctx, int compressionLevel);
ZBUFF_DEPRECATED("use ZSTD_initCStream_usingDict") size_t ZBUFF_compressInitDictionary(ZBUFF_CCtx* cctx, const void* dict, size_t dictSize, int compressionLevel);

ZBUFF_DEPRECATED("use ZSTD_compressStream") size_t ZBUFF_compressContinue(ZBUFF_CCtx* cctx, void* dst, size_t* dstCapacityPtr, const void* src, size_t* srcSizePtr);
ZBUFF_DEPRECATED("use ZSTD_flushStream")    size_t ZBUFF_compressFlush(ZBUFF_CCtx* cctx, void* dst, size_t* dstCapacityPtr);
ZBUFF_DEPRECATED("use ZSTD_endStream")      size_t ZBUFF_compressEnd(ZBUFF_CCtx* cctx, void* dst, size_t* dstCapacityPtr);

/*-*************************************************
*  Streaming compression - howto
*
*  A ZBUFF_CCtx object is required to track streaming operation.
*  Use ZBUFF_createCCtx() and ZBUFF_freeCCtx() to create/release resources.
*  ZBUFF_CCtx objects can be reused multiple times.
*
*  Start by initializing ZBUF_CCtx.
*  Use ZBUFF_compressInit() to start a new compression operation.
*  Use ZBUFF_compressInitDictionary() for a compression which requires a dictionary.
*
*  Use ZBUFF_compressContinue() repetitively to consume input stream.
*  *srcSizePtr and *dstCapacityPtr can be any size.
*  The function will report how many bytes were read or written within *srcSizePtr and *dstCapacityPtr.
*  Note that it may not consume the entire input, in which case it's up to the caller to present again remaining data.
*  The content of `dst` will be overwritten (up to *dstCapacityPtr) at each call, so save its content if it matters or change @dst .
*  @return : a hint to preferred nb of bytes to use as input for next function call (it's just a hint, to improve latency)
*            or an error code, which can be tested using ZBUFF_isError().
*
*  At any moment, it's possible to flush whatever data remains within buffer, using ZBUFF_compressFlush().
*  The nb of bytes written into `dst` will be reported into *dstCapacityPtr.
*  Note that the function cannot output more than *dstCapacityPtr,
*  therefore, some content might still be left into internal buffer if *dstCapacityPtr is too small.
*  @return : nb of bytes still present into internal buffer (0 if it's empty)
*            or an error code, which can be tested using ZBUFF_isError().
*
*  ZBUFF_compressEnd() instructs to finish a frame.
*  It will perform a flush and write frame epilogue.
*  The epilogue is required for decoders to consider a frame completed.
*  Similar to ZBUFF_compressFlush(), it may not be able to output the entire internal buffer content if *dstCapacityPtr is too small.
*  In which case, call again ZBUFF_compressFlush() to complete the flush.
*  @return : nb of bytes still present into internal buffer (0 if it's empty)
*            or an error code, which can be tested using ZBUFF_isError().
*
*  Hint : _recommended buffer_ sizes (not compulsory) : ZBUFF_recommendedCInSize() / ZBUFF_recommendedCOutSize()
*  input : ZBUFF_recommendedCInSize==128 KB block size is the internal unit, use this value to reduce intermediate stages (better latency)
*  output : ZBUFF_recommendedCOutSize==ZSTD_compressBound(128 KB) + 3 + 3 : ensures it's always possible to write/flush/end a full block. Skip some buffering.
*  By using both, it ensures that input will be entirely consumed, and output will always contain the result, reducing intermediate buffering.
* **************************************************/


typedef ZSTD_DStream ZBUFF_DCtx;
ZBUFF_DEPRECATED("use ZSTD_createDStream") ZBUFF_DCtx* ZBUFF_createDCtx(void);
ZBUFF_DEPRECATED("use ZSTD_freeDStream")   size_t      ZBUFF_freeDCtx(ZBUFF_DCtx* dctx);

ZBUFF_DEPRECATED("use ZSTD_initDStream")           size_t ZBUFF_decompressInit(ZBUFF_DCtx* dctx);
ZBUFF_DEPRECATED("use ZSTD_initDStream_usingDict") size_t ZBUFF_decompressInitDictionary(ZBUFF_DCtx* dctx, const void* dict, size_t dictSize);

ZBUFF_DEPRECATED("use ZSTD_decompressStream") size_t ZBUFF_decompressContinue(ZBUFF_DCtx* dctx,
                                            void* dst, size_t* dstCapacityPtr,
                                      const void* src, size_t* srcSizePtr);

/*-***************************************************************************
*  Streaming decompression howto
*
*  A ZBUFF_DCtx object is required to track streaming operations.
*  Use ZBUFF_createDCtx() and ZBUFF_freeDCtx() to create/release resources.
*  Use ZBUFF_decompressInit() to start a new decompression operation,
*   or ZBUFF_decompressInitDictionary() if decompression requires a dictionary.
*  Note that ZBUFF_DCtx objects can be re-init multiple times.
*
*  Use ZBUFF_decompressContinue() repetitively to consume your input.
*  *srcSizePtr and *dstCapacityPtr can be any size.
*  The function will report how many bytes were read or written by modifying *srcSizePtr and *dstCapacityPtr.
*  Note that it may not consume the entire input, in which case it's up to the caller to present remaining input again.
*  The content of `dst` will be overwritten (up to *dstCapacityPtr) at each function call, so save its content if it matters, or change `dst`.
*  @return : 0 when a frame is completely decoded and fully flushed,
*            1 when there is still some data left within internal buffer to flush,
*            >1 when more data is expected, with value being a suggested next input size (it's just a hint, which helps latency),
*            or an error code, which can be tested using ZBUFF_isError().
*
*  Hint : recommended buffer sizes (not compulsory) : ZBUFF_recommendedDInSize() and ZBUFF_recommendedDOutSize()
*  output : ZBUFF_recommendedDOutSize== 128 KB block size is the internal unit, it ensures it's always possible to write a full block when decoded.
*  input  : ZBUFF_recommendedDInSize == 128KB + 3;
*           just follow indications from ZBUFF_decompressContinue() to minimize latency. It should always be <= 128 KB + 3 .
* *******************************************************************************/


/* *************************************
*  Tool functions
***************************************/
ZBUFF_DEPRECATED("use ZSTD_isError")      unsigned ZBUFF_isError(size_t errorCode);
ZBUFF_DEPRECATED("use ZSTD_getErrorName") const char* ZBUFF_getErrorName(size_t errorCode);

/** Functions below provide recommended buffer sizes for Compression or Decompression operations.
*   These sizes are just hints, they tend to offer better latency */
ZBUFF_DEPRECATED("use ZSTD_CStreamInSize")  size_t ZBUFF_recommendedCInSize(void);
ZBUFF_DEPRECATED("use ZSTD_CStreamOutSize") size_t ZBUFF_recommendedCOutSize(void);
ZBUFF_DEPRECATED("use ZSTD_DStreamInSize")  size_t ZBUFF_recommendedDInSize(void);
ZBUFF_DEPRECATED("use ZSTD_DStreamOutSize") size_t ZBUFF_recommendedDOutSize(void);

#endif  /* ZSTD_BUFFERED_H_23987 */


#ifdef ZBUFF_STATIC_LINKING_ONLY
#ifndef ZBUFF_STATIC_H_30298098432
#define ZBUFF_STATIC_H_30298098432

/* ====================================================================================
 * The definitions in this section are considered experimental.
 * They should never be used in association with a dynamic library, as they may change in the future.
 * They are provided for advanced usages.
 * Use them only in association with static linking.
 * ==================================================================================== */

/*--- Dependency ---*/
#define ZSTD_STATIC_LINKING_ONLY   /* ZSTD_parameters, ZSTD_customMem */
#include "zstd.h"


/*--- Custom memory allocator ---*/
/*! ZBUFF_createCCtx_advanced() :
 *  Create a ZBUFF compression context using external alloc and free functions */
ZBUFF_DEPRECATED("use ZSTD_createCStream_advanced") ZBUFF_CCtx* ZBUFF_createCCtx_advanced(ZSTD_customMem customMem);

/*! ZBUFF_createDCtx_advanced() :
 *  Create a ZBUFF decompression context using external alloc and free functions */
ZBUFF_DEPRECATED("use ZSTD_createDStream_advanced") ZBUFF_DCtx* ZBUFF_createDCtx_advanced(ZSTD_customMem customMem);


/*--- Advanced Streaming Initialization ---*/
ZBUFF_DEPRECATED("use ZSTD_initDStream_usingDict") size_t ZBUFF_compressInit_advanced(ZBUFF_CCtx* zbc,
                                               const void* dict, size_t dictSize,
                                               ZSTD_parameters params, unsigned long long pledgedSrcSize);


#endif    /* ZBUFF_STATIC_H_30298098432 */
#endif    /* ZBUFF_STATIC_LINKING_ONLY */


#if defined (__cplusplus)
}
#endif
