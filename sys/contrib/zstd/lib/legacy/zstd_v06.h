/*
 * Copyright (c) 2016-present, Yann Collet, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under both the BSD-style license (found in the
 * LICENSE file in the root directory of this source tree) and the GPLv2 (found
 * in the COPYING file in the root directory of this source tree).
 * You may select, at your option, one of the above-listed licenses.
 */

#ifndef ZSTDv06_H
#define ZSTDv06_H

#if defined (__cplusplus)
extern "C" {
#endif

/*======  Dependency  ======*/
#include <stddef.h>   /* size_t */


/*======  Export for Windows  ======*/
/*!
*  ZSTDv06_DLL_EXPORT :
*  Enable exporting of functions when building a Windows DLL
*/
#if defined(_WIN32) && defined(ZSTDv06_DLL_EXPORT) && (ZSTDv06_DLL_EXPORT==1)
#  define ZSTDLIBv06_API __declspec(dllexport)
#else
#  define ZSTDLIBv06_API
#endif


/* *************************************
*  Simple functions
***************************************/
/*! ZSTDv06_decompress() :
    `compressedSize` : is the _exact_ size of the compressed blob, otherwise decompression will fail.
    `dstCapacity` must be large enough, equal or larger than originalSize.
    @return : the number of bytes decompressed into `dst` (<= `dstCapacity`),
              or an errorCode if it fails (which can be tested using ZSTDv06_isError()) */
ZSTDLIBv06_API size_t ZSTDv06_decompress( void* dst, size_t dstCapacity,
                                    const void* src, size_t compressedSize);

/**
ZSTDv06_getFrameSrcSize() : get the source length of a ZSTD frame
    compressedSize : The size of the 'src' buffer, at least as large as the frame pointed to by 'src'
    return : the number of bytes that would be read to decompress this frame
             or an errorCode if it fails (which can be tested using ZSTDv06_isError())
*/
size_t ZSTDv06_findFrameCompressedSize(const void* src, size_t compressedSize);

/* *************************************
*  Helper functions
***************************************/
ZSTDLIBv06_API size_t      ZSTDv06_compressBound(size_t srcSize); /*!< maximum compressed size (worst case scenario) */

/* Error Management */
ZSTDLIBv06_API unsigned    ZSTDv06_isError(size_t code);          /*!< tells if a `size_t` function result is an error code */
ZSTDLIBv06_API const char* ZSTDv06_getErrorName(size_t code);     /*!< provides readable string for an error code */


/* *************************************
*  Explicit memory management
***************************************/
/** Decompression context */
typedef struct ZSTDv06_DCtx_s ZSTDv06_DCtx;
ZSTDLIBv06_API ZSTDv06_DCtx* ZSTDv06_createDCtx(void);
ZSTDLIBv06_API size_t     ZSTDv06_freeDCtx(ZSTDv06_DCtx* dctx);      /*!< @return : errorCode */

/** ZSTDv06_decompressDCtx() :
*   Same as ZSTDv06_decompress(), but requires an already allocated ZSTDv06_DCtx (see ZSTDv06_createDCtx()) */
ZSTDLIBv06_API size_t ZSTDv06_decompressDCtx(ZSTDv06_DCtx* ctx, void* dst, size_t dstCapacity, const void* src, size_t srcSize);


/*-***********************
*  Dictionary API
*************************/
/*! ZSTDv06_decompress_usingDict() :
*   Decompression using a pre-defined Dictionary content (see dictBuilder).
*   Dictionary must be identical to the one used during compression, otherwise regenerated data will be corrupted.
*   Note : dict can be NULL, in which case, it's equivalent to ZSTDv06_decompressDCtx() */
ZSTDLIBv06_API size_t ZSTDv06_decompress_usingDict(ZSTDv06_DCtx* dctx,
                                                   void* dst, size_t dstCapacity,
                                             const void* src, size_t srcSize,
                                             const void* dict,size_t dictSize);


/*-************************
*  Advanced Streaming API
***************************/
struct ZSTDv06_frameParams_s { unsigned long long frameContentSize; unsigned windowLog; };
typedef struct ZSTDv06_frameParams_s ZSTDv06_frameParams;

ZSTDLIBv06_API size_t ZSTDv06_getFrameParams(ZSTDv06_frameParams* fparamsPtr, const void* src, size_t srcSize);   /**< doesn't consume input */
ZSTDLIBv06_API size_t ZSTDv06_decompressBegin_usingDict(ZSTDv06_DCtx* dctx, const void* dict, size_t dictSize);
ZSTDLIBv06_API void   ZSTDv06_copyDCtx(ZSTDv06_DCtx* dctx, const ZSTDv06_DCtx* preparedDCtx);

ZSTDLIBv06_API size_t ZSTDv06_nextSrcSizeToDecompress(ZSTDv06_DCtx* dctx);
ZSTDLIBv06_API size_t ZSTDv06_decompressContinue(ZSTDv06_DCtx* dctx, void* dst, size_t dstCapacity, const void* src, size_t srcSize);



/* *************************************
*  ZBUFF API
***************************************/

typedef struct ZBUFFv06_DCtx_s ZBUFFv06_DCtx;
ZSTDLIBv06_API ZBUFFv06_DCtx* ZBUFFv06_createDCtx(void);
ZSTDLIBv06_API size_t         ZBUFFv06_freeDCtx(ZBUFFv06_DCtx* dctx);

ZSTDLIBv06_API size_t ZBUFFv06_decompressInit(ZBUFFv06_DCtx* dctx);
ZSTDLIBv06_API size_t ZBUFFv06_decompressInitDictionary(ZBUFFv06_DCtx* dctx, const void* dict, size_t dictSize);

ZSTDLIBv06_API size_t ZBUFFv06_decompressContinue(ZBUFFv06_DCtx* dctx,
                                                  void* dst, size_t* dstCapacityPtr,
                                            const void* src, size_t* srcSizePtr);

/*-***************************************************************************
*  Streaming decompression howto
*
*  A ZBUFFv06_DCtx object is required to track streaming operations.
*  Use ZBUFFv06_createDCtx() and ZBUFFv06_freeDCtx() to create/release resources.
*  Use ZBUFFv06_decompressInit() to start a new decompression operation,
*   or ZBUFFv06_decompressInitDictionary() if decompression requires a dictionary.
*  Note that ZBUFFv06_DCtx objects can be re-init multiple times.
*
*  Use ZBUFFv06_decompressContinue() repetitively to consume your input.
*  *srcSizePtr and *dstCapacityPtr can be any size.
*  The function will report how many bytes were read or written by modifying *srcSizePtr and *dstCapacityPtr.
*  Note that it may not consume the entire input, in which case it's up to the caller to present remaining input again.
*  The content of `dst` will be overwritten (up to *dstCapacityPtr) at each function call, so save its content if it matters, or change `dst`.
*  @return : a hint to preferred nb of bytes to use as input for next function call (it's only a hint, to help latency),
*            or 0 when a frame is completely decoded,
*            or an error code, which can be tested using ZBUFFv06_isError().
*
*  Hint : recommended buffer sizes (not compulsory) : ZBUFFv06_recommendedDInSize() and ZBUFFv06_recommendedDOutSize()
*  output : ZBUFFv06_recommendedDOutSize== 128 KB block size is the internal unit, it ensures it's always possible to write a full block when decoded.
*  input  : ZBUFFv06_recommendedDInSize == 128KB + 3;
*           just follow indications from ZBUFFv06_decompressContinue() to minimize latency. It should always be <= 128 KB + 3 .
* *******************************************************************************/


/* *************************************
*  Tool functions
***************************************/
ZSTDLIBv06_API unsigned ZBUFFv06_isError(size_t errorCode);
ZSTDLIBv06_API const char* ZBUFFv06_getErrorName(size_t errorCode);

/** Functions below provide recommended buffer sizes for Compression or Decompression operations.
*   These sizes are just hints, they tend to offer better latency */
ZSTDLIBv06_API size_t ZBUFFv06_recommendedDInSize(void);
ZSTDLIBv06_API size_t ZBUFFv06_recommendedDOutSize(void);


/*-*************************************
*  Constants
***************************************/
#define ZSTDv06_MAGICNUMBER 0xFD2FB526   /* v0.6 */



#if defined (__cplusplus)
}
#endif

#endif  /* ZSTDv06_BUFFERED_H */
