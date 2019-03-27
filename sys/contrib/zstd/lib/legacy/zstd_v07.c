/*
 * Copyright (c) 2016-present, Yann Collet, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under both the BSD-style license (found in the
 * LICENSE file in the root directory of this source tree) and the GPLv2 (found
 * in the COPYING file in the root directory of this source tree).
 * You may select, at your option, one of the above-listed licenses.
 */


/*- Dependencies -*/
#include <stddef.h>     /* size_t, ptrdiff_t */
#include <string.h>     /* memcpy */
#include <stdlib.h>     /* malloc, free, qsort */

#ifndef XXH_STATIC_LINKING_ONLY
#  define XXH_STATIC_LINKING_ONLY    /* XXH64_state_t */
#endif
#include "xxhash.h"                  /* XXH64_* */
#include "zstd_v07.h"

#define FSEv07_STATIC_LINKING_ONLY   /* FSEv07_MIN_TABLELOG */
#define HUFv07_STATIC_LINKING_ONLY   /* HUFv07_TABLELOG_ABSOLUTEMAX */
#define ZSTDv07_STATIC_LINKING_ONLY

#include "error_private.h"


#ifdef ZSTDv07_STATIC_LINKING_ONLY

/* ====================================================================================
 * The definitions in this section are considered experimental.
 * They should never be used with a dynamic library, as they may change in the future.
 * They are provided for advanced usages.
 * Use them only in association with static linking.
 * ==================================================================================== */

/*--- Constants ---*/
#define ZSTDv07_MAGIC_SKIPPABLE_START  0x184D2A50U

#define ZSTDv07_WINDOWLOG_MAX_32  25
#define ZSTDv07_WINDOWLOG_MAX_64  27
#define ZSTDv07_WINDOWLOG_MAX    ((U32)(MEM_32bits() ? ZSTDv07_WINDOWLOG_MAX_32 : ZSTDv07_WINDOWLOG_MAX_64))
#define ZSTDv07_WINDOWLOG_MIN     18
#define ZSTDv07_CHAINLOG_MAX     (ZSTDv07_WINDOWLOG_MAX+1)
#define ZSTDv07_CHAINLOG_MIN       4
#define ZSTDv07_HASHLOG_MAX       ZSTDv07_WINDOWLOG_MAX
#define ZSTDv07_HASHLOG_MIN       12
#define ZSTDv07_HASHLOG3_MAX      17
#define ZSTDv07_SEARCHLOG_MAX    (ZSTDv07_WINDOWLOG_MAX-1)
#define ZSTDv07_SEARCHLOG_MIN      1
#define ZSTDv07_SEARCHLENGTH_MAX   7
#define ZSTDv07_SEARCHLENGTH_MIN   3
#define ZSTDv07_TARGETLENGTH_MIN   4
#define ZSTDv07_TARGETLENGTH_MAX 999

#define ZSTDv07_FRAMEHEADERSIZE_MAX 18    /* for static allocation */
static const size_t ZSTDv07_frameHeaderSize_min = 5;
static const size_t ZSTDv07_frameHeaderSize_max = ZSTDv07_FRAMEHEADERSIZE_MAX;
static const size_t ZSTDv07_skippableHeaderSize = 8;  /* magic number + skippable frame length */


/* custom memory allocation functions */
typedef void* (*ZSTDv07_allocFunction) (void* opaque, size_t size);
typedef void  (*ZSTDv07_freeFunction) (void* opaque, void* address);
typedef struct { ZSTDv07_allocFunction customAlloc; ZSTDv07_freeFunction customFree; void* opaque; } ZSTDv07_customMem;


/*--- Advanced Decompression functions ---*/

/*! ZSTDv07_estimateDCtxSize() :
 *  Gives the potential amount of memory allocated to create a ZSTDv07_DCtx */
ZSTDLIBv07_API size_t ZSTDv07_estimateDCtxSize(void);

/*! ZSTDv07_createDCtx_advanced() :
 *  Create a ZSTD decompression context using external alloc and free functions */
ZSTDLIBv07_API ZSTDv07_DCtx* ZSTDv07_createDCtx_advanced(ZSTDv07_customMem customMem);

/*! ZSTDv07_sizeofDCtx() :
 *  Gives the amount of memory used by a given ZSTDv07_DCtx */
ZSTDLIBv07_API size_t ZSTDv07_sizeofDCtx(const ZSTDv07_DCtx* dctx);


/* ******************************************************************
*  Buffer-less streaming functions (synchronous mode)
********************************************************************/

ZSTDLIBv07_API size_t ZSTDv07_decompressBegin(ZSTDv07_DCtx* dctx);
ZSTDLIBv07_API size_t ZSTDv07_decompressBegin_usingDict(ZSTDv07_DCtx* dctx, const void* dict, size_t dictSize);
ZSTDLIBv07_API void   ZSTDv07_copyDCtx(ZSTDv07_DCtx* dctx, const ZSTDv07_DCtx* preparedDCtx);

ZSTDLIBv07_API size_t ZSTDv07_nextSrcSizeToDecompress(ZSTDv07_DCtx* dctx);
ZSTDLIBv07_API size_t ZSTDv07_decompressContinue(ZSTDv07_DCtx* dctx, void* dst, size_t dstCapacity, const void* src, size_t srcSize);

/*
  Buffer-less streaming decompression (synchronous mode)

  A ZSTDv07_DCtx object is required to track streaming operations.
  Use ZSTDv07_createDCtx() / ZSTDv07_freeDCtx() to manage it.
  A ZSTDv07_DCtx object can be re-used multiple times.

  First optional operation is to retrieve frame parameters, using ZSTDv07_getFrameParams(), which doesn't consume the input.
  It can provide the minimum size of rolling buffer required to properly decompress data (`windowSize`),
  and optionally the final size of uncompressed content.
  (Note : content size is an optional info that may not be present. 0 means : content size unknown)
  Frame parameters are extracted from the beginning of compressed frame.
  The amount of data to read is variable, from ZSTDv07_frameHeaderSize_min to ZSTDv07_frameHeaderSize_max (so if `srcSize` >= ZSTDv07_frameHeaderSize_max, it will always work)
  If `srcSize` is too small for operation to succeed, function will return the minimum size it requires to produce a result.
  Result : 0 when successful, it means the ZSTDv07_frameParams structure has been filled.
          >0 : means there is not enough data into `src`. Provides the expected size to successfully decode header.
           errorCode, which can be tested using ZSTDv07_isError()

  Start decompression, with ZSTDv07_decompressBegin() or ZSTDv07_decompressBegin_usingDict().
  Alternatively, you can copy a prepared context, using ZSTDv07_copyDCtx().

  Then use ZSTDv07_nextSrcSizeToDecompress() and ZSTDv07_decompressContinue() alternatively.
  ZSTDv07_nextSrcSizeToDecompress() tells how much bytes to provide as 'srcSize' to ZSTDv07_decompressContinue().
  ZSTDv07_decompressContinue() requires this exact amount of bytes, or it will fail.

  @result of ZSTDv07_decompressContinue() is the number of bytes regenerated within 'dst' (necessarily <= dstCapacity).
  It can be zero, which is not an error; it just means ZSTDv07_decompressContinue() has decoded some header.

  ZSTDv07_decompressContinue() needs previous data blocks during decompression, up to `windowSize`.
  They should preferably be located contiguously, prior to current block.
  Alternatively, a round buffer of sufficient size is also possible. Sufficient size is determined by frame parameters.
  ZSTDv07_decompressContinue() is very sensitive to contiguity,
  if 2 blocks don't follow each other, make sure that either the compressor breaks contiguity at the same place,
    or that previous contiguous segment is large enough to properly handle maximum back-reference.

  A frame is fully decoded when ZSTDv07_nextSrcSizeToDecompress() returns zero.
  Context can then be reset to start a new decompression.


  == Special case : skippable frames ==

  Skippable frames allow the integration of user-defined data into a flow of concatenated frames.
  Skippable frames will be ignored (skipped) by a decompressor. The format of skippable frame is following:
  a) Skippable frame ID - 4 Bytes, Little endian format, any value from 0x184D2A50 to 0x184D2A5F
  b) Frame Size - 4 Bytes, Little endian format, unsigned 32-bits
  c) Frame Content - any content (User Data) of length equal to Frame Size
  For skippable frames ZSTDv07_decompressContinue() always returns 0.
  For skippable frames ZSTDv07_getFrameParams() returns fparamsPtr->windowLog==0 what means that a frame is skippable.
  It also returns Frame Size as fparamsPtr->frameContentSize.
*/


/* **************************************
*  Block functions
****************************************/
/*! Block functions produce and decode raw zstd blocks, without frame metadata.
    Frame metadata cost is typically ~18 bytes, which can be non-negligible for very small blocks (< 100 bytes).
    User will have to take in charge required information to regenerate data, such as compressed and content sizes.

    A few rules to respect :
    - Compressing and decompressing require a context structure
      + Use ZSTDv07_createCCtx() and ZSTDv07_createDCtx()
    - It is necessary to init context before starting
      + compression : ZSTDv07_compressBegin()
      + decompression : ZSTDv07_decompressBegin()
      + variants _usingDict() are also allowed
      + copyCCtx() and copyDCtx() work too
    - Block size is limited, it must be <= ZSTDv07_getBlockSizeMax()
      + If you need to compress more, cut data into multiple blocks
      + Consider using the regular ZSTDv07_compress() instead, as frame metadata costs become negligible when source size is large.
    - When a block is considered not compressible enough, ZSTDv07_compressBlock() result will be zero.
      In which case, nothing is produced into `dst`.
      + User must test for such outcome and deal directly with uncompressed data
      + ZSTDv07_decompressBlock() doesn't accept uncompressed data as input !!!
      + In case of multiple successive blocks, decoder must be informed of uncompressed block existence to follow proper history.
        Use ZSTDv07_insertBlock() in such a case.
*/

#define ZSTDv07_BLOCKSIZE_ABSOLUTEMAX (128 * 1024)   /* define, for static allocation */
ZSTDLIBv07_API size_t ZSTDv07_decompressBlock(ZSTDv07_DCtx* dctx, void* dst, size_t dstCapacity, const void* src, size_t srcSize);
ZSTDLIBv07_API size_t ZSTDv07_insertBlock(ZSTDv07_DCtx* dctx, const void* blockStart, size_t blockSize);  /**< insert block into `dctx` history. Useful for uncompressed blocks */


#endif   /* ZSTDv07_STATIC_LINKING_ONLY */


/* ******************************************************************
   mem.h
   low-level memory access routines
   Copyright (C) 2013-2015, Yann Collet.

   BSD 2-Clause License (http://www.opensource.org/licenses/bsd-license.php)

   Redistribution and use in source and binary forms, with or without
   modification, are permitted provided that the following conditions are
   met:

       * Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.
       * Redistributions in binary form must reproduce the above
   copyright notice, this list of conditions and the following disclaimer
   in the documentation and/or other materials provided with the
   distribution.

   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
   OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
   THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

    You can contact the author at :
    - FSE source repository : https://github.com/Cyan4973/FiniteStateEntropy
    - Public forum : https://groups.google.com/forum/#!forum/lz4c
****************************************************************** */
#ifndef MEM_H_MODULE
#define MEM_H_MODULE

#if defined (__cplusplus)
extern "C" {
#endif

/*-****************************************
*  Compiler specifics
******************************************/
#if defined(_MSC_VER)   /* Visual Studio */
#   include <stdlib.h>  /* _byteswap_ulong */
#   include <intrin.h>  /* _byteswap_* */
#endif
#if defined(__GNUC__)
#  define MEM_STATIC static __attribute__((unused))
#elif defined (__cplusplus) || (defined (__STDC_VERSION__) && (__STDC_VERSION__ >= 199901L) /* C99 */)
#  define MEM_STATIC static inline
#elif defined(_MSC_VER)
#  define MEM_STATIC static __inline
#else
#  define MEM_STATIC static  /* this version may generate warnings for unused static functions; disable the relevant warning */
#endif


/*-**************************************************************
*  Basic Types
*****************************************************************/
#if  !defined (__VMS) && (defined (__cplusplus) || (defined (__STDC_VERSION__) && (__STDC_VERSION__ >= 199901L) /* C99 */) )
# include <stdint.h>
  typedef  uint8_t BYTE;
  typedef uint16_t U16;
  typedef  int16_t S16;
  typedef uint32_t U32;
  typedef  int32_t S32;
  typedef uint64_t U64;
  typedef  int64_t S64;
#else
  typedef unsigned char       BYTE;
  typedef unsigned short      U16;
  typedef   signed short      S16;
  typedef unsigned int        U32;
  typedef   signed int        S32;
  typedef unsigned long long  U64;
  typedef   signed long long  S64;
#endif


/*-**************************************************************
*  Memory I/O
*****************************************************************/
/* MEM_FORCE_MEMORY_ACCESS :
 * By default, access to unaligned memory is controlled by `memcpy()`, which is safe and portable.
 * Unfortunately, on some target/compiler combinations, the generated assembly is sub-optimal.
 * The below switch allow to select different access method for improved performance.
 * Method 0 (default) : use `memcpy()`. Safe and portable.
 * Method 1 : `__packed` statement. It depends on compiler extension (ie, not portable).
 *            This method is safe if your compiler supports it, and *generally* as fast or faster than `memcpy`.
 * Method 2 : direct access. This method is portable but violate C standard.
 *            It can generate buggy code on targets depending on alignment.
 *            In some circumstances, it's the only known way to get the most performance (ie GCC + ARMv6)
 * See http://fastcompression.blogspot.fr/2015/08/accessing-unaligned-memory.html for details.
 * Prefer these methods in priority order (0 > 1 > 2)
 */
#ifndef MEM_FORCE_MEMORY_ACCESS   /* can be defined externally, on command line for example */
#  if defined(__GNUC__) && ( defined(__ARM_ARCH_6__) || defined(__ARM_ARCH_6J__) || defined(__ARM_ARCH_6K__) || defined(__ARM_ARCH_6Z__) || defined(__ARM_ARCH_6ZK__) || defined(__ARM_ARCH_6T2__) )
#    define MEM_FORCE_MEMORY_ACCESS 2
#  elif (defined(__INTEL_COMPILER) && !defined(WIN32)) || \
  (defined(__GNUC__) && ( defined(__ARM_ARCH_7__) || defined(__ARM_ARCH_7A__) || defined(__ARM_ARCH_7R__) || defined(__ARM_ARCH_7M__) || defined(__ARM_ARCH_7S__) ))
#    define MEM_FORCE_MEMORY_ACCESS 1
#  endif
#endif

MEM_STATIC unsigned MEM_32bits(void) { return sizeof(size_t)==4; }
MEM_STATIC unsigned MEM_64bits(void) { return sizeof(size_t)==8; }

MEM_STATIC unsigned MEM_isLittleEndian(void)
{
    const union { U32 u; BYTE c[4]; } one = { 1 };   /* don't use static : performance detrimental  */
    return one.c[0];
}

#if defined(MEM_FORCE_MEMORY_ACCESS) && (MEM_FORCE_MEMORY_ACCESS==2)

/* violates C standard, by lying on structure alignment.
Only use if no other choice to achieve best performance on target platform */
MEM_STATIC U16 MEM_read16(const void* memPtr) { return *(const U16*) memPtr; }
MEM_STATIC U32 MEM_read32(const void* memPtr) { return *(const U32*) memPtr; }
MEM_STATIC U64 MEM_read64(const void* memPtr) { return *(const U64*) memPtr; }

MEM_STATIC void MEM_write16(void* memPtr, U16 value) { *(U16*)memPtr = value; }

#elif defined(MEM_FORCE_MEMORY_ACCESS) && (MEM_FORCE_MEMORY_ACCESS==1)

/* __pack instructions are safer, but compiler specific, hence potentially problematic for some compilers */
/* currently only defined for gcc and icc */
typedef union { U16 u16; U32 u32; U64 u64; size_t st; } __attribute__((packed)) unalign;

MEM_STATIC U16 MEM_read16(const void* ptr) { return ((const unalign*)ptr)->u16; }
MEM_STATIC U32 MEM_read32(const void* ptr) { return ((const unalign*)ptr)->u32; }
MEM_STATIC U64 MEM_read64(const void* ptr) { return ((const unalign*)ptr)->u64; }

MEM_STATIC void MEM_write16(void* memPtr, U16 value) { ((unalign*)memPtr)->u16 = value; }

#else

/* default method, safe and standard.
   can sometimes prove slower */

MEM_STATIC U16 MEM_read16(const void* memPtr)
{
    U16 val; memcpy(&val, memPtr, sizeof(val)); return val;
}

MEM_STATIC U32 MEM_read32(const void* memPtr)
{
    U32 val; memcpy(&val, memPtr, sizeof(val)); return val;
}

MEM_STATIC U64 MEM_read64(const void* memPtr)
{
    U64 val; memcpy(&val, memPtr, sizeof(val)); return val;
}

MEM_STATIC void MEM_write16(void* memPtr, U16 value)
{
    memcpy(memPtr, &value, sizeof(value));
}

#endif /* MEM_FORCE_MEMORY_ACCESS */

MEM_STATIC U32 MEM_swap32(U32 in)
{
#if defined(_MSC_VER)     /* Visual Studio */
    return _byteswap_ulong(in);
#elif defined (__GNUC__) && (__GNUC__ * 100 + __GNUC_MINOR__ >= 403)
    return __builtin_bswap32(in);
#else
    return  ((in << 24) & 0xff000000 ) |
            ((in <<  8) & 0x00ff0000 ) |
            ((in >>  8) & 0x0000ff00 ) |
            ((in >> 24) & 0x000000ff );
#endif
}

MEM_STATIC U64 MEM_swap64(U64 in)
{
#if defined(_MSC_VER)     /* Visual Studio */
    return _byteswap_uint64(in);
#elif defined (__GNUC__) && (__GNUC__ * 100 + __GNUC_MINOR__ >= 403)
    return __builtin_bswap64(in);
#else
    return  ((in << 56) & 0xff00000000000000ULL) |
            ((in << 40) & 0x00ff000000000000ULL) |
            ((in << 24) & 0x0000ff0000000000ULL) |
            ((in << 8)  & 0x000000ff00000000ULL) |
            ((in >> 8)  & 0x00000000ff000000ULL) |
            ((in >> 24) & 0x0000000000ff0000ULL) |
            ((in >> 40) & 0x000000000000ff00ULL) |
            ((in >> 56) & 0x00000000000000ffULL);
#endif
}


/*=== Little endian r/w ===*/

MEM_STATIC U16 MEM_readLE16(const void* memPtr)
{
    if (MEM_isLittleEndian())
        return MEM_read16(memPtr);
    else {
        const BYTE* p = (const BYTE*)memPtr;
        return (U16)(p[0] + (p[1]<<8));
    }
}

MEM_STATIC void MEM_writeLE16(void* memPtr, U16 val)
{
    if (MEM_isLittleEndian()) {
        MEM_write16(memPtr, val);
    } else {
        BYTE* p = (BYTE*)memPtr;
        p[0] = (BYTE)val;
        p[1] = (BYTE)(val>>8);
    }
}

MEM_STATIC U32 MEM_readLE32(const void* memPtr)
{
    if (MEM_isLittleEndian())
        return MEM_read32(memPtr);
    else
        return MEM_swap32(MEM_read32(memPtr));
}


MEM_STATIC U64 MEM_readLE64(const void* memPtr)
{
    if (MEM_isLittleEndian())
        return MEM_read64(memPtr);
    else
        return MEM_swap64(MEM_read64(memPtr));
}

MEM_STATIC size_t MEM_readLEST(const void* memPtr)
{
    if (MEM_32bits())
        return (size_t)MEM_readLE32(memPtr);
    else
        return (size_t)MEM_readLE64(memPtr);
}



#if defined (__cplusplus)
}
#endif

#endif /* MEM_H_MODULE */
/* ******************************************************************
   bitstream
   Part of FSE library
   header file (to include)
   Copyright (C) 2013-2016, Yann Collet.

   BSD 2-Clause License (http://www.opensource.org/licenses/bsd-license.php)

   Redistribution and use in source and binary forms, with or without
   modification, are permitted provided that the following conditions are
   met:

       * Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.
       * Redistributions in binary form must reproduce the above
   copyright notice, this list of conditions and the following disclaimer
   in the documentation and/or other materials provided with the
   distribution.

   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
   OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
   THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

   You can contact the author at :
   - Source repository : https://github.com/Cyan4973/FiniteStateEntropy
****************************************************************** */
#ifndef BITSTREAM_H_MODULE
#define BITSTREAM_H_MODULE

#if defined (__cplusplus)
extern "C" {
#endif


/*
*  This API consists of small unitary functions, which must be inlined for best performance.
*  Since link-time-optimization is not available for all compilers,
*  these functions are defined into a .h to be included.
*/


/*=========================================
*  Target specific
=========================================*/
#if defined(__BMI__) && defined(__GNUC__)
#  include <immintrin.h>   /* support for bextr (experimental) */
#endif

/*-********************************************
*  bitStream decoding API (read backward)
**********************************************/
typedef struct
{
    size_t   bitContainer;
    unsigned bitsConsumed;
    const char* ptr;
    const char* start;
} BITv07_DStream_t;

typedef enum { BITv07_DStream_unfinished = 0,
               BITv07_DStream_endOfBuffer = 1,
               BITv07_DStream_completed = 2,
               BITv07_DStream_overflow = 3 } BITv07_DStream_status;  /* result of BITv07_reloadDStream() */
               /* 1,2,4,8 would be better for bitmap combinations, but slows down performance a bit ... :( */

MEM_STATIC size_t   BITv07_initDStream(BITv07_DStream_t* bitD, const void* srcBuffer, size_t srcSize);
MEM_STATIC size_t   BITv07_readBits(BITv07_DStream_t* bitD, unsigned nbBits);
MEM_STATIC BITv07_DStream_status BITv07_reloadDStream(BITv07_DStream_t* bitD);
MEM_STATIC unsigned BITv07_endOfDStream(const BITv07_DStream_t* bitD);



/*-****************************************
*  unsafe API
******************************************/
MEM_STATIC size_t BITv07_readBitsFast(BITv07_DStream_t* bitD, unsigned nbBits);
/* faster, but works only if nbBits >= 1 */



/*-**************************************************************
*  Internal functions
****************************************************************/
MEM_STATIC unsigned BITv07_highbit32 (U32 val)
{
#   if defined(_MSC_VER)   /* Visual */
    unsigned long r=0;
    _BitScanReverse ( &r, val );
    return (unsigned) r;
#   elif defined(__GNUC__) && (__GNUC__ >= 3)   /* Use GCC Intrinsic */
    return 31 - __builtin_clz (val);
#   else   /* Software version */
    static const unsigned DeBruijnClz[32] = { 0, 9, 1, 10, 13, 21, 2, 29, 11, 14, 16, 18, 22, 25, 3, 30, 8, 12, 20, 28, 15, 17, 24, 7, 19, 27, 23, 6, 26, 5, 4, 31 };
    U32 v = val;
    v |= v >> 1;
    v |= v >> 2;
    v |= v >> 4;
    v |= v >> 8;
    v |= v >> 16;
    return DeBruijnClz[ (U32) (v * 0x07C4ACDDU) >> 27];
#   endif
}



/*-********************************************************
* bitStream decoding
**********************************************************/
/*! BITv07_initDStream() :
*   Initialize a BITv07_DStream_t.
*   `bitD` : a pointer to an already allocated BITv07_DStream_t structure.
*   `srcSize` must be the *exact* size of the bitStream, in bytes.
*   @return : size of stream (== srcSize) or an errorCode if a problem is detected
*/
MEM_STATIC size_t BITv07_initDStream(BITv07_DStream_t* bitD, const void* srcBuffer, size_t srcSize)
{
    if (srcSize < 1) { memset(bitD, 0, sizeof(*bitD)); return ERROR(srcSize_wrong); }

    if (srcSize >=  sizeof(bitD->bitContainer)) {  /* normal case */
        bitD->start = (const char*)srcBuffer;
        bitD->ptr   = (const char*)srcBuffer + srcSize - sizeof(bitD->bitContainer);
        bitD->bitContainer = MEM_readLEST(bitD->ptr);
        { BYTE const lastByte = ((const BYTE*)srcBuffer)[srcSize-1];
          bitD->bitsConsumed = lastByte ? 8 - BITv07_highbit32(lastByte) : 0;
          if (lastByte == 0) return ERROR(GENERIC); /* endMark not present */ }
    } else {
        bitD->start = (const char*)srcBuffer;
        bitD->ptr   = bitD->start;
        bitD->bitContainer = *(const BYTE*)(bitD->start);
        switch(srcSize)
        {
            case 7: bitD->bitContainer += (size_t)(((const BYTE*)(srcBuffer))[6]) << (sizeof(bitD->bitContainer)*8 - 16);/* fall-through */
            case 6: bitD->bitContainer += (size_t)(((const BYTE*)(srcBuffer))[5]) << (sizeof(bitD->bitContainer)*8 - 24);/* fall-through */
            case 5: bitD->bitContainer += (size_t)(((const BYTE*)(srcBuffer))[4]) << (sizeof(bitD->bitContainer)*8 - 32);/* fall-through */
            case 4: bitD->bitContainer += (size_t)(((const BYTE*)(srcBuffer))[3]) << 24; /* fall-through */
            case 3: bitD->bitContainer += (size_t)(((const BYTE*)(srcBuffer))[2]) << 16; /* fall-through */
            case 2: bitD->bitContainer += (size_t)(((const BYTE*)(srcBuffer))[1]) <<  8; /* fall-through */
            default: break;
        }
        { BYTE const lastByte = ((const BYTE*)srcBuffer)[srcSize-1];
          bitD->bitsConsumed = lastByte ? 8 - BITv07_highbit32(lastByte) : 0;
          if (lastByte == 0) return ERROR(GENERIC); /* endMark not present */ }
        bitD->bitsConsumed += (U32)(sizeof(bitD->bitContainer) - srcSize)*8;
    }

    return srcSize;
}


 MEM_STATIC size_t BITv07_lookBits(const BITv07_DStream_t* bitD, U32 nbBits)
{
    U32 const bitMask = sizeof(bitD->bitContainer)*8 - 1;
    return ((bitD->bitContainer << (bitD->bitsConsumed & bitMask)) >> 1) >> ((bitMask-nbBits) & bitMask);
}

/*! BITv07_lookBitsFast() :
*   unsafe version; only works only if nbBits >= 1 */
MEM_STATIC size_t BITv07_lookBitsFast(const BITv07_DStream_t* bitD, U32 nbBits)
{
    U32 const bitMask = sizeof(bitD->bitContainer)*8 - 1;
    return (bitD->bitContainer << (bitD->bitsConsumed & bitMask)) >> (((bitMask+1)-nbBits) & bitMask);
}

MEM_STATIC void BITv07_skipBits(BITv07_DStream_t* bitD, U32 nbBits)
{
    bitD->bitsConsumed += nbBits;
}

MEM_STATIC size_t BITv07_readBits(BITv07_DStream_t* bitD, U32 nbBits)
{
    size_t const value = BITv07_lookBits(bitD, nbBits);
    BITv07_skipBits(bitD, nbBits);
    return value;
}

/*! BITv07_readBitsFast() :
*   unsafe version; only works only if nbBits >= 1 */
MEM_STATIC size_t BITv07_readBitsFast(BITv07_DStream_t* bitD, U32 nbBits)
{
    size_t const value = BITv07_lookBitsFast(bitD, nbBits);
    BITv07_skipBits(bitD, nbBits);
    return value;
}

MEM_STATIC BITv07_DStream_status BITv07_reloadDStream(BITv07_DStream_t* bitD)
{
    if (bitD->bitsConsumed > (sizeof(bitD->bitContainer)*8))  /* should not happen => corruption detected */
        return BITv07_DStream_overflow;

    if (bitD->ptr >= bitD->start + sizeof(bitD->bitContainer)) {
        bitD->ptr -= bitD->bitsConsumed >> 3;
        bitD->bitsConsumed &= 7;
        bitD->bitContainer = MEM_readLEST(bitD->ptr);
        return BITv07_DStream_unfinished;
    }
    if (bitD->ptr == bitD->start) {
        if (bitD->bitsConsumed < sizeof(bitD->bitContainer)*8) return BITv07_DStream_endOfBuffer;
        return BITv07_DStream_completed;
    }
    {   U32 nbBytes = bitD->bitsConsumed >> 3;
        BITv07_DStream_status result = BITv07_DStream_unfinished;
        if (bitD->ptr - nbBytes < bitD->start) {
            nbBytes = (U32)(bitD->ptr - bitD->start);  /* ptr > start */
            result = BITv07_DStream_endOfBuffer;
        }
        bitD->ptr -= nbBytes;
        bitD->bitsConsumed -= nbBytes*8;
        bitD->bitContainer = MEM_readLEST(bitD->ptr);   /* reminder : srcSize > sizeof(bitD) */
        return result;
    }
}

/*! BITv07_endOfDStream() :
*   @return Tells if DStream has exactly reached its end (all bits consumed).
*/
MEM_STATIC unsigned BITv07_endOfDStream(const BITv07_DStream_t* DStream)
{
    return ((DStream->ptr == DStream->start) && (DStream->bitsConsumed == sizeof(DStream->bitContainer)*8));
}

#if defined (__cplusplus)
}
#endif

#endif /* BITSTREAM_H_MODULE */
/* ******************************************************************
   FSE : Finite State Entropy codec
   Public Prototypes declaration
   Copyright (C) 2013-2016, Yann Collet.

   BSD 2-Clause License (http://www.opensource.org/licenses/bsd-license.php)

   Redistribution and use in source and binary forms, with or without
   modification, are permitted provided that the following conditions are
   met:

       * Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.
       * Redistributions in binary form must reproduce the above
   copyright notice, this list of conditions and the following disclaimer
   in the documentation and/or other materials provided with the
   distribution.

   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
   OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
   THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

   You can contact the author at :
   - Source repository : https://github.com/Cyan4973/FiniteStateEntropy
****************************************************************** */
#ifndef FSEv07_H
#define FSEv07_H

#if defined (__cplusplus)
extern "C" {
#endif



/*-****************************************
*  FSE simple functions
******************************************/

/*! FSEv07_decompress():
    Decompress FSE data from buffer 'cSrc', of size 'cSrcSize',
    into already allocated destination buffer 'dst', of size 'dstCapacity'.
    @return : size of regenerated data (<= maxDstSize),
              or an error code, which can be tested using FSEv07_isError() .

    ** Important ** : FSEv07_decompress() does not decompress non-compressible nor RLE data !!!
    Why ? : making this distinction requires a header.
    Header management is intentionally delegated to the user layer, which can better manage special cases.
*/
size_t FSEv07_decompress(void* dst,  size_t dstCapacity,
                const void* cSrc, size_t cSrcSize);


/* Error Management */
unsigned    FSEv07_isError(size_t code);        /* tells if a return value is an error code */
const char* FSEv07_getErrorName(size_t code);   /* provides error code string (useful for debugging) */


/*-*****************************************
*  FSE detailed API
******************************************/
/*!
FSEv07_decompress() does the following:
1. read normalized counters with readNCount()
2. build decoding table 'DTable' from normalized counters
3. decode the data stream using decoding table 'DTable'

The following API allows targeting specific sub-functions for advanced tasks.
For example, it's possible to compress several blocks using the same 'CTable',
or to save and provide normalized distribution using external method.
*/


/* *** DECOMPRESSION *** */

/*! FSEv07_readNCount():
    Read compactly saved 'normalizedCounter' from 'rBuffer'.
    @return : size read from 'rBuffer',
              or an errorCode, which can be tested using FSEv07_isError().
              maxSymbolValuePtr[0] and tableLogPtr[0] will also be updated with their respective values */
size_t FSEv07_readNCount (short* normalizedCounter, unsigned* maxSymbolValuePtr, unsigned* tableLogPtr, const void* rBuffer, size_t rBuffSize);

/*! Constructor and Destructor of FSEv07_DTable.
    Note that its size depends on 'tableLog' */
typedef unsigned FSEv07_DTable;   /* don't allocate that. It's just a way to be more restrictive than void* */
FSEv07_DTable* FSEv07_createDTable(unsigned tableLog);
void        FSEv07_freeDTable(FSEv07_DTable* dt);

/*! FSEv07_buildDTable():
    Builds 'dt', which must be already allocated, using FSEv07_createDTable().
    return : 0, or an errorCode, which can be tested using FSEv07_isError() */
size_t FSEv07_buildDTable (FSEv07_DTable* dt, const short* normalizedCounter, unsigned maxSymbolValue, unsigned tableLog);

/*! FSEv07_decompress_usingDTable():
    Decompress compressed source `cSrc` of size `cSrcSize` using `dt`
    into `dst` which must be already allocated.
    @return : size of regenerated data (necessarily <= `dstCapacity`),
              or an errorCode, which can be tested using FSEv07_isError() */
size_t FSEv07_decompress_usingDTable(void* dst, size_t dstCapacity, const void* cSrc, size_t cSrcSize, const FSEv07_DTable* dt);

/*!
Tutorial :
----------
(Note : these functions only decompress FSE-compressed blocks.
 If block is uncompressed, use memcpy() instead
 If block is a single repeated byte, use memset() instead )

The first step is to obtain the normalized frequencies of symbols.
This can be performed by FSEv07_readNCount() if it was saved using FSEv07_writeNCount().
'normalizedCounter' must be already allocated, and have at least 'maxSymbolValuePtr[0]+1' cells of signed short.
In practice, that means it's necessary to know 'maxSymbolValue' beforehand,
or size the table to handle worst case situations (typically 256).
FSEv07_readNCount() will provide 'tableLog' and 'maxSymbolValue'.
The result of FSEv07_readNCount() is the number of bytes read from 'rBuffer'.
Note that 'rBufferSize' must be at least 4 bytes, even if useful information is less than that.
If there is an error, the function will return an error code, which can be tested using FSEv07_isError().

The next step is to build the decompression tables 'FSEv07_DTable' from 'normalizedCounter'.
This is performed by the function FSEv07_buildDTable().
The space required by 'FSEv07_DTable' must be already allocated using FSEv07_createDTable().
If there is an error, the function will return an error code, which can be tested using FSEv07_isError().

`FSEv07_DTable` can then be used to decompress `cSrc`, with FSEv07_decompress_usingDTable().
`cSrcSize` must be strictly correct, otherwise decompression will fail.
FSEv07_decompress_usingDTable() result will tell how many bytes were regenerated (<=`dstCapacity`).
If there is an error, the function will return an error code, which can be tested using FSEv07_isError(). (ex: dst buffer too small)
*/


#ifdef FSEv07_STATIC_LINKING_ONLY


/* *****************************************
*  Static allocation
*******************************************/
/* FSE buffer bounds */
#define FSEv07_NCOUNTBOUND 512
#define FSEv07_BLOCKBOUND(size) (size + (size>>7))

/* It is possible to statically allocate FSE CTable/DTable as a table of unsigned using below macros */
#define FSEv07_DTABLE_SIZE_U32(maxTableLog)                   (1 + (1<<maxTableLog))


/* *****************************************
*  FSE advanced API
*******************************************/
size_t FSEv07_countFast(unsigned* count, unsigned* maxSymbolValuePtr, const void* src, size_t srcSize);
/**< same as FSEv07_count(), but blindly trusts that all byte values within src are <= *maxSymbolValuePtr  */

unsigned FSEv07_optimalTableLog_internal(unsigned maxTableLog, size_t srcSize, unsigned maxSymbolValue, unsigned minus);
/**< same as FSEv07_optimalTableLog(), which used `minus==2` */

size_t FSEv07_buildDTable_raw (FSEv07_DTable* dt, unsigned nbBits);
/**< build a fake FSEv07_DTable, designed to read an uncompressed bitstream where each symbol uses nbBits */

size_t FSEv07_buildDTable_rle (FSEv07_DTable* dt, unsigned char symbolValue);
/**< build a fake FSEv07_DTable, designed to always generate the same symbolValue */



/* *****************************************
*  FSE symbol decompression API
*******************************************/
typedef struct
{
    size_t      state;
    const void* table;   /* precise table may vary, depending on U16 */
} FSEv07_DState_t;


static void     FSEv07_initDState(FSEv07_DState_t* DStatePtr, BITv07_DStream_t* bitD, const FSEv07_DTable* dt);

static unsigned char FSEv07_decodeSymbol(FSEv07_DState_t* DStatePtr, BITv07_DStream_t* bitD);



/* *****************************************
*  FSE unsafe API
*******************************************/
static unsigned char FSEv07_decodeSymbolFast(FSEv07_DState_t* DStatePtr, BITv07_DStream_t* bitD);
/* faster, but works only if nbBits is always >= 1 (otherwise, result will be corrupted) */


/* ======    Decompression    ====== */

typedef struct {
    U16 tableLog;
    U16 fastMode;
} FSEv07_DTableHeader;   /* sizeof U32 */

typedef struct
{
    unsigned short newState;
    unsigned char  symbol;
    unsigned char  nbBits;
} FSEv07_decode_t;   /* size == U32 */

MEM_STATIC void FSEv07_initDState(FSEv07_DState_t* DStatePtr, BITv07_DStream_t* bitD, const FSEv07_DTable* dt)
{
    const void* ptr = dt;
    const FSEv07_DTableHeader* const DTableH = (const FSEv07_DTableHeader*)ptr;
    DStatePtr->state = BITv07_readBits(bitD, DTableH->tableLog);
    BITv07_reloadDStream(bitD);
    DStatePtr->table = dt + 1;
}

MEM_STATIC BYTE FSEv07_peekSymbol(const FSEv07_DState_t* DStatePtr)
{
    FSEv07_decode_t const DInfo = ((const FSEv07_decode_t*)(DStatePtr->table))[DStatePtr->state];
    return DInfo.symbol;
}

MEM_STATIC void FSEv07_updateState(FSEv07_DState_t* DStatePtr, BITv07_DStream_t* bitD)
{
    FSEv07_decode_t const DInfo = ((const FSEv07_decode_t*)(DStatePtr->table))[DStatePtr->state];
    U32 const nbBits = DInfo.nbBits;
    size_t const lowBits = BITv07_readBits(bitD, nbBits);
    DStatePtr->state = DInfo.newState + lowBits;
}

MEM_STATIC BYTE FSEv07_decodeSymbol(FSEv07_DState_t* DStatePtr, BITv07_DStream_t* bitD)
{
    FSEv07_decode_t const DInfo = ((const FSEv07_decode_t*)(DStatePtr->table))[DStatePtr->state];
    U32 const nbBits = DInfo.nbBits;
    BYTE const symbol = DInfo.symbol;
    size_t const lowBits = BITv07_readBits(bitD, nbBits);

    DStatePtr->state = DInfo.newState + lowBits;
    return symbol;
}

/*! FSEv07_decodeSymbolFast() :
    unsafe, only works if no symbol has a probability > 50% */
MEM_STATIC BYTE FSEv07_decodeSymbolFast(FSEv07_DState_t* DStatePtr, BITv07_DStream_t* bitD)
{
    FSEv07_decode_t const DInfo = ((const FSEv07_decode_t*)(DStatePtr->table))[DStatePtr->state];
    U32 const nbBits = DInfo.nbBits;
    BYTE const symbol = DInfo.symbol;
    size_t const lowBits = BITv07_readBitsFast(bitD, nbBits);

    DStatePtr->state = DInfo.newState + lowBits;
    return symbol;
}



#ifndef FSEv07_COMMONDEFS_ONLY

/* **************************************************************
*  Tuning parameters
****************************************************************/
/*!MEMORY_USAGE :
*  Memory usage formula : N->2^N Bytes (examples : 10 -> 1KB; 12 -> 4KB ; 16 -> 64KB; 20 -> 1MB; etc.)
*  Increasing memory usage improves compression ratio
*  Reduced memory usage can improve speed, due to cache effect
*  Recommended max value is 14, for 16KB, which nicely fits into Intel x86 L1 cache */
#define FSEv07_MAX_MEMORY_USAGE 14
#define FSEv07_DEFAULT_MEMORY_USAGE 13

/*!FSEv07_MAX_SYMBOL_VALUE :
*  Maximum symbol value authorized.
*  Required for proper stack allocation */
#define FSEv07_MAX_SYMBOL_VALUE 255


/* **************************************************************
*  template functions type & suffix
****************************************************************/
#define FSEv07_FUNCTION_TYPE BYTE
#define FSEv07_FUNCTION_EXTENSION
#define FSEv07_DECODE_TYPE FSEv07_decode_t


#endif   /* !FSEv07_COMMONDEFS_ONLY */


/* ***************************************************************
*  Constants
*****************************************************************/
#define FSEv07_MAX_TABLELOG  (FSEv07_MAX_MEMORY_USAGE-2)
#define FSEv07_MAX_TABLESIZE (1U<<FSEv07_MAX_TABLELOG)
#define FSEv07_MAXTABLESIZE_MASK (FSEv07_MAX_TABLESIZE-1)
#define FSEv07_DEFAULT_TABLELOG (FSEv07_DEFAULT_MEMORY_USAGE-2)
#define FSEv07_MIN_TABLELOG 5

#define FSEv07_TABLELOG_ABSOLUTE_MAX 15
#if FSEv07_MAX_TABLELOG > FSEv07_TABLELOG_ABSOLUTE_MAX
#  error "FSEv07_MAX_TABLELOG > FSEv07_TABLELOG_ABSOLUTE_MAX is not supported"
#endif

#define FSEv07_TABLESTEP(tableSize) ((tableSize>>1) + (tableSize>>3) + 3)


#endif /* FSEv07_STATIC_LINKING_ONLY */


#if defined (__cplusplus)
}
#endif

#endif  /* FSEv07_H */
/* ******************************************************************
   Huffman coder, part of New Generation Entropy library
   header file
   Copyright (C) 2013-2016, Yann Collet.

   BSD 2-Clause License (http://www.opensource.org/licenses/bsd-license.php)

   Redistribution and use in source and binary forms, with or without
   modification, are permitted provided that the following conditions are
   met:

       * Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.
       * Redistributions in binary form must reproduce the above
   copyright notice, this list of conditions and the following disclaimer
   in the documentation and/or other materials provided with the
   distribution.

   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
   OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
   THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

   You can contact the author at :
   - Source repository : https://github.com/Cyan4973/FiniteStateEntropy
****************************************************************** */
#ifndef HUFv07_H_298734234
#define HUFv07_H_298734234

#if defined (__cplusplus)
extern "C" {
#endif



/* *** simple functions *** */
/**
HUFv07_decompress() :
    Decompress HUF data from buffer 'cSrc', of size 'cSrcSize',
    into already allocated buffer 'dst', of minimum size 'dstSize'.
    `dstSize` : **must** be the ***exact*** size of original (uncompressed) data.
    Note : in contrast with FSE, HUFv07_decompress can regenerate
           RLE (cSrcSize==1) and uncompressed (cSrcSize==dstSize) data,
           because it knows size to regenerate.
    @return : size of regenerated data (== dstSize),
              or an error code, which can be tested using HUFv07_isError()
*/
size_t HUFv07_decompress(void* dst,  size_t dstSize,
                const void* cSrc, size_t cSrcSize);


/* ****************************************
*  Tool functions
******************************************/
#define HUFv07_BLOCKSIZE_MAX (128 * 1024)

/* Error Management */
unsigned    HUFv07_isError(size_t code);        /**< tells if a return value is an error code */
const char* HUFv07_getErrorName(size_t code);   /**< provides error code string (useful for debugging) */


/* *** Advanced function *** */


#ifdef HUFv07_STATIC_LINKING_ONLY


/* *** Constants *** */
#define HUFv07_TABLELOG_ABSOLUTEMAX  16   /* absolute limit of HUFv07_MAX_TABLELOG. Beyond that value, code does not work */
#define HUFv07_TABLELOG_MAX  12           /* max configured tableLog (for static allocation); can be modified up to HUFv07_ABSOLUTEMAX_TABLELOG */
#define HUFv07_TABLELOG_DEFAULT  11       /* tableLog by default, when not specified */
#define HUFv07_SYMBOLVALUE_MAX 255
#if (HUFv07_TABLELOG_MAX > HUFv07_TABLELOG_ABSOLUTEMAX)
#  error "HUFv07_TABLELOG_MAX is too large !"
#endif


/* ****************************************
*  Static allocation
******************************************/
/* HUF buffer bounds */
#define HUFv07_BLOCKBOUND(size) (size + (size>>8) + 8)   /* only true if incompressible pre-filtered with fast heuristic */

/* static allocation of HUF's DTable */
typedef U32 HUFv07_DTable;
#define HUFv07_DTABLE_SIZE(maxTableLog)   (1 + (1<<(maxTableLog)))
#define HUFv07_CREATE_STATIC_DTABLEX2(DTable, maxTableLog) \
        HUFv07_DTable DTable[HUFv07_DTABLE_SIZE((maxTableLog)-1)] = { ((U32)((maxTableLog)-1)*0x1000001) }
#define HUFv07_CREATE_STATIC_DTABLEX4(DTable, maxTableLog) \
        HUFv07_DTable DTable[HUFv07_DTABLE_SIZE(maxTableLog)] = { ((U32)(maxTableLog)*0x1000001) }


/* ****************************************
*  Advanced decompression functions
******************************************/
size_t HUFv07_decompress4X2 (void* dst, size_t dstSize, const void* cSrc, size_t cSrcSize);   /**< single-symbol decoder */
size_t HUFv07_decompress4X4 (void* dst, size_t dstSize, const void* cSrc, size_t cSrcSize);   /**< double-symbols decoder */

size_t HUFv07_decompress4X_DCtx (HUFv07_DTable* dctx, void* dst, size_t dstSize, const void* cSrc, size_t cSrcSize);   /**< decodes RLE and uncompressed */
size_t HUFv07_decompress4X_hufOnly(HUFv07_DTable* dctx, void* dst, size_t dstSize, const void* cSrc, size_t cSrcSize); /**< considers RLE and uncompressed as errors */
size_t HUFv07_decompress4X2_DCtx(HUFv07_DTable* dctx, void* dst, size_t dstSize, const void* cSrc, size_t cSrcSize);   /**< single-symbol decoder */
size_t HUFv07_decompress4X4_DCtx(HUFv07_DTable* dctx, void* dst, size_t dstSize, const void* cSrc, size_t cSrcSize);   /**< double-symbols decoder */

size_t HUFv07_decompress1X_DCtx (HUFv07_DTable* dctx, void* dst, size_t dstSize, const void* cSrc, size_t cSrcSize);
size_t HUFv07_decompress1X2_DCtx(HUFv07_DTable* dctx, void* dst, size_t dstSize, const void* cSrc, size_t cSrcSize);   /**< single-symbol decoder */
size_t HUFv07_decompress1X4_DCtx(HUFv07_DTable* dctx, void* dst, size_t dstSize, const void* cSrc, size_t cSrcSize);   /**< double-symbols decoder */


/* ****************************************
*  HUF detailed API
******************************************/
/*!
The following API allows targeting specific sub-functions for advanced tasks.
For example, it's possible to compress several blocks using the same 'CTable',
or to save and regenerate 'CTable' using external methods.
*/
/* FSEv07_count() : find it within "fse.h" */

/*! HUFv07_readStats() :
    Read compact Huffman tree, saved by HUFv07_writeCTable().
    `huffWeight` is destination buffer.
    @return : size read from `src` , or an error Code .
    Note : Needed by HUFv07_readCTable() and HUFv07_readDTableXn() . */
size_t HUFv07_readStats(BYTE* huffWeight, size_t hwSize, U32* rankStats,
                     U32* nbSymbolsPtr, U32* tableLogPtr,
                     const void* src, size_t srcSize);


/*
HUFv07_decompress() does the following:
1. select the decompression algorithm (X2, X4) based on pre-computed heuristics
2. build Huffman table from save, using HUFv07_readDTableXn()
3. decode 1 or 4 segments in parallel using HUFv07_decompressSXn_usingDTable
*/

/** HUFv07_selectDecoder() :
*   Tells which decoder is likely to decode faster,
*   based on a set of pre-determined metrics.
*   @return : 0==HUFv07_decompress4X2, 1==HUFv07_decompress4X4 .
*   Assumption : 0 < cSrcSize < dstSize <= 128 KB */
U32 HUFv07_selectDecoder (size_t dstSize, size_t cSrcSize);

size_t HUFv07_readDTableX2 (HUFv07_DTable* DTable, const void* src, size_t srcSize);
size_t HUFv07_readDTableX4 (HUFv07_DTable* DTable, const void* src, size_t srcSize);

size_t HUFv07_decompress4X_usingDTable(void* dst, size_t maxDstSize, const void* cSrc, size_t cSrcSize, const HUFv07_DTable* DTable);
size_t HUFv07_decompress4X2_usingDTable(void* dst, size_t maxDstSize, const void* cSrc, size_t cSrcSize, const HUFv07_DTable* DTable);
size_t HUFv07_decompress4X4_usingDTable(void* dst, size_t maxDstSize, const void* cSrc, size_t cSrcSize, const HUFv07_DTable* DTable);


/* single stream variants */
size_t HUFv07_decompress1X2 (void* dst, size_t dstSize, const void* cSrc, size_t cSrcSize);   /* single-symbol decoder */
size_t HUFv07_decompress1X4 (void* dst, size_t dstSize, const void* cSrc, size_t cSrcSize);   /* double-symbol decoder */

size_t HUFv07_decompress1X_usingDTable(void* dst, size_t maxDstSize, const void* cSrc, size_t cSrcSize, const HUFv07_DTable* DTable);
size_t HUFv07_decompress1X2_usingDTable(void* dst, size_t maxDstSize, const void* cSrc, size_t cSrcSize, const HUFv07_DTable* DTable);
size_t HUFv07_decompress1X4_usingDTable(void* dst, size_t maxDstSize, const void* cSrc, size_t cSrcSize, const HUFv07_DTable* DTable);


#endif /* HUFv07_STATIC_LINKING_ONLY */


#if defined (__cplusplus)
}
#endif

#endif   /* HUFv07_H_298734234 */
/*
   Common functions of New Generation Entropy library
   Copyright (C) 2016, Yann Collet.

   BSD 2-Clause License (http://www.opensource.org/licenses/bsd-license.php)

   Redistribution and use in source and binary forms, with or without
   modification, are permitted provided that the following conditions are
   met:

       * Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.
       * Redistributions in binary form must reproduce the above
   copyright notice, this list of conditions and the following disclaimer
   in the documentation and/or other materials provided with the
   distribution.

   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
   OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
   THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

    You can contact the author at :
    - FSE+HUF source repository : https://github.com/Cyan4973/FiniteStateEntropy
    - Public forum : https://groups.google.com/forum/#!forum/lz4c
*************************************************************************** */



/*-****************************************
*  FSE Error Management
******************************************/
unsigned FSEv07_isError(size_t code) { return ERR_isError(code); }

const char* FSEv07_getErrorName(size_t code) { return ERR_getErrorName(code); }


/* **************************************************************
*  HUF Error Management
****************************************************************/
unsigned HUFv07_isError(size_t code) { return ERR_isError(code); }

const char* HUFv07_getErrorName(size_t code) { return ERR_getErrorName(code); }


/*-**************************************************************
*  FSE NCount encoding-decoding
****************************************************************/
static short FSEv07_abs(short a) { return (short)(a<0 ? -a : a); }

size_t FSEv07_readNCount (short* normalizedCounter, unsigned* maxSVPtr, unsigned* tableLogPtr,
                 const void* headerBuffer, size_t hbSize)
{
    const BYTE* const istart = (const BYTE*) headerBuffer;
    const BYTE* const iend = istart + hbSize;
    const BYTE* ip = istart;
    int nbBits;
    int remaining;
    int threshold;
    U32 bitStream;
    int bitCount;
    unsigned charnum = 0;
    int previous0 = 0;

    if (hbSize < 4) return ERROR(srcSize_wrong);
    bitStream = MEM_readLE32(ip);
    nbBits = (bitStream & 0xF) + FSEv07_MIN_TABLELOG;   /* extract tableLog */
    if (nbBits > FSEv07_TABLELOG_ABSOLUTE_MAX) return ERROR(tableLog_tooLarge);
    bitStream >>= 4;
    bitCount = 4;
    *tableLogPtr = nbBits;
    remaining = (1<<nbBits)+1;
    threshold = 1<<nbBits;
    nbBits++;

    while ((remaining>1) && (charnum<=*maxSVPtr)) {
        if (previous0) {
            unsigned n0 = charnum;
            while ((bitStream & 0xFFFF) == 0xFFFF) {
                n0+=24;
                if (ip < iend-5) {
                    ip+=2;
                    bitStream = MEM_readLE32(ip) >> bitCount;
                } else {
                    bitStream >>= 16;
                    bitCount+=16;
            }   }
            while ((bitStream & 3) == 3) {
                n0+=3;
                bitStream>>=2;
                bitCount+=2;
            }
            n0 += bitStream & 3;
            bitCount += 2;
            if (n0 > *maxSVPtr) return ERROR(maxSymbolValue_tooSmall);
            while (charnum < n0) normalizedCounter[charnum++] = 0;
            if ((ip <= iend-7) || (ip + (bitCount>>3) <= iend-4)) {
                ip += bitCount>>3;
                bitCount &= 7;
                bitStream = MEM_readLE32(ip) >> bitCount;
            }
            else
                bitStream >>= 2;
        }
        {   short const max = (short)((2*threshold-1)-remaining);
            short count;

            if ((bitStream & (threshold-1)) < (U32)max) {
                count = (short)(bitStream & (threshold-1));
                bitCount   += nbBits-1;
            } else {
                count = (short)(bitStream & (2*threshold-1));
                if (count >= threshold) count -= max;
                bitCount   += nbBits;
            }

            count--;   /* extra accuracy */
            remaining -= FSEv07_abs(count);
            normalizedCounter[charnum++] = count;
            previous0 = !count;
            while (remaining < threshold) {
                nbBits--;
                threshold >>= 1;
            }

            if ((ip <= iend-7) || (ip + (bitCount>>3) <= iend-4)) {
                ip += bitCount>>3;
                bitCount &= 7;
            } else {
                bitCount -= (int)(8 * (iend - 4 - ip));
                ip = iend - 4;
            }
            bitStream = MEM_readLE32(ip) >> (bitCount & 31);
    }   }   /* while ((remaining>1) && (charnum<=*maxSVPtr)) */
    if (remaining != 1) return ERROR(GENERIC);
    *maxSVPtr = charnum-1;

    ip += (bitCount+7)>>3;
    if ((size_t)(ip-istart) > hbSize) return ERROR(srcSize_wrong);
    return ip-istart;
}


/*! HUFv07_readStats() :
    Read compact Huffman tree, saved by HUFv07_writeCTable().
    `huffWeight` is destination buffer.
    @return : size read from `src` , or an error Code .
    Note : Needed by HUFv07_readCTable() and HUFv07_readDTableXn() .
*/
size_t HUFv07_readStats(BYTE* huffWeight, size_t hwSize, U32* rankStats,
                     U32* nbSymbolsPtr, U32* tableLogPtr,
                     const void* src, size_t srcSize)
{
    U32 weightTotal;
    const BYTE* ip = (const BYTE*) src;
    size_t iSize;
    size_t oSize;

    if (!srcSize) return ERROR(srcSize_wrong);
    iSize = ip[0];
    //memset(huffWeight, 0, hwSize);   /* is not necessary, even though some analyzer complain ... */

    if (iSize >= 128)  { /* special header */
        if (iSize >= (242)) {  /* RLE */
            static U32 l[14] = { 1, 2, 3, 4, 7, 8, 15, 16, 31, 32, 63, 64, 127, 128 };
            oSize = l[iSize-242];
            memset(huffWeight, 1, hwSize);
            iSize = 0;
        }
        else {   /* Incompressible */
            oSize = iSize - 127;
            iSize = ((oSize+1)/2);
            if (iSize+1 > srcSize) return ERROR(srcSize_wrong);
            if (oSize >= hwSize) return ERROR(corruption_detected);
            ip += 1;
            {   U32 n;
                for (n=0; n<oSize; n+=2) {
                    huffWeight[n]   = ip[n/2] >> 4;
                    huffWeight[n+1] = ip[n/2] & 15;
    }   }   }   }
    else  {   /* header compressed with FSE (normal case) */
        if (iSize+1 > srcSize) return ERROR(srcSize_wrong);
        oSize = FSEv07_decompress(huffWeight, hwSize-1, ip+1, iSize);   /* max (hwSize-1) values decoded, as last one is implied */
        if (FSEv07_isError(oSize)) return oSize;
    }

    /* collect weight stats */
    memset(rankStats, 0, (HUFv07_TABLELOG_ABSOLUTEMAX + 1) * sizeof(U32));
    weightTotal = 0;
    {   U32 n; for (n=0; n<oSize; n++) {
            if (huffWeight[n] >= HUFv07_TABLELOG_ABSOLUTEMAX) return ERROR(corruption_detected);
            rankStats[huffWeight[n]]++;
            weightTotal += (1 << huffWeight[n]) >> 1;
    }   }
    if (weightTotal == 0) return ERROR(corruption_detected);

    /* get last non-null symbol weight (implied, total must be 2^n) */
    {   U32 const tableLog = BITv07_highbit32(weightTotal) + 1;
        if (tableLog > HUFv07_TABLELOG_ABSOLUTEMAX) return ERROR(corruption_detected);
        *tableLogPtr = tableLog;
        /* determine last weight */
        {   U32 const total = 1 << tableLog;
            U32 const rest = total - weightTotal;
            U32 const verif = 1 << BITv07_highbit32(rest);
            U32 const lastWeight = BITv07_highbit32(rest) + 1;
            if (verif != rest) return ERROR(corruption_detected);    /* last value must be a clean power of 2 */
            huffWeight[oSize] = (BYTE)lastWeight;
            rankStats[lastWeight]++;
    }   }

    /* check tree construction validity */
    if ((rankStats[1] < 2) || (rankStats[1] & 1)) return ERROR(corruption_detected);   /* by construction : at least 2 elts of rank 1, must be even */

    /* results */
    *nbSymbolsPtr = (U32)(oSize+1);
    return iSize+1;
}
/* ******************************************************************
   FSE : Finite State Entropy decoder
   Copyright (C) 2013-2015, Yann Collet.

   BSD 2-Clause License (http://www.opensource.org/licenses/bsd-license.php)

   Redistribution and use in source and binary forms, with or without
   modification, are permitted provided that the following conditions are
   met:

       * Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.
       * Redistributions in binary form must reproduce the above
   copyright notice, this list of conditions and the following disclaimer
   in the documentation and/or other materials provided with the
   distribution.

   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
   OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
   THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

    You can contact the author at :
    - FSE source repository : https://github.com/Cyan4973/FiniteStateEntropy
    - Public forum : https://groups.google.com/forum/#!forum/lz4c
****************************************************************** */


/* **************************************************************
*  Compiler specifics
****************************************************************/
#ifdef _MSC_VER    /* Visual Studio */
#  define FORCE_INLINE static __forceinline
#  include <intrin.h>                    /* For Visual 2005 */
#  pragma warning(disable : 4127)        /* disable: C4127: conditional expression is constant */
#  pragma warning(disable : 4214)        /* disable: C4214: non-int bitfields */
#else
#  if defined (__cplusplus) || defined (__STDC_VERSION__) && __STDC_VERSION__ >= 199901L   /* C99 */
#    ifdef __GNUC__
#      define FORCE_INLINE static inline __attribute__((always_inline))
#    else
#      define FORCE_INLINE static inline
#    endif
#  else
#    define FORCE_INLINE static
#  endif /* __STDC_VERSION__ */
#endif


/* **************************************************************
*  Error Management
****************************************************************/
#define FSEv07_isError ERR_isError
#define FSEv07_STATIC_ASSERT(c) { enum { FSEv07_static_assert = 1/(int)(!!(c)) }; }   /* use only *after* variable declarations */


/* **************************************************************
*  Complex types
****************************************************************/
typedef U32 DTable_max_t[FSEv07_DTABLE_SIZE_U32(FSEv07_MAX_TABLELOG)];


/* **************************************************************
*  Templates
****************************************************************/
/*
  designed to be included
  for type-specific functions (template emulation in C)
  Objective is to write these functions only once, for improved maintenance
*/

/* safety checks */
#ifndef FSEv07_FUNCTION_EXTENSION
#  error "FSEv07_FUNCTION_EXTENSION must be defined"
#endif
#ifndef FSEv07_FUNCTION_TYPE
#  error "FSEv07_FUNCTION_TYPE must be defined"
#endif

/* Function names */
#define FSEv07_CAT(X,Y) X##Y
#define FSEv07_FUNCTION_NAME(X,Y) FSEv07_CAT(X,Y)
#define FSEv07_TYPE_NAME(X,Y) FSEv07_CAT(X,Y)


/* Function templates */
FSEv07_DTable* FSEv07_createDTable (unsigned tableLog)
{
    if (tableLog > FSEv07_TABLELOG_ABSOLUTE_MAX) tableLog = FSEv07_TABLELOG_ABSOLUTE_MAX;
    return (FSEv07_DTable*)malloc( FSEv07_DTABLE_SIZE_U32(tableLog) * sizeof (U32) );
}

void FSEv07_freeDTable (FSEv07_DTable* dt)
{
    free(dt);
}

size_t FSEv07_buildDTable(FSEv07_DTable* dt, const short* normalizedCounter, unsigned maxSymbolValue, unsigned tableLog)
{
    void* const tdPtr = dt+1;   /* because *dt is unsigned, 32-bits aligned on 32-bits */
    FSEv07_DECODE_TYPE* const tableDecode = (FSEv07_DECODE_TYPE*) (tdPtr);
    U16 symbolNext[FSEv07_MAX_SYMBOL_VALUE+1];

    U32 const maxSV1 = maxSymbolValue + 1;
    U32 const tableSize = 1 << tableLog;
    U32 highThreshold = tableSize-1;

    /* Sanity Checks */
    if (maxSymbolValue > FSEv07_MAX_SYMBOL_VALUE) return ERROR(maxSymbolValue_tooLarge);
    if (tableLog > FSEv07_MAX_TABLELOG) return ERROR(tableLog_tooLarge);

    /* Init, lay down lowprob symbols */
    {   FSEv07_DTableHeader DTableH;
        DTableH.tableLog = (U16)tableLog;
        DTableH.fastMode = 1;
        {   S16 const largeLimit= (S16)(1 << (tableLog-1));
            U32 s;
            for (s=0; s<maxSV1; s++) {
                if (normalizedCounter[s]==-1) {
                    tableDecode[highThreshold--].symbol = (FSEv07_FUNCTION_TYPE)s;
                    symbolNext[s] = 1;
                } else {
                    if (normalizedCounter[s] >= largeLimit) DTableH.fastMode=0;
                    symbolNext[s] = normalizedCounter[s];
        }   }   }
        memcpy(dt, &DTableH, sizeof(DTableH));
    }

    /* Spread symbols */
    {   U32 const tableMask = tableSize-1;
        U32 const step = FSEv07_TABLESTEP(tableSize);
        U32 s, position = 0;
        for (s=0; s<maxSV1; s++) {
            int i;
            for (i=0; i<normalizedCounter[s]; i++) {
                tableDecode[position].symbol = (FSEv07_FUNCTION_TYPE)s;
                position = (position + step) & tableMask;
                while (position > highThreshold) position = (position + step) & tableMask;   /* lowprob area */
        }   }

        if (position!=0) return ERROR(GENERIC);   /* position must reach all cells once, otherwise normalizedCounter is incorrect */
    }

    /* Build Decoding table */
    {   U32 u;
        for (u=0; u<tableSize; u++) {
            FSEv07_FUNCTION_TYPE const symbol = (FSEv07_FUNCTION_TYPE)(tableDecode[u].symbol);
            U16 nextState = symbolNext[symbol]++;
            tableDecode[u].nbBits = (BYTE) (tableLog - BITv07_highbit32 ((U32)nextState) );
            tableDecode[u].newState = (U16) ( (nextState << tableDecode[u].nbBits) - tableSize);
    }   }

    return 0;
}



#ifndef FSEv07_COMMONDEFS_ONLY

/*-*******************************************************
*  Decompression (Byte symbols)
*********************************************************/
size_t FSEv07_buildDTable_rle (FSEv07_DTable* dt, BYTE symbolValue)
{
    void* ptr = dt;
    FSEv07_DTableHeader* const DTableH = (FSEv07_DTableHeader*)ptr;
    void* dPtr = dt + 1;
    FSEv07_decode_t* const cell = (FSEv07_decode_t*)dPtr;

    DTableH->tableLog = 0;
    DTableH->fastMode = 0;

    cell->newState = 0;
    cell->symbol = symbolValue;
    cell->nbBits = 0;

    return 0;
}


size_t FSEv07_buildDTable_raw (FSEv07_DTable* dt, unsigned nbBits)
{
    void* ptr = dt;
    FSEv07_DTableHeader* const DTableH = (FSEv07_DTableHeader*)ptr;
    void* dPtr = dt + 1;
    FSEv07_decode_t* const dinfo = (FSEv07_decode_t*)dPtr;
    const unsigned tableSize = 1 << nbBits;
    const unsigned tableMask = tableSize - 1;
    const unsigned maxSV1 = tableMask+1;
    unsigned s;

    /* Sanity checks */
    if (nbBits < 1) return ERROR(GENERIC);         /* min size */

    /* Build Decoding Table */
    DTableH->tableLog = (U16)nbBits;
    DTableH->fastMode = 1;
    for (s=0; s<maxSV1; s++) {
        dinfo[s].newState = 0;
        dinfo[s].symbol = (BYTE)s;
        dinfo[s].nbBits = (BYTE)nbBits;
    }

    return 0;
}

FORCE_INLINE size_t FSEv07_decompress_usingDTable_generic(
          void* dst, size_t maxDstSize,
    const void* cSrc, size_t cSrcSize,
    const FSEv07_DTable* dt, const unsigned fast)
{
    BYTE* const ostart = (BYTE*) dst;
    BYTE* op = ostart;
    BYTE* const omax = op + maxDstSize;
    BYTE* const olimit = omax-3;

    BITv07_DStream_t bitD;
    FSEv07_DState_t state1;
    FSEv07_DState_t state2;

    /* Init */
    { size_t const errorCode = BITv07_initDStream(&bitD, cSrc, cSrcSize);   /* replaced last arg by maxCompressed Size */
      if (FSEv07_isError(errorCode)) return errorCode; }

    FSEv07_initDState(&state1, &bitD, dt);
    FSEv07_initDState(&state2, &bitD, dt);

#define FSEv07_GETSYMBOL(statePtr) fast ? FSEv07_decodeSymbolFast(statePtr, &bitD) : FSEv07_decodeSymbol(statePtr, &bitD)

    /* 4 symbols per loop */
    for ( ; (BITv07_reloadDStream(&bitD)==BITv07_DStream_unfinished) && (op<olimit) ; op+=4) {
        op[0] = FSEv07_GETSYMBOL(&state1);

        if (FSEv07_MAX_TABLELOG*2+7 > sizeof(bitD.bitContainer)*8)    /* This test must be static */
            BITv07_reloadDStream(&bitD);

        op[1] = FSEv07_GETSYMBOL(&state2);

        if (FSEv07_MAX_TABLELOG*4+7 > sizeof(bitD.bitContainer)*8)    /* This test must be static */
            { if (BITv07_reloadDStream(&bitD) > BITv07_DStream_unfinished) { op+=2; break; } }

        op[2] = FSEv07_GETSYMBOL(&state1);

        if (FSEv07_MAX_TABLELOG*2+7 > sizeof(bitD.bitContainer)*8)    /* This test must be static */
            BITv07_reloadDStream(&bitD);

        op[3] = FSEv07_GETSYMBOL(&state2);
    }

    /* tail */
    /* note : BITv07_reloadDStream(&bitD) >= FSEv07_DStream_partiallyFilled; Ends at exactly BITv07_DStream_completed */
    while (1) {
        if (op>(omax-2)) return ERROR(dstSize_tooSmall);

        *op++ = FSEv07_GETSYMBOL(&state1);

        if (BITv07_reloadDStream(&bitD)==BITv07_DStream_overflow) {
            *op++ = FSEv07_GETSYMBOL(&state2);
            break;
        }

        if (op>(omax-2)) return ERROR(dstSize_tooSmall);

        *op++ = FSEv07_GETSYMBOL(&state2);

        if (BITv07_reloadDStream(&bitD)==BITv07_DStream_overflow) {
            *op++ = FSEv07_GETSYMBOL(&state1);
            break;
    }   }

    return op-ostart;
}


size_t FSEv07_decompress_usingDTable(void* dst, size_t originalSize,
                            const void* cSrc, size_t cSrcSize,
                            const FSEv07_DTable* dt)
{
    const void* ptr = dt;
    const FSEv07_DTableHeader* DTableH = (const FSEv07_DTableHeader*)ptr;
    const U32 fastMode = DTableH->fastMode;

    /* select fast mode (static) */
    if (fastMode) return FSEv07_decompress_usingDTable_generic(dst, originalSize, cSrc, cSrcSize, dt, 1);
    return FSEv07_decompress_usingDTable_generic(dst, originalSize, cSrc, cSrcSize, dt, 0);
}


size_t FSEv07_decompress(void* dst, size_t maxDstSize, const void* cSrc, size_t cSrcSize)
{
    const BYTE* const istart = (const BYTE*)cSrc;
    const BYTE* ip = istart;
    short counting[FSEv07_MAX_SYMBOL_VALUE+1];
    DTable_max_t dt;   /* Static analyzer seems unable to understand this table will be properly initialized later */
    unsigned tableLog;
    unsigned maxSymbolValue = FSEv07_MAX_SYMBOL_VALUE;

    if (cSrcSize<2) return ERROR(srcSize_wrong);   /* too small input size */

    /* normal FSE decoding mode */
    {   size_t const NCountLength = FSEv07_readNCount (counting, &maxSymbolValue, &tableLog, istart, cSrcSize);
        if (FSEv07_isError(NCountLength)) return NCountLength;
        if (NCountLength >= cSrcSize) return ERROR(srcSize_wrong);   /* too small input size */
        ip += NCountLength;
        cSrcSize -= NCountLength;
    }

    { size_t const errorCode = FSEv07_buildDTable (dt, counting, maxSymbolValue, tableLog);
      if (FSEv07_isError(errorCode)) return errorCode; }

    return FSEv07_decompress_usingDTable (dst, maxDstSize, ip, cSrcSize, dt);   /* always return, even if it is an error code */
}



#endif   /* FSEv07_COMMONDEFS_ONLY */

/* ******************************************************************
   Huffman decoder, part of New Generation Entropy library
   Copyright (C) 2013-2016, Yann Collet.

   BSD 2-Clause License (http://www.opensource.org/licenses/bsd-license.php)

   Redistribution and use in source and binary forms, with or without
   modification, are permitted provided that the following conditions are
   met:

       * Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.
       * Redistributions in binary form must reproduce the above
   copyright notice, this list of conditions and the following disclaimer
   in the documentation and/or other materials provided with the
   distribution.

   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
   OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
   THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

    You can contact the author at :
    - FSE+HUF source repository : https://github.com/Cyan4973/FiniteStateEntropy
    - Public forum : https://groups.google.com/forum/#!forum/lz4c
****************************************************************** */

/* **************************************************************
*  Compiler specifics
****************************************************************/
#if defined (__cplusplus) || (defined (__STDC_VERSION__) && (__STDC_VERSION__ >= 199901L) /* C99 */)
/* inline is defined */
#elif defined(_MSC_VER)
#  define inline __inline
#else
#  define inline /* disable inline */
#endif


#ifdef _MSC_VER    /* Visual Studio */
#  pragma warning(disable : 4127)        /* disable: C4127: conditional expression is constant */
#endif



/* **************************************************************
*  Error Management
****************************************************************/
#define HUFv07_STATIC_ASSERT(c) { enum { HUFv07_static_assert = 1/(int)(!!(c)) }; }   /* use only *after* variable declarations */


/*-***************************/
/*  generic DTableDesc       */
/*-***************************/

typedef struct { BYTE maxTableLog; BYTE tableType; BYTE tableLog; BYTE reserved; } DTableDesc;

static DTableDesc HUFv07_getDTableDesc(const HUFv07_DTable* table)
{
    DTableDesc dtd;
    memcpy(&dtd, table, sizeof(dtd));
    return dtd;
}


/*-***************************/
/*  single-symbol decoding   */
/*-***************************/

typedef struct { BYTE byte; BYTE nbBits; } HUFv07_DEltX2;   /* single-symbol decoding */

size_t HUFv07_readDTableX2 (HUFv07_DTable* DTable, const void* src, size_t srcSize)
{
    BYTE huffWeight[HUFv07_SYMBOLVALUE_MAX + 1];
    U32 rankVal[HUFv07_TABLELOG_ABSOLUTEMAX + 1];   /* large enough for values from 0 to 16 */
    U32 tableLog = 0;
    U32 nbSymbols = 0;
    size_t iSize;
    void* const dtPtr = DTable + 1;
    HUFv07_DEltX2* const dt = (HUFv07_DEltX2*)dtPtr;

    HUFv07_STATIC_ASSERT(sizeof(DTableDesc) == sizeof(HUFv07_DTable));
    //memset(huffWeight, 0, sizeof(huffWeight));   /* is not necessary, even though some analyzer complain ... */

    iSize = HUFv07_readStats(huffWeight, HUFv07_SYMBOLVALUE_MAX + 1, rankVal, &nbSymbols, &tableLog, src, srcSize);
    if (HUFv07_isError(iSize)) return iSize;

    /* Table header */
    {   DTableDesc dtd = HUFv07_getDTableDesc(DTable);
        if (tableLog > (U32)(dtd.maxTableLog+1)) return ERROR(tableLog_tooLarge);   /* DTable too small, huffman tree cannot fit in */
        dtd.tableType = 0;
        dtd.tableLog = (BYTE)tableLog;
        memcpy(DTable, &dtd, sizeof(dtd));
    }

    /* Prepare ranks */
    {   U32 n, nextRankStart = 0;
        for (n=1; n<tableLog+1; n++) {
            U32 current = nextRankStart;
            nextRankStart += (rankVal[n] << (n-1));
            rankVal[n] = current;
    }   }

    /* fill DTable */
    {   U32 n;
        for (n=0; n<nbSymbols; n++) {
            U32 const w = huffWeight[n];
            U32 const length = (1 << w) >> 1;
            U32 i;
            HUFv07_DEltX2 D;
            D.byte = (BYTE)n; D.nbBits = (BYTE)(tableLog + 1 - w);
            for (i = rankVal[w]; i < rankVal[w] + length; i++)
                dt[i] = D;
            rankVal[w] += length;
    }   }

    return iSize;
}


static BYTE HUFv07_decodeSymbolX2(BITv07_DStream_t* Dstream, const HUFv07_DEltX2* dt, const U32 dtLog)
{
    size_t const val = BITv07_lookBitsFast(Dstream, dtLog); /* note : dtLog >= 1 */
    BYTE const c = dt[val].byte;
    BITv07_skipBits(Dstream, dt[val].nbBits);
    return c;
}

#define HUFv07_DECODE_SYMBOLX2_0(ptr, DStreamPtr) \
    *ptr++ = HUFv07_decodeSymbolX2(DStreamPtr, dt, dtLog)

#define HUFv07_DECODE_SYMBOLX2_1(ptr, DStreamPtr) \
    if (MEM_64bits() || (HUFv07_TABLELOG_MAX<=12)) \
        HUFv07_DECODE_SYMBOLX2_0(ptr, DStreamPtr)

#define HUFv07_DECODE_SYMBOLX2_2(ptr, DStreamPtr) \
    if (MEM_64bits()) \
        HUFv07_DECODE_SYMBOLX2_0(ptr, DStreamPtr)

static inline size_t HUFv07_decodeStreamX2(BYTE* p, BITv07_DStream_t* const bitDPtr, BYTE* const pEnd, const HUFv07_DEltX2* const dt, const U32 dtLog)
{
    BYTE* const pStart = p;

    /* up to 4 symbols at a time */
    while ((BITv07_reloadDStream(bitDPtr) == BITv07_DStream_unfinished) && (p <= pEnd-4)) {
        HUFv07_DECODE_SYMBOLX2_2(p, bitDPtr);
        HUFv07_DECODE_SYMBOLX2_1(p, bitDPtr);
        HUFv07_DECODE_SYMBOLX2_2(p, bitDPtr);
        HUFv07_DECODE_SYMBOLX2_0(p, bitDPtr);
    }

    /* closer to the end */
    while ((BITv07_reloadDStream(bitDPtr) == BITv07_DStream_unfinished) && (p < pEnd))
        HUFv07_DECODE_SYMBOLX2_0(p, bitDPtr);

    /* no more data to retrieve from bitstream, hence no need to reload */
    while (p < pEnd)
        HUFv07_DECODE_SYMBOLX2_0(p, bitDPtr);

    return pEnd-pStart;
}

static size_t HUFv07_decompress1X2_usingDTable_internal(
          void* dst,  size_t dstSize,
    const void* cSrc, size_t cSrcSize,
    const HUFv07_DTable* DTable)
{
    BYTE* op = (BYTE*)dst;
    BYTE* const oend = op + dstSize;
    const void* dtPtr = DTable + 1;
    const HUFv07_DEltX2* const dt = (const HUFv07_DEltX2*)dtPtr;
    BITv07_DStream_t bitD;
    DTableDesc const dtd = HUFv07_getDTableDesc(DTable);
    U32 const dtLog = dtd.tableLog;

    { size_t const errorCode = BITv07_initDStream(&bitD, cSrc, cSrcSize);
      if (HUFv07_isError(errorCode)) return errorCode; }

    HUFv07_decodeStreamX2(op, &bitD, oend, dt, dtLog);

    /* check */
    if (!BITv07_endOfDStream(&bitD)) return ERROR(corruption_detected);

    return dstSize;
}

size_t HUFv07_decompress1X2_usingDTable(
          void* dst,  size_t dstSize,
    const void* cSrc, size_t cSrcSize,
    const HUFv07_DTable* DTable)
{
    DTableDesc dtd = HUFv07_getDTableDesc(DTable);
    if (dtd.tableType != 0) return ERROR(GENERIC);
    return HUFv07_decompress1X2_usingDTable_internal(dst, dstSize, cSrc, cSrcSize, DTable);
}

size_t HUFv07_decompress1X2_DCtx (HUFv07_DTable* DCtx, void* dst, size_t dstSize, const void* cSrc, size_t cSrcSize)
{
    const BYTE* ip = (const BYTE*) cSrc;

    size_t const hSize = HUFv07_readDTableX2 (DCtx, cSrc, cSrcSize);
    if (HUFv07_isError(hSize)) return hSize;
    if (hSize >= cSrcSize) return ERROR(srcSize_wrong);
    ip += hSize; cSrcSize -= hSize;

    return HUFv07_decompress1X2_usingDTable_internal (dst, dstSize, ip, cSrcSize, DCtx);
}

size_t HUFv07_decompress1X2 (void* dst, size_t dstSize, const void* cSrc, size_t cSrcSize)
{
    HUFv07_CREATE_STATIC_DTABLEX2(DTable, HUFv07_TABLELOG_MAX);
    return HUFv07_decompress1X2_DCtx (DTable, dst, dstSize, cSrc, cSrcSize);
}


static size_t HUFv07_decompress4X2_usingDTable_internal(
          void* dst,  size_t dstSize,
    const void* cSrc, size_t cSrcSize,
    const HUFv07_DTable* DTable)
{
    /* Check */
    if (cSrcSize < 10) return ERROR(corruption_detected);  /* strict minimum : jump table + 1 byte per stream */

    {   const BYTE* const istart = (const BYTE*) cSrc;
        BYTE* const ostart = (BYTE*) dst;
        BYTE* const oend = ostart + dstSize;
        const void* const dtPtr = DTable + 1;
        const HUFv07_DEltX2* const dt = (const HUFv07_DEltX2*)dtPtr;

        /* Init */
        BITv07_DStream_t bitD1;
        BITv07_DStream_t bitD2;
        BITv07_DStream_t bitD3;
        BITv07_DStream_t bitD4;
        size_t const length1 = MEM_readLE16(istart);
        size_t const length2 = MEM_readLE16(istart+2);
        size_t const length3 = MEM_readLE16(istart+4);
        size_t const length4 = cSrcSize - (length1 + length2 + length3 + 6);
        const BYTE* const istart1 = istart + 6;  /* jumpTable */
        const BYTE* const istart2 = istart1 + length1;
        const BYTE* const istart3 = istart2 + length2;
        const BYTE* const istart4 = istart3 + length3;
        const size_t segmentSize = (dstSize+3) / 4;
        BYTE* const opStart2 = ostart + segmentSize;
        BYTE* const opStart3 = opStart2 + segmentSize;
        BYTE* const opStart4 = opStart3 + segmentSize;
        BYTE* op1 = ostart;
        BYTE* op2 = opStart2;
        BYTE* op3 = opStart3;
        BYTE* op4 = opStart4;
        U32 endSignal;
        DTableDesc const dtd = HUFv07_getDTableDesc(DTable);
        U32 const dtLog = dtd.tableLog;

        if (length4 > cSrcSize) return ERROR(corruption_detected);   /* overflow */
        { size_t const errorCode = BITv07_initDStream(&bitD1, istart1, length1);
          if (HUFv07_isError(errorCode)) return errorCode; }
        { size_t const errorCode = BITv07_initDStream(&bitD2, istart2, length2);
          if (HUFv07_isError(errorCode)) return errorCode; }
        { size_t const errorCode = BITv07_initDStream(&bitD3, istart3, length3);
          if (HUFv07_isError(errorCode)) return errorCode; }
        { size_t const errorCode = BITv07_initDStream(&bitD4, istart4, length4);
          if (HUFv07_isError(errorCode)) return errorCode; }

        /* 16-32 symbols per loop (4-8 symbols per stream) */
        endSignal = BITv07_reloadDStream(&bitD1) | BITv07_reloadDStream(&bitD2) | BITv07_reloadDStream(&bitD3) | BITv07_reloadDStream(&bitD4);
        for ( ; (endSignal==BITv07_DStream_unfinished) && (op4<(oend-7)) ; ) {
            HUFv07_DECODE_SYMBOLX2_2(op1, &bitD1);
            HUFv07_DECODE_SYMBOLX2_2(op2, &bitD2);
            HUFv07_DECODE_SYMBOLX2_2(op3, &bitD3);
            HUFv07_DECODE_SYMBOLX2_2(op4, &bitD4);
            HUFv07_DECODE_SYMBOLX2_1(op1, &bitD1);
            HUFv07_DECODE_SYMBOLX2_1(op2, &bitD2);
            HUFv07_DECODE_SYMBOLX2_1(op3, &bitD3);
            HUFv07_DECODE_SYMBOLX2_1(op4, &bitD4);
            HUFv07_DECODE_SYMBOLX2_2(op1, &bitD1);
            HUFv07_DECODE_SYMBOLX2_2(op2, &bitD2);
            HUFv07_DECODE_SYMBOLX2_2(op3, &bitD3);
            HUFv07_DECODE_SYMBOLX2_2(op4, &bitD4);
            HUFv07_DECODE_SYMBOLX2_0(op1, &bitD1);
            HUFv07_DECODE_SYMBOLX2_0(op2, &bitD2);
            HUFv07_DECODE_SYMBOLX2_0(op3, &bitD3);
            HUFv07_DECODE_SYMBOLX2_0(op4, &bitD4);
            endSignal = BITv07_reloadDStream(&bitD1) | BITv07_reloadDStream(&bitD2) | BITv07_reloadDStream(&bitD3) | BITv07_reloadDStream(&bitD4);
        }

        /* check corruption */
        if (op1 > opStart2) return ERROR(corruption_detected);
        if (op2 > opStart3) return ERROR(corruption_detected);
        if (op3 > opStart4) return ERROR(corruption_detected);
        /* note : op4 supposed already verified within main loop */

        /* finish bitStreams one by one */
        HUFv07_decodeStreamX2(op1, &bitD1, opStart2, dt, dtLog);
        HUFv07_decodeStreamX2(op2, &bitD2, opStart3, dt, dtLog);
        HUFv07_decodeStreamX2(op3, &bitD3, opStart4, dt, dtLog);
        HUFv07_decodeStreamX2(op4, &bitD4, oend,     dt, dtLog);

        /* check */
        endSignal = BITv07_endOfDStream(&bitD1) & BITv07_endOfDStream(&bitD2) & BITv07_endOfDStream(&bitD3) & BITv07_endOfDStream(&bitD4);
        if (!endSignal) return ERROR(corruption_detected);

        /* decoded size */
        return dstSize;
    }
}


size_t HUFv07_decompress4X2_usingDTable(
          void* dst,  size_t dstSize,
    const void* cSrc, size_t cSrcSize,
    const HUFv07_DTable* DTable)
{
    DTableDesc dtd = HUFv07_getDTableDesc(DTable);
    if (dtd.tableType != 0) return ERROR(GENERIC);
    return HUFv07_decompress4X2_usingDTable_internal(dst, dstSize, cSrc, cSrcSize, DTable);
}


size_t HUFv07_decompress4X2_DCtx (HUFv07_DTable* dctx, void* dst, size_t dstSize, const void* cSrc, size_t cSrcSize)
{
    const BYTE* ip = (const BYTE*) cSrc;

    size_t const hSize = HUFv07_readDTableX2 (dctx, cSrc, cSrcSize);
    if (HUFv07_isError(hSize)) return hSize;
    if (hSize >= cSrcSize) return ERROR(srcSize_wrong);
    ip += hSize; cSrcSize -= hSize;

    return HUFv07_decompress4X2_usingDTable_internal (dst, dstSize, ip, cSrcSize, dctx);
}

size_t HUFv07_decompress4X2 (void* dst, size_t dstSize, const void* cSrc, size_t cSrcSize)
{
    HUFv07_CREATE_STATIC_DTABLEX2(DTable, HUFv07_TABLELOG_MAX);
    return HUFv07_decompress4X2_DCtx(DTable, dst, dstSize, cSrc, cSrcSize);
}


/* *************************/
/* double-symbols decoding */
/* *************************/
typedef struct { U16 sequence; BYTE nbBits; BYTE length; } HUFv07_DEltX4;  /* double-symbols decoding */

typedef struct { BYTE symbol; BYTE weight; } sortedSymbol_t;

static void HUFv07_fillDTableX4Level2(HUFv07_DEltX4* DTable, U32 sizeLog, const U32 consumed,
                           const U32* rankValOrigin, const int minWeight,
                           const sortedSymbol_t* sortedSymbols, const U32 sortedListSize,
                           U32 nbBitsBaseline, U16 baseSeq)
{
    HUFv07_DEltX4 DElt;
    U32 rankVal[HUFv07_TABLELOG_ABSOLUTEMAX + 1];

    /* get pre-calculated rankVal */
    memcpy(rankVal, rankValOrigin, sizeof(rankVal));

    /* fill skipped values */
    if (minWeight>1) {
        U32 i, skipSize = rankVal[minWeight];
        MEM_writeLE16(&(DElt.sequence), baseSeq);
        DElt.nbBits   = (BYTE)(consumed);
        DElt.length   = 1;
        for (i = 0; i < skipSize; i++)
            DTable[i] = DElt;
    }

    /* fill DTable */
    { U32 s; for (s=0; s<sortedListSize; s++) {   /* note : sortedSymbols already skipped */
        const U32 symbol = sortedSymbols[s].symbol;
        const U32 weight = sortedSymbols[s].weight;
        const U32 nbBits = nbBitsBaseline - weight;
        const U32 length = 1 << (sizeLog-nbBits);
        const U32 start = rankVal[weight];
        U32 i = start;
        const U32 end = start + length;

        MEM_writeLE16(&(DElt.sequence), (U16)(baseSeq + (symbol << 8)));
        DElt.nbBits = (BYTE)(nbBits + consumed);
        DElt.length = 2;
        do { DTable[i++] = DElt; } while (i<end);   /* since length >= 1 */

        rankVal[weight] += length;
    }}
}

typedef U32 rankVal_t[HUFv07_TABLELOG_ABSOLUTEMAX][HUFv07_TABLELOG_ABSOLUTEMAX + 1];

static void HUFv07_fillDTableX4(HUFv07_DEltX4* DTable, const U32 targetLog,
                           const sortedSymbol_t* sortedList, const U32 sortedListSize,
                           const U32* rankStart, rankVal_t rankValOrigin, const U32 maxWeight,
                           const U32 nbBitsBaseline)
{
    U32 rankVal[HUFv07_TABLELOG_ABSOLUTEMAX + 1];
    const int scaleLog = nbBitsBaseline - targetLog;   /* note : targetLog >= srcLog, hence scaleLog <= 1 */
    const U32 minBits  = nbBitsBaseline - maxWeight;
    U32 s;

    memcpy(rankVal, rankValOrigin, sizeof(rankVal));

    /* fill DTable */
    for (s=0; s<sortedListSize; s++) {
        const U16 symbol = sortedList[s].symbol;
        const U32 weight = sortedList[s].weight;
        const U32 nbBits = nbBitsBaseline - weight;
        const U32 start = rankVal[weight];
        const U32 length = 1 << (targetLog-nbBits);

        if (targetLog-nbBits >= minBits) {   /* enough room for a second symbol */
            U32 sortedRank;
            int minWeight = nbBits + scaleLog;
            if (minWeight < 1) minWeight = 1;
            sortedRank = rankStart[minWeight];
            HUFv07_fillDTableX4Level2(DTable+start, targetLog-nbBits, nbBits,
                           rankValOrigin[nbBits], minWeight,
                           sortedList+sortedRank, sortedListSize-sortedRank,
                           nbBitsBaseline, symbol);
        } else {
            HUFv07_DEltX4 DElt;
            MEM_writeLE16(&(DElt.sequence), symbol);
            DElt.nbBits = (BYTE)(nbBits);
            DElt.length = 1;
            {   U32 u;
                const U32 end = start + length;
                for (u = start; u < end; u++) DTable[u] = DElt;
        }   }
        rankVal[weight] += length;
    }
}

size_t HUFv07_readDTableX4 (HUFv07_DTable* DTable, const void* src, size_t srcSize)
{
    BYTE weightList[HUFv07_SYMBOLVALUE_MAX + 1];
    sortedSymbol_t sortedSymbol[HUFv07_SYMBOLVALUE_MAX + 1];
    U32 rankStats[HUFv07_TABLELOG_ABSOLUTEMAX + 1] = { 0 };
    U32 rankStart0[HUFv07_TABLELOG_ABSOLUTEMAX + 2] = { 0 };
    U32* const rankStart = rankStart0+1;
    rankVal_t rankVal;
    U32 tableLog, maxW, sizeOfSort, nbSymbols;
    DTableDesc dtd = HUFv07_getDTableDesc(DTable);
    U32 const maxTableLog = dtd.maxTableLog;
    size_t iSize;
    void* dtPtr = DTable+1;   /* force compiler to avoid strict-aliasing */
    HUFv07_DEltX4* const dt = (HUFv07_DEltX4*)dtPtr;

    HUFv07_STATIC_ASSERT(sizeof(HUFv07_DEltX4) == sizeof(HUFv07_DTable));   /* if compilation fails here, assertion is false */
    if (maxTableLog > HUFv07_TABLELOG_ABSOLUTEMAX) return ERROR(tableLog_tooLarge);
    //memset(weightList, 0, sizeof(weightList));   /* is not necessary, even though some analyzer complain ... */

    iSize = HUFv07_readStats(weightList, HUFv07_SYMBOLVALUE_MAX + 1, rankStats, &nbSymbols, &tableLog, src, srcSize);
    if (HUFv07_isError(iSize)) return iSize;

    /* check result */
    if (tableLog > maxTableLog) return ERROR(tableLog_tooLarge);   /* DTable can't fit code depth */

    /* find maxWeight */
    for (maxW = tableLog; rankStats[maxW]==0; maxW--) {}  /* necessarily finds a solution before 0 */

    /* Get start index of each weight */
    {   U32 w, nextRankStart = 0;
        for (w=1; w<maxW+1; w++) {
            U32 current = nextRankStart;
            nextRankStart += rankStats[w];
            rankStart[w] = current;
        }
        rankStart[0] = nextRankStart;   /* put all 0w symbols at the end of sorted list*/
        sizeOfSort = nextRankStart;
    }

    /* sort symbols by weight */
    {   U32 s;
        for (s=0; s<nbSymbols; s++) {
            U32 const w = weightList[s];
            U32 const r = rankStart[w]++;
            sortedSymbol[r].symbol = (BYTE)s;
            sortedSymbol[r].weight = (BYTE)w;
        }
        rankStart[0] = 0;   /* forget 0w symbols; this is beginning of weight(1) */
    }

    /* Build rankVal */
    {   U32* const rankVal0 = rankVal[0];
        {   int const rescale = (maxTableLog-tableLog) - 1;   /* tableLog <= maxTableLog */
            U32 nextRankVal = 0;
            U32 w;
            for (w=1; w<maxW+1; w++) {
                U32 current = nextRankVal;
                nextRankVal += rankStats[w] << (w+rescale);
                rankVal0[w] = current;
        }   }
        {   U32 const minBits = tableLog+1 - maxW;
            U32 consumed;
            for (consumed = minBits; consumed < maxTableLog - minBits + 1; consumed++) {
                U32* const rankValPtr = rankVal[consumed];
                U32 w;
                for (w = 1; w < maxW+1; w++) {
                    rankValPtr[w] = rankVal0[w] >> consumed;
    }   }   }   }

    HUFv07_fillDTableX4(dt, maxTableLog,
                   sortedSymbol, sizeOfSort,
                   rankStart0, rankVal, maxW,
                   tableLog+1);

    dtd.tableLog = (BYTE)maxTableLog;
    dtd.tableType = 1;
    memcpy(DTable, &dtd, sizeof(dtd));
    return iSize;
}


static U32 HUFv07_decodeSymbolX4(void* op, BITv07_DStream_t* DStream, const HUFv07_DEltX4* dt, const U32 dtLog)
{
    const size_t val = BITv07_lookBitsFast(DStream, dtLog);   /* note : dtLog >= 1 */
    memcpy(op, dt+val, 2);
    BITv07_skipBits(DStream, dt[val].nbBits);
    return dt[val].length;
}

static U32 HUFv07_decodeLastSymbolX4(void* op, BITv07_DStream_t* DStream, const HUFv07_DEltX4* dt, const U32 dtLog)
{
    const size_t val = BITv07_lookBitsFast(DStream, dtLog);   /* note : dtLog >= 1 */
    memcpy(op, dt+val, 1);
    if (dt[val].length==1) BITv07_skipBits(DStream, dt[val].nbBits);
    else {
        if (DStream->bitsConsumed < (sizeof(DStream->bitContainer)*8)) {
            BITv07_skipBits(DStream, dt[val].nbBits);
            if (DStream->bitsConsumed > (sizeof(DStream->bitContainer)*8))
                DStream->bitsConsumed = (sizeof(DStream->bitContainer)*8);   /* ugly hack; works only because it's the last symbol. Note : can't easily extract nbBits from just this symbol */
    }   }
    return 1;
}


#define HUFv07_DECODE_SYMBOLX4_0(ptr, DStreamPtr) \
    ptr += HUFv07_decodeSymbolX4(ptr, DStreamPtr, dt, dtLog)

#define HUFv07_DECODE_SYMBOLX4_1(ptr, DStreamPtr) \
    if (MEM_64bits() || (HUFv07_TABLELOG_MAX<=12)) \
        ptr += HUFv07_decodeSymbolX4(ptr, DStreamPtr, dt, dtLog)

#define HUFv07_DECODE_SYMBOLX4_2(ptr, DStreamPtr) \
    if (MEM_64bits()) \
        ptr += HUFv07_decodeSymbolX4(ptr, DStreamPtr, dt, dtLog)

static inline size_t HUFv07_decodeStreamX4(BYTE* p, BITv07_DStream_t* bitDPtr, BYTE* const pEnd, const HUFv07_DEltX4* const dt, const U32 dtLog)
{
    BYTE* const pStart = p;

    /* up to 8 symbols at a time */
    while ((BITv07_reloadDStream(bitDPtr) == BITv07_DStream_unfinished) && (p < pEnd-7)) {
        HUFv07_DECODE_SYMBOLX4_2(p, bitDPtr);
        HUFv07_DECODE_SYMBOLX4_1(p, bitDPtr);
        HUFv07_DECODE_SYMBOLX4_2(p, bitDPtr);
        HUFv07_DECODE_SYMBOLX4_0(p, bitDPtr);
    }

    /* closer to end : up to 2 symbols at a time */
    while ((BITv07_reloadDStream(bitDPtr) == BITv07_DStream_unfinished) && (p <= pEnd-2))
        HUFv07_DECODE_SYMBOLX4_0(p, bitDPtr);

    while (p <= pEnd-2)
        HUFv07_DECODE_SYMBOLX4_0(p, bitDPtr);   /* no need to reload : reached the end of DStream */

    if (p < pEnd)
        p += HUFv07_decodeLastSymbolX4(p, bitDPtr, dt, dtLog);

    return p-pStart;
}


static size_t HUFv07_decompress1X4_usingDTable_internal(
          void* dst,  size_t dstSize,
    const void* cSrc, size_t cSrcSize,
    const HUFv07_DTable* DTable)
{
    BITv07_DStream_t bitD;

    /* Init */
    {   size_t const errorCode = BITv07_initDStream(&bitD, cSrc, cSrcSize);
        if (HUFv07_isError(errorCode)) return errorCode;
    }

    /* decode */
    {   BYTE* const ostart = (BYTE*) dst;
        BYTE* const oend = ostart + dstSize;
        const void* const dtPtr = DTable+1;   /* force compiler to not use strict-aliasing */
        const HUFv07_DEltX4* const dt = (const HUFv07_DEltX4*)dtPtr;
        DTableDesc const dtd = HUFv07_getDTableDesc(DTable);
        HUFv07_decodeStreamX4(ostart, &bitD, oend, dt, dtd.tableLog);
    }

    /* check */
    if (!BITv07_endOfDStream(&bitD)) return ERROR(corruption_detected);

    /* decoded size */
    return dstSize;
}

size_t HUFv07_decompress1X4_usingDTable(
          void* dst,  size_t dstSize,
    const void* cSrc, size_t cSrcSize,
    const HUFv07_DTable* DTable)
{
    DTableDesc dtd = HUFv07_getDTableDesc(DTable);
    if (dtd.tableType != 1) return ERROR(GENERIC);
    return HUFv07_decompress1X4_usingDTable_internal(dst, dstSize, cSrc, cSrcSize, DTable);
}

size_t HUFv07_decompress1X4_DCtx (HUFv07_DTable* DCtx, void* dst, size_t dstSize, const void* cSrc, size_t cSrcSize)
{
    const BYTE* ip = (const BYTE*) cSrc;

    size_t const hSize = HUFv07_readDTableX4 (DCtx, cSrc, cSrcSize);
    if (HUFv07_isError(hSize)) return hSize;
    if (hSize >= cSrcSize) return ERROR(srcSize_wrong);
    ip += hSize; cSrcSize -= hSize;

    return HUFv07_decompress1X4_usingDTable_internal (dst, dstSize, ip, cSrcSize, DCtx);
}

size_t HUFv07_decompress1X4 (void* dst, size_t dstSize, const void* cSrc, size_t cSrcSize)
{
    HUFv07_CREATE_STATIC_DTABLEX4(DTable, HUFv07_TABLELOG_MAX);
    return HUFv07_decompress1X4_DCtx(DTable, dst, dstSize, cSrc, cSrcSize);
}

static size_t HUFv07_decompress4X4_usingDTable_internal(
          void* dst,  size_t dstSize,
    const void* cSrc, size_t cSrcSize,
    const HUFv07_DTable* DTable)
{
    if (cSrcSize < 10) return ERROR(corruption_detected);   /* strict minimum : jump table + 1 byte per stream */

    {   const BYTE* const istart = (const BYTE*) cSrc;
        BYTE* const ostart = (BYTE*) dst;
        BYTE* const oend = ostart + dstSize;
        const void* const dtPtr = DTable+1;
        const HUFv07_DEltX4* const dt = (const HUFv07_DEltX4*)dtPtr;

        /* Init */
        BITv07_DStream_t bitD1;
        BITv07_DStream_t bitD2;
        BITv07_DStream_t bitD3;
        BITv07_DStream_t bitD4;
        size_t const length1 = MEM_readLE16(istart);
        size_t const length2 = MEM_readLE16(istart+2);
        size_t const length3 = MEM_readLE16(istart+4);
        size_t const length4 = cSrcSize - (length1 + length2 + length3 + 6);
        const BYTE* const istart1 = istart + 6;  /* jumpTable */
        const BYTE* const istart2 = istart1 + length1;
        const BYTE* const istart3 = istart2 + length2;
        const BYTE* const istart4 = istart3 + length3;
        size_t const segmentSize = (dstSize+3) / 4;
        BYTE* const opStart2 = ostart + segmentSize;
        BYTE* const opStart3 = opStart2 + segmentSize;
        BYTE* const opStart4 = opStart3 + segmentSize;
        BYTE* op1 = ostart;
        BYTE* op2 = opStart2;
        BYTE* op3 = opStart3;
        BYTE* op4 = opStart4;
        U32 endSignal;
        DTableDesc const dtd = HUFv07_getDTableDesc(DTable);
        U32 const dtLog = dtd.tableLog;

        if (length4 > cSrcSize) return ERROR(corruption_detected);   /* overflow */
        { size_t const errorCode = BITv07_initDStream(&bitD1, istart1, length1);
          if (HUFv07_isError(errorCode)) return errorCode; }
        { size_t const errorCode = BITv07_initDStream(&bitD2, istart2, length2);
          if (HUFv07_isError(errorCode)) return errorCode; }
        { size_t const errorCode = BITv07_initDStream(&bitD3, istart3, length3);
          if (HUFv07_isError(errorCode)) return errorCode; }
        { size_t const errorCode = BITv07_initDStream(&bitD4, istart4, length4);
          if (HUFv07_isError(errorCode)) return errorCode; }

        /* 16-32 symbols per loop (4-8 symbols per stream) */
        endSignal = BITv07_reloadDStream(&bitD1) | BITv07_reloadDStream(&bitD2) | BITv07_reloadDStream(&bitD3) | BITv07_reloadDStream(&bitD4);
        for ( ; (endSignal==BITv07_DStream_unfinished) && (op4<(oend-7)) ; ) {
            HUFv07_DECODE_SYMBOLX4_2(op1, &bitD1);
            HUFv07_DECODE_SYMBOLX4_2(op2, &bitD2);
            HUFv07_DECODE_SYMBOLX4_2(op3, &bitD3);
            HUFv07_DECODE_SYMBOLX4_2(op4, &bitD4);
            HUFv07_DECODE_SYMBOLX4_1(op1, &bitD1);
            HUFv07_DECODE_SYMBOLX4_1(op2, &bitD2);
            HUFv07_DECODE_SYMBOLX4_1(op3, &bitD3);
            HUFv07_DECODE_SYMBOLX4_1(op4, &bitD4);
            HUFv07_DECODE_SYMBOLX4_2(op1, &bitD1);
            HUFv07_DECODE_SYMBOLX4_2(op2, &bitD2);
            HUFv07_DECODE_SYMBOLX4_2(op3, &bitD3);
            HUFv07_DECODE_SYMBOLX4_2(op4, &bitD4);
            HUFv07_DECODE_SYMBOLX4_0(op1, &bitD1);
            HUFv07_DECODE_SYMBOLX4_0(op2, &bitD2);
            HUFv07_DECODE_SYMBOLX4_0(op3, &bitD3);
            HUFv07_DECODE_SYMBOLX4_0(op4, &bitD4);

            endSignal = BITv07_reloadDStream(&bitD1) | BITv07_reloadDStream(&bitD2) | BITv07_reloadDStream(&bitD3) | BITv07_reloadDStream(&bitD4);
        }

        /* check corruption */
        if (op1 > opStart2) return ERROR(corruption_detected);
        if (op2 > opStart3) return ERROR(corruption_detected);
        if (op3 > opStart4) return ERROR(corruption_detected);
        /* note : op4 supposed already verified within main loop */

        /* finish bitStreams one by one */
        HUFv07_decodeStreamX4(op1, &bitD1, opStart2, dt, dtLog);
        HUFv07_decodeStreamX4(op2, &bitD2, opStart3, dt, dtLog);
        HUFv07_decodeStreamX4(op3, &bitD3, opStart4, dt, dtLog);
        HUFv07_decodeStreamX4(op4, &bitD4, oend,     dt, dtLog);

        /* check */
        { U32 const endCheck = BITv07_endOfDStream(&bitD1) & BITv07_endOfDStream(&bitD2) & BITv07_endOfDStream(&bitD3) & BITv07_endOfDStream(&bitD4);
          if (!endCheck) return ERROR(corruption_detected); }

        /* decoded size */
        return dstSize;
    }
}


size_t HUFv07_decompress4X4_usingDTable(
          void* dst,  size_t dstSize,
    const void* cSrc, size_t cSrcSize,
    const HUFv07_DTable* DTable)
{
    DTableDesc dtd = HUFv07_getDTableDesc(DTable);
    if (dtd.tableType != 1) return ERROR(GENERIC);
    return HUFv07_decompress4X4_usingDTable_internal(dst, dstSize, cSrc, cSrcSize, DTable);
}


size_t HUFv07_decompress4X4_DCtx (HUFv07_DTable* dctx, void* dst, size_t dstSize, const void* cSrc, size_t cSrcSize)
{
    const BYTE* ip = (const BYTE*) cSrc;

    size_t hSize = HUFv07_readDTableX4 (dctx, cSrc, cSrcSize);
    if (HUFv07_isError(hSize)) return hSize;
    if (hSize >= cSrcSize) return ERROR(srcSize_wrong);
    ip += hSize; cSrcSize -= hSize;

    return HUFv07_decompress4X4_usingDTable_internal(dst, dstSize, ip, cSrcSize, dctx);
}

size_t HUFv07_decompress4X4 (void* dst, size_t dstSize, const void* cSrc, size_t cSrcSize)
{
    HUFv07_CREATE_STATIC_DTABLEX4(DTable, HUFv07_TABLELOG_MAX);
    return HUFv07_decompress4X4_DCtx(DTable, dst, dstSize, cSrc, cSrcSize);
}


/* ********************************/
/* Generic decompression selector */
/* ********************************/

size_t HUFv07_decompress1X_usingDTable(void* dst, size_t maxDstSize,
                                    const void* cSrc, size_t cSrcSize,
                                    const HUFv07_DTable* DTable)
{
    DTableDesc const dtd = HUFv07_getDTableDesc(DTable);
    return dtd.tableType ? HUFv07_decompress1X4_usingDTable_internal(dst, maxDstSize, cSrc, cSrcSize, DTable) :
                           HUFv07_decompress1X2_usingDTable_internal(dst, maxDstSize, cSrc, cSrcSize, DTable);
}

size_t HUFv07_decompress4X_usingDTable(void* dst, size_t maxDstSize,
                                    const void* cSrc, size_t cSrcSize,
                                    const HUFv07_DTable* DTable)
{
    DTableDesc const dtd = HUFv07_getDTableDesc(DTable);
    return dtd.tableType ? HUFv07_decompress4X4_usingDTable_internal(dst, maxDstSize, cSrc, cSrcSize, DTable) :
                           HUFv07_decompress4X2_usingDTable_internal(dst, maxDstSize, cSrc, cSrcSize, DTable);
}


typedef struct { U32 tableTime; U32 decode256Time; } algo_time_t;
static const algo_time_t algoTime[16 /* Quantization */][3 /* single, double, quad */] =
{
    /* single, double, quad */
    {{0,0}, {1,1}, {2,2}},  /* Q==0 : impossible */
    {{0,0}, {1,1}, {2,2}},  /* Q==1 : impossible */
    {{  38,130}, {1313, 74}, {2151, 38}},   /* Q == 2 : 12-18% */
    {{ 448,128}, {1353, 74}, {2238, 41}},   /* Q == 3 : 18-25% */
    {{ 556,128}, {1353, 74}, {2238, 47}},   /* Q == 4 : 25-32% */
    {{ 714,128}, {1418, 74}, {2436, 53}},   /* Q == 5 : 32-38% */
    {{ 883,128}, {1437, 74}, {2464, 61}},   /* Q == 6 : 38-44% */
    {{ 897,128}, {1515, 75}, {2622, 68}},   /* Q == 7 : 44-50% */
    {{ 926,128}, {1613, 75}, {2730, 75}},   /* Q == 8 : 50-56% */
    {{ 947,128}, {1729, 77}, {3359, 77}},   /* Q == 9 : 56-62% */
    {{1107,128}, {2083, 81}, {4006, 84}},   /* Q ==10 : 62-69% */
    {{1177,128}, {2379, 87}, {4785, 88}},   /* Q ==11 : 69-75% */
    {{1242,128}, {2415, 93}, {5155, 84}},   /* Q ==12 : 75-81% */
    {{1349,128}, {2644,106}, {5260,106}},   /* Q ==13 : 81-87% */
    {{1455,128}, {2422,124}, {4174,124}},   /* Q ==14 : 87-93% */
    {{ 722,128}, {1891,145}, {1936,146}},   /* Q ==15 : 93-99% */
};

/** HUFv07_selectDecoder() :
*   Tells which decoder is likely to decode faster,
*   based on a set of pre-determined metrics.
*   @return : 0==HUFv07_decompress4X2, 1==HUFv07_decompress4X4 .
*   Assumption : 0 < cSrcSize < dstSize <= 128 KB */
U32 HUFv07_selectDecoder (size_t dstSize, size_t cSrcSize)
{
    /* decoder timing evaluation */
    U32 const Q = (U32)(cSrcSize * 16 / dstSize);   /* Q < 16 since dstSize > cSrcSize */
    U32 const D256 = (U32)(dstSize >> 8);
    U32 const DTime0 = algoTime[Q][0].tableTime + (algoTime[Q][0].decode256Time * D256);
    U32 DTime1 = algoTime[Q][1].tableTime + (algoTime[Q][1].decode256Time * D256);
    DTime1 += DTime1 >> 3;  /* advantage to algorithm using less memory, for cache eviction */

    return DTime1 < DTime0;
}


typedef size_t (*decompressionAlgo)(void* dst, size_t dstSize, const void* cSrc, size_t cSrcSize);

size_t HUFv07_decompress (void* dst, size_t dstSize, const void* cSrc, size_t cSrcSize)
{
    static const decompressionAlgo decompress[2] = { HUFv07_decompress4X2, HUFv07_decompress4X4 };

    /* validation checks */
    if (dstSize == 0) return ERROR(dstSize_tooSmall);
    if (cSrcSize > dstSize) return ERROR(corruption_detected);   /* invalid */
    if (cSrcSize == dstSize) { memcpy(dst, cSrc, dstSize); return dstSize; }   /* not compressed */
    if (cSrcSize == 1) { memset(dst, *(const BYTE*)cSrc, dstSize); return dstSize; }   /* RLE */

    {   U32 const algoNb = HUFv07_selectDecoder(dstSize, cSrcSize);
        return decompress[algoNb](dst, dstSize, cSrc, cSrcSize);
    }

    //return HUFv07_decompress4X2(dst, dstSize, cSrc, cSrcSize);   /* multi-streams single-symbol decoding */
    //return HUFv07_decompress4X4(dst, dstSize, cSrc, cSrcSize);   /* multi-streams double-symbols decoding */
}

size_t HUFv07_decompress4X_DCtx (HUFv07_DTable* dctx, void* dst, size_t dstSize, const void* cSrc, size_t cSrcSize)
{
    /* validation checks */
    if (dstSize == 0) return ERROR(dstSize_tooSmall);
    if (cSrcSize > dstSize) return ERROR(corruption_detected);   /* invalid */
    if (cSrcSize == dstSize) { memcpy(dst, cSrc, dstSize); return dstSize; }   /* not compressed */
    if (cSrcSize == 1) { memset(dst, *(const BYTE*)cSrc, dstSize); return dstSize; }   /* RLE */

    {   U32 const algoNb = HUFv07_selectDecoder(dstSize, cSrcSize);
        return algoNb ? HUFv07_decompress4X4_DCtx(dctx, dst, dstSize, cSrc, cSrcSize) :
                        HUFv07_decompress4X2_DCtx(dctx, dst, dstSize, cSrc, cSrcSize) ;
    }
}

size_t HUFv07_decompress4X_hufOnly (HUFv07_DTable* dctx, void* dst, size_t dstSize, const void* cSrc, size_t cSrcSize)
{
    /* validation checks */
    if (dstSize == 0) return ERROR(dstSize_tooSmall);
    if ((cSrcSize >= dstSize) || (cSrcSize <= 1)) return ERROR(corruption_detected);   /* invalid */

    {   U32 const algoNb = HUFv07_selectDecoder(dstSize, cSrcSize);
        return algoNb ? HUFv07_decompress4X4_DCtx(dctx, dst, dstSize, cSrc, cSrcSize) :
                        HUFv07_decompress4X2_DCtx(dctx, dst, dstSize, cSrc, cSrcSize) ;
    }
}

size_t HUFv07_decompress1X_DCtx (HUFv07_DTable* dctx, void* dst, size_t dstSize, const void* cSrc, size_t cSrcSize)
{
    /* validation checks */
    if (dstSize == 0) return ERROR(dstSize_tooSmall);
    if (cSrcSize > dstSize) return ERROR(corruption_detected);   /* invalid */
    if (cSrcSize == dstSize) { memcpy(dst, cSrc, dstSize); return dstSize; }   /* not compressed */
    if (cSrcSize == 1) { memset(dst, *(const BYTE*)cSrc, dstSize); return dstSize; }   /* RLE */

    {   U32 const algoNb = HUFv07_selectDecoder(dstSize, cSrcSize);
        return algoNb ? HUFv07_decompress1X4_DCtx(dctx, dst, dstSize, cSrc, cSrcSize) :
                        HUFv07_decompress1X2_DCtx(dctx, dst, dstSize, cSrc, cSrcSize) ;
    }
}
/*
    Common functions of Zstd compression library
    Copyright (C) 2015-2016, Yann Collet.

    BSD 2-Clause License (http://www.opensource.org/licenses/bsd-license.php)

    Redistribution and use in source and binary forms, with or without
    modification, are permitted provided that the following conditions are
    met:
    * Redistributions of source code must retain the above copyright
    notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above
    copyright notice, this list of conditions and the following disclaimer
    in the documentation and/or other materials provided with the
    distribution.
    THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
    "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
    LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
    A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
    OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
    SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
    LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
    DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
    THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
    (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
    OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

    You can contact the author at :
    - zstd homepage : http://www.zstd.net/
*/



/*-****************************************
*  ZSTD Error Management
******************************************/
/*! ZSTDv07_isError() :
*   tells if a return value is an error code */
unsigned ZSTDv07_isError(size_t code) { return ERR_isError(code); }

/*! ZSTDv07_getErrorName() :
*   provides error code string from function result (useful for debugging) */
const char* ZSTDv07_getErrorName(size_t code) { return ERR_getErrorName(code); }



/* **************************************************************
*  ZBUFF Error Management
****************************************************************/
unsigned ZBUFFv07_isError(size_t errorCode) { return ERR_isError(errorCode); }

const char* ZBUFFv07_getErrorName(size_t errorCode) { return ERR_getErrorName(errorCode); }



static void* ZSTDv07_defaultAllocFunction(void* opaque, size_t size)
{
    void* address = malloc(size);
    (void)opaque;
    /* printf("alloc %p, %d opaque=%p \n", address, (int)size, opaque); */
    return address;
}

static void ZSTDv07_defaultFreeFunction(void* opaque, void* address)
{
    (void)opaque;
    /* if (address) printf("free %p opaque=%p \n", address, opaque); */
    free(address);
}
/*
    zstd_internal - common functions to include
    Header File for include
    Copyright (C) 2014-2016, Yann Collet.

    BSD 2-Clause License (http://www.opensource.org/licenses/bsd-license.php)

    Redistribution and use in source and binary forms, with or without
    modification, are permitted provided that the following conditions are
    met:
    * Redistributions of source code must retain the above copyright
    notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above
    copyright notice, this list of conditions and the following disclaimer
    in the documentation and/or other materials provided with the
    distribution.
    THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
    "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
    LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
    A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
    OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
    SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
    LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
    DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
    THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
    (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
    OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

    You can contact the author at :
    - zstd homepage : https://www.zstd.net
*/
#ifndef ZSTDv07_CCOMMON_H_MODULE
#define ZSTDv07_CCOMMON_H_MODULE


/*-*************************************
*  Common macros
***************************************/
#define MIN(a,b) ((a)<(b) ? (a) : (b))
#define MAX(a,b) ((a)>(b) ? (a) : (b))


/*-*************************************
*  Common constants
***************************************/
#define ZSTDv07_OPT_NUM    (1<<12)
#define ZSTDv07_DICT_MAGIC  0xEC30A437   /* v0.7 */

#define ZSTDv07_REP_NUM    3
#define ZSTDv07_REP_INIT   ZSTDv07_REP_NUM
#define ZSTDv07_REP_MOVE   (ZSTDv07_REP_NUM-1)
static const U32 repStartValue[ZSTDv07_REP_NUM] = { 1, 4, 8 };

#define KB *(1 <<10)
#define MB *(1 <<20)
#define GB *(1U<<30)

#define BIT7 128
#define BIT6  64
#define BIT5  32
#define BIT4  16
#define BIT1   2
#define BIT0   1

#define ZSTDv07_WINDOWLOG_ABSOLUTEMIN 10
static const size_t ZSTDv07_fcs_fieldSize[4] = { 0, 2, 4, 8 };
static const size_t ZSTDv07_did_fieldSize[4] = { 0, 1, 2, 4 };

#define ZSTDv07_BLOCKHEADERSIZE 3   /* C standard doesn't allow `static const` variable to be init using another `static const` variable */
static const size_t ZSTDv07_blockHeaderSize = ZSTDv07_BLOCKHEADERSIZE;
typedef enum { bt_compressed, bt_raw, bt_rle, bt_end } blockType_t;

#define MIN_SEQUENCES_SIZE 1 /* nbSeq==0 */
#define MIN_CBLOCK_SIZE (1 /*litCSize*/ + 1 /* RLE or RAW */ + MIN_SEQUENCES_SIZE /* nbSeq==0 */)   /* for a non-null block */

#define HufLog 12
typedef enum { lbt_huffman, lbt_repeat, lbt_raw, lbt_rle } litBlockType_t;

#define LONGNBSEQ 0x7F00

#define MINMATCH 3
#define EQUAL_READ32 4

#define Litbits  8
#define MaxLit ((1<<Litbits) - 1)
#define MaxML  52
#define MaxLL  35
#define MaxOff 28
#define MaxSeq MAX(MaxLL, MaxML)   /* Assumption : MaxOff < MaxLL,MaxML */
#define MLFSELog    9
#define LLFSELog    9
#define OffFSELog   8

#define FSEv07_ENCODING_RAW     0
#define FSEv07_ENCODING_RLE     1
#define FSEv07_ENCODING_STATIC  2
#define FSEv07_ENCODING_DYNAMIC 3

static const U32 LL_bits[MaxLL+1] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                                      1, 1, 1, 1, 2, 2, 3, 3, 4, 6, 7, 8, 9,10,11,12,
                                     13,14,15,16 };
static const S16 LL_defaultNorm[MaxLL+1] = { 4, 3, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 1, 1, 1,
                                             2, 2, 2, 2, 2, 2, 2, 2, 2, 3, 2, 1, 1, 1, 1, 1,
                                            -1,-1,-1,-1 };
static const U32 LL_defaultNormLog = 6;

static const U32 ML_bits[MaxML+1] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                                      0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                                      1, 1, 1, 1, 2, 2, 3, 3, 4, 4, 5, 7, 8, 9,10,11,
                                     12,13,14,15,16 };
static const S16 ML_defaultNorm[MaxML+1] = { 1, 4, 3, 2, 2, 2, 2, 2, 2, 1, 1, 1, 1, 1, 1, 1,
                                             1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
                                             1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,-1,-1,
                                            -1,-1,-1,-1,-1 };
static const U32 ML_defaultNormLog = 6;

static const S16 OF_defaultNorm[MaxOff+1] = { 1, 1, 1, 1, 1, 1, 2, 2, 2, 1, 1, 1, 1, 1, 1, 1,
                                              1, 1, 1, 1, 1, 1, 1, 1,-1,-1,-1,-1,-1 };
static const U32 OF_defaultNormLog = 5;


/*-*******************************************
*  Shared functions to include for inlining
*********************************************/
static void ZSTDv07_copy8(void* dst, const void* src) { memcpy(dst, src, 8); }
#define COPY8(d,s) { ZSTDv07_copy8(d,s); d+=8; s+=8; }

/*! ZSTDv07_wildcopy() :
*   custom version of memcpy(), can copy up to 7 bytes too many (8 bytes if length==0) */
#define WILDCOPY_OVERLENGTH 8
MEM_STATIC void ZSTDv07_wildcopy(void* dst, const void* src, ptrdiff_t length)
{
    const BYTE* ip = (const BYTE*)src;
    BYTE* op = (BYTE*)dst;
    BYTE* const oend = op + length;
    do
        COPY8(op, ip)
    while (op < oend);
}


/*-*******************************************
*  Private interfaces
*********************************************/
typedef struct ZSTDv07_stats_s ZSTDv07_stats_t;

typedef struct {
    U32 off;
    U32 len;
} ZSTDv07_match_t;

typedef struct {
    U32 price;
    U32 off;
    U32 mlen;
    U32 litlen;
    U32 rep[ZSTDv07_REP_INIT];
} ZSTDv07_optimal_t;

struct ZSTDv07_stats_s { U32 unused; };

typedef struct {
    void* buffer;
    U32*  offsetStart;
    U32*  offset;
    BYTE* offCodeStart;
    BYTE* litStart;
    BYTE* lit;
    U16*  litLengthStart;
    U16*  litLength;
    BYTE* llCodeStart;
    U16*  matchLengthStart;
    U16*  matchLength;
    BYTE* mlCodeStart;
    U32   longLengthID;   /* 0 == no longLength; 1 == Lit.longLength; 2 == Match.longLength; */
    U32   longLengthPos;
    /* opt */
    ZSTDv07_optimal_t* priceTable;
    ZSTDv07_match_t* matchTable;
    U32* matchLengthFreq;
    U32* litLengthFreq;
    U32* litFreq;
    U32* offCodeFreq;
    U32  matchLengthSum;
    U32  matchSum;
    U32  litLengthSum;
    U32  litSum;
    U32  offCodeSum;
    U32  log2matchLengthSum;
    U32  log2matchSum;
    U32  log2litLengthSum;
    U32  log2litSum;
    U32  log2offCodeSum;
    U32  factor;
    U32  cachedPrice;
    U32  cachedLitLength;
    const BYTE* cachedLiterals;
    ZSTDv07_stats_t stats;
} seqStore_t;

void ZSTDv07_seqToCodes(const seqStore_t* seqStorePtr, size_t const nbSeq);

/* custom memory allocation functions */
static const ZSTDv07_customMem defaultCustomMem = { ZSTDv07_defaultAllocFunction, ZSTDv07_defaultFreeFunction, NULL };

#endif   /* ZSTDv07_CCOMMON_H_MODULE */
/*
    zstd - standard compression library
    Copyright (C) 2014-2016, Yann Collet.

    BSD 2-Clause License (http://www.opensource.org/licenses/bsd-license.php)

    Redistribution and use in source and binary forms, with or without
    modification, are permitted provided that the following conditions are
    met:
    * Redistributions of source code must retain the above copyright
    notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above
    copyright notice, this list of conditions and the following disclaimer
    in the documentation and/or other materials provided with the
    distribution.
    THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
    "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
    LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
    A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
    OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
    SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
    LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
    DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
    THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
    (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
    OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

    You can contact the author at :
    - zstd homepage : http://www.zstd.net
*/

/* ***************************************************************
*  Tuning parameters
*****************************************************************/
/*!
 * HEAPMODE :
 * Select how default decompression function ZSTDv07_decompress() will allocate memory,
 * in memory stack (0), or in memory heap (1, requires malloc())
 */
#ifndef ZSTDv07_HEAPMODE
#  define ZSTDv07_HEAPMODE 1
#endif


/*-*******************************************************
*  Compiler specifics
*********************************************************/
#ifdef _MSC_VER    /* Visual Studio */
#  include <intrin.h>                    /* For Visual 2005 */
#  pragma warning(disable : 4127)        /* disable: C4127: conditional expression is constant */
#  pragma warning(disable : 4324)        /* disable: C4324: padded structure */
#  pragma warning(disable : 4100)        /* disable: C4100: unreferenced formal parameter */
#endif


/*-*************************************
*  Macros
***************************************/
#define ZSTDv07_isError ERR_isError   /* for inlining */
#define FSEv07_isError  ERR_isError
#define HUFv07_isError  ERR_isError


/*_*******************************************************
*  Memory operations
**********************************************************/
static void ZSTDv07_copy4(void* dst, const void* src) { memcpy(dst, src, 4); }


/*-*************************************************************
*   Context management
***************************************************************/
typedef enum { ZSTDds_getFrameHeaderSize, ZSTDds_decodeFrameHeader,
               ZSTDds_decodeBlockHeader, ZSTDds_decompressBlock,
               ZSTDds_decodeSkippableHeader, ZSTDds_skipFrame } ZSTDv07_dStage;

struct ZSTDv07_DCtx_s
{
    FSEv07_DTable LLTable[FSEv07_DTABLE_SIZE_U32(LLFSELog)];
    FSEv07_DTable OffTable[FSEv07_DTABLE_SIZE_U32(OffFSELog)];
    FSEv07_DTable MLTable[FSEv07_DTABLE_SIZE_U32(MLFSELog)];
    HUFv07_DTable hufTable[HUFv07_DTABLE_SIZE(HufLog)];  /* can accommodate HUFv07_decompress4X */
    const void* previousDstEnd;
    const void* base;
    const void* vBase;
    const void* dictEnd;
    size_t expected;
    U32 rep[3];
    ZSTDv07_frameParams fParams;
    blockType_t bType;   /* used in ZSTDv07_decompressContinue(), to transfer blockType between header decoding and block decoding stages */
    ZSTDv07_dStage stage;
    U32 litEntropy;
    U32 fseEntropy;
    XXH64_state_t xxhState;
    size_t headerSize;
    U32 dictID;
    const BYTE* litPtr;
    ZSTDv07_customMem customMem;
    size_t litSize;
    BYTE litBuffer[ZSTDv07_BLOCKSIZE_ABSOLUTEMAX + WILDCOPY_OVERLENGTH];
    BYTE headerBuffer[ZSTDv07_FRAMEHEADERSIZE_MAX];
};  /* typedef'd to ZSTDv07_DCtx within "zstd_static.h" */

int ZSTDv07_isSkipFrame(ZSTDv07_DCtx* dctx);

size_t ZSTDv07_sizeofDCtx (const ZSTDv07_DCtx* dctx) { return sizeof(*dctx); }

size_t ZSTDv07_estimateDCtxSize(void) { return sizeof(ZSTDv07_DCtx); }

size_t ZSTDv07_decompressBegin(ZSTDv07_DCtx* dctx)
{
    dctx->expected = ZSTDv07_frameHeaderSize_min;
    dctx->stage = ZSTDds_getFrameHeaderSize;
    dctx->previousDstEnd = NULL;
    dctx->base = NULL;
    dctx->vBase = NULL;
    dctx->dictEnd = NULL;
    dctx->hufTable[0] = (HUFv07_DTable)((HufLog)*0x1000001);
    dctx->litEntropy = dctx->fseEntropy = 0;
    dctx->dictID = 0;
    { int i; for (i=0; i<ZSTDv07_REP_NUM; i++) dctx->rep[i] = repStartValue[i]; }
    return 0;
}

ZSTDv07_DCtx* ZSTDv07_createDCtx_advanced(ZSTDv07_customMem customMem)
{
    ZSTDv07_DCtx* dctx;

    if (!customMem.customAlloc && !customMem.customFree)
        customMem = defaultCustomMem;

    if (!customMem.customAlloc || !customMem.customFree)
        return NULL;

    dctx = (ZSTDv07_DCtx*) customMem.customAlloc(customMem.opaque, sizeof(ZSTDv07_DCtx));
    if (!dctx) return NULL;
    memcpy(&dctx->customMem, &customMem, sizeof(ZSTDv07_customMem));
    ZSTDv07_decompressBegin(dctx);
    return dctx;
}

ZSTDv07_DCtx* ZSTDv07_createDCtx(void)
{
    return ZSTDv07_createDCtx_advanced(defaultCustomMem);
}

size_t ZSTDv07_freeDCtx(ZSTDv07_DCtx* dctx)
{
    if (dctx==NULL) return 0;   /* support free on NULL */
    dctx->customMem.customFree(dctx->customMem.opaque, dctx);
    return 0;   /* reserved as a potential error code in the future */
}

void ZSTDv07_copyDCtx(ZSTDv07_DCtx* dstDCtx, const ZSTDv07_DCtx* srcDCtx)
{
    memcpy(dstDCtx, srcDCtx,
           sizeof(ZSTDv07_DCtx) - (ZSTDv07_BLOCKSIZE_ABSOLUTEMAX+WILDCOPY_OVERLENGTH + ZSTDv07_frameHeaderSize_max));  /* no need to copy workspace */
}


/*-*************************************************************
*   Decompression section
***************************************************************/

/* Frame format description
   Frame Header -  [ Block Header - Block ] - Frame End
   1) Frame Header
      - 4 bytes - Magic Number : ZSTDv07_MAGICNUMBER (defined within zstd.h)
      - 1 byte  - Frame Descriptor
   2) Block Header
      - 3 bytes, starting with a 2-bits descriptor
                 Uncompressed, Compressed, Frame End, unused
   3) Block
      See Block Format Description
   4) Frame End
      - 3 bytes, compatible with Block Header
*/


/* Frame Header :

   1 byte - FrameHeaderDescription :
   bit 0-1 : dictID (0, 1, 2 or 4 bytes)
   bit 2   : checksumFlag
   bit 3   : reserved (must be zero)
   bit 4   : reserved (unused, can be any value)
   bit 5   : Single Segment (if 1, WindowLog byte is not present)
   bit 6-7 : FrameContentFieldSize (0, 2, 4, or 8)
             if (SkippedWindowLog && !FrameContentFieldsize) FrameContentFieldsize=1;

   Optional : WindowLog (0 or 1 byte)
   bit 0-2 : octal Fractional (1/8th)
   bit 3-7 : Power of 2, with 0 = 1 KB (up to 2 TB)

   Optional : dictID (0, 1, 2 or 4 bytes)
   Automatic adaptation
   0 : no dictID
   1 : 1 - 255
   2 : 256 - 65535
   4 : all other values

   Optional : content size (0, 1, 2, 4 or 8 bytes)
   0 : unknown          (fcfs==0 and swl==0)
   1 : 0-255 bytes      (fcfs==0 and swl==1)
   2 : 256 - 65535+256  (fcfs==1)
   4 : 0 - 4GB-1        (fcfs==2)
   8 : 0 - 16EB-1       (fcfs==3)
*/


/* Compressed Block, format description

   Block = Literal Section - Sequences Section
   Prerequisite : size of (compressed) block, maximum size of regenerated data

   1) Literal Section

   1.1) Header : 1-5 bytes
        flags: 2 bits
            00 compressed by Huff0
            01 unused
            10 is Raw (uncompressed)
            11 is Rle
            Note : using 01 => Huff0 with precomputed table ?
            Note : delta map ? => compressed ?

   1.1.1) Huff0-compressed literal block : 3-5 bytes
            srcSize < 1 KB => 3 bytes (2-2-10-10) => single stream
            srcSize < 1 KB => 3 bytes (2-2-10-10)
            srcSize < 16KB => 4 bytes (2-2-14-14)
            else           => 5 bytes (2-2-18-18)
            big endian convention

   1.1.2) Raw (uncompressed) literal block header : 1-3 bytes
        size :  5 bits: (IS_RAW<<6) + (0<<4) + size
               12 bits: (IS_RAW<<6) + (2<<4) + (size>>8)
                        size&255
               20 bits: (IS_RAW<<6) + (3<<4) + (size>>16)
                        size>>8&255
                        size&255

   1.1.3) Rle (repeated single byte) literal block header : 1-3 bytes
        size :  5 bits: (IS_RLE<<6) + (0<<4) + size
               12 bits: (IS_RLE<<6) + (2<<4) + (size>>8)
                        size&255
               20 bits: (IS_RLE<<6) + (3<<4) + (size>>16)
                        size>>8&255
                        size&255

   1.1.4) Huff0-compressed literal block, using precomputed CTables : 3-5 bytes
            srcSize < 1 KB => 3 bytes (2-2-10-10) => single stream
            srcSize < 1 KB => 3 bytes (2-2-10-10)
            srcSize < 16KB => 4 bytes (2-2-14-14)
            else           => 5 bytes (2-2-18-18)
            big endian convention

        1- CTable available (stored into workspace ?)
        2- Small input (fast heuristic ? Full comparison ? depend on clevel ?)


   1.2) Literal block content

   1.2.1) Huff0 block, using sizes from header
        See Huff0 format

   1.2.2) Huff0 block, using prepared table

   1.2.3) Raw content

   1.2.4) single byte


   2) Sequences section
      TO DO
*/

/** ZSTDv07_frameHeaderSize() :
*   srcSize must be >= ZSTDv07_frameHeaderSize_min.
*   @return : size of the Frame Header */
static size_t ZSTDv07_frameHeaderSize(const void* src, size_t srcSize)
{
    if (srcSize < ZSTDv07_frameHeaderSize_min) return ERROR(srcSize_wrong);
    {   BYTE const fhd = ((const BYTE*)src)[4];
        U32 const dictID= fhd & 3;
        U32 const directMode = (fhd >> 5) & 1;
        U32 const fcsId = fhd >> 6;
        return ZSTDv07_frameHeaderSize_min + !directMode + ZSTDv07_did_fieldSize[dictID] + ZSTDv07_fcs_fieldSize[fcsId]
                + (directMode && !ZSTDv07_fcs_fieldSize[fcsId]);
    }
}


/** ZSTDv07_getFrameParams() :
*   decode Frame Header, or require larger `srcSize`.
*   @return : 0, `fparamsPtr` is correctly filled,
*            >0, `srcSize` is too small, result is expected `srcSize`,
*             or an error code, which can be tested using ZSTDv07_isError() */
size_t ZSTDv07_getFrameParams(ZSTDv07_frameParams* fparamsPtr, const void* src, size_t srcSize)
{
    const BYTE* ip = (const BYTE*)src;

    if (srcSize < ZSTDv07_frameHeaderSize_min) return ZSTDv07_frameHeaderSize_min;
    memset(fparamsPtr, 0, sizeof(*fparamsPtr));
    if (MEM_readLE32(src) != ZSTDv07_MAGICNUMBER) {
        if ((MEM_readLE32(src) & 0xFFFFFFF0U) == ZSTDv07_MAGIC_SKIPPABLE_START) {
            if (srcSize < ZSTDv07_skippableHeaderSize) return ZSTDv07_skippableHeaderSize; /* magic number + skippable frame length */
            fparamsPtr->frameContentSize = MEM_readLE32((const char *)src + 4);
            fparamsPtr->windowSize = 0; /* windowSize==0 means a frame is skippable */
            return 0;
        }
        return ERROR(prefix_unknown);
    }

    /* ensure there is enough `srcSize` to fully read/decode frame header */
    { size_t const fhsize = ZSTDv07_frameHeaderSize(src, srcSize);
      if (srcSize < fhsize) return fhsize; }

    {   BYTE const fhdByte = ip[4];
        size_t pos = 5;
        U32 const dictIDSizeCode = fhdByte&3;
        U32 const checksumFlag = (fhdByte>>2)&1;
        U32 const directMode = (fhdByte>>5)&1;
        U32 const fcsID = fhdByte>>6;
        U32 const windowSizeMax = 1U << ZSTDv07_WINDOWLOG_MAX;
        U32 windowSize = 0;
        U32 dictID = 0;
        U64 frameContentSize = 0;
        if ((fhdByte & 0x08) != 0)   /* reserved bits, which must be zero */
            return ERROR(frameParameter_unsupported);
        if (!directMode) {
            BYTE const wlByte = ip[pos++];
            U32 const windowLog = (wlByte >> 3) + ZSTDv07_WINDOWLOG_ABSOLUTEMIN;
            if (windowLog > ZSTDv07_WINDOWLOG_MAX)
                return ERROR(frameParameter_unsupported);
            windowSize = (1U << windowLog);
            windowSize += (windowSize >> 3) * (wlByte&7);
        }

        switch(dictIDSizeCode)
        {
            default:   /* impossible */
            case 0 : break;
            case 1 : dictID = ip[pos]; pos++; break;
            case 2 : dictID = MEM_readLE16(ip+pos); pos+=2; break;
            case 3 : dictID = MEM_readLE32(ip+pos); pos+=4; break;
        }
        switch(fcsID)
        {
            default:   /* impossible */
            case 0 : if (directMode) frameContentSize = ip[pos]; break;
            case 1 : frameContentSize = MEM_readLE16(ip+pos)+256; break;
            case 2 : frameContentSize = MEM_readLE32(ip+pos); break;
            case 3 : frameContentSize = MEM_readLE64(ip+pos); break;
        }
        if (!windowSize) windowSize = (U32)frameContentSize;
        if (windowSize > windowSizeMax)
            return ERROR(frameParameter_unsupported);
        fparamsPtr->frameContentSize = frameContentSize;
        fparamsPtr->windowSize = windowSize;
        fparamsPtr->dictID = dictID;
        fparamsPtr->checksumFlag = checksumFlag;
    }
    return 0;
}


/** ZSTDv07_getDecompressedSize() :
*   compatible with legacy mode
*   @return : decompressed size if known, 0 otherwise
              note : 0 can mean any of the following :
                   - decompressed size is not provided within frame header
                   - frame header unknown / not supported
                   - frame header not completely provided (`srcSize` too small) */
unsigned long long ZSTDv07_getDecompressedSize(const void* src, size_t srcSize)
{
    ZSTDv07_frameParams fparams;
    size_t const frResult = ZSTDv07_getFrameParams(&fparams, src, srcSize);
    if (frResult!=0) return 0;
    return fparams.frameContentSize;
}


/** ZSTDv07_decodeFrameHeader() :
*   `srcSize` must be the size provided by ZSTDv07_frameHeaderSize().
*   @return : 0 if success, or an error code, which can be tested using ZSTDv07_isError() */
static size_t ZSTDv07_decodeFrameHeader(ZSTDv07_DCtx* dctx, const void* src, size_t srcSize)
{
    size_t const result = ZSTDv07_getFrameParams(&(dctx->fParams), src, srcSize);
    if (dctx->fParams.dictID && (dctx->dictID != dctx->fParams.dictID)) return ERROR(dictionary_wrong);
    if (dctx->fParams.checksumFlag) XXH64_reset(&dctx->xxhState, 0);
    return result;
}


typedef struct
{
    blockType_t blockType;
    U32 origSize;
} blockProperties_t;

/*! ZSTDv07_getcBlockSize() :
*   Provides the size of compressed block from block header `src` */
static size_t ZSTDv07_getcBlockSize(const void* src, size_t srcSize, blockProperties_t* bpPtr)
{
    const BYTE* const in = (const BYTE* const)src;
    U32 cSize;

    if (srcSize < ZSTDv07_blockHeaderSize) return ERROR(srcSize_wrong);

    bpPtr->blockType = (blockType_t)((*in) >> 6);
    cSize = in[2] + (in[1]<<8) + ((in[0] & 7)<<16);
    bpPtr->origSize = (bpPtr->blockType == bt_rle) ? cSize : 0;

    if (bpPtr->blockType == bt_end) return 0;
    if (bpPtr->blockType == bt_rle) return 1;
    return cSize;
}


static size_t ZSTDv07_copyRawBlock(void* dst, size_t dstCapacity, const void* src, size_t srcSize)
{
    if (srcSize > dstCapacity) return ERROR(dstSize_tooSmall);
    memcpy(dst, src, srcSize);
    return srcSize;
}


/*! ZSTDv07_decodeLiteralsBlock() :
    @return : nb of bytes read from src (< srcSize ) */
static size_t ZSTDv07_decodeLiteralsBlock(ZSTDv07_DCtx* dctx,
                          const void* src, size_t srcSize)   /* note : srcSize < BLOCKSIZE */
{
    const BYTE* const istart = (const BYTE*) src;

    if (srcSize < MIN_CBLOCK_SIZE) return ERROR(corruption_detected);

    switch((litBlockType_t)(istart[0]>> 6))
    {
    case lbt_huffman:
        {   size_t litSize, litCSize, singleStream=0;
            U32 lhSize = (istart[0] >> 4) & 3;
            if (srcSize < 5) return ERROR(corruption_detected);   /* srcSize >= MIN_CBLOCK_SIZE == 3; here we need up to 5 for lhSize, + cSize (+nbSeq) */
            switch(lhSize)
            {
            case 0: case 1: default:   /* note : default is impossible, since lhSize into [0..3] */
                /* 2 - 2 - 10 - 10 */
                lhSize=3;
                singleStream = istart[0] & 16;
                litSize  = ((istart[0] & 15) << 6) + (istart[1] >> 2);
                litCSize = ((istart[1] &  3) << 8) + istart[2];
                break;
            case 2:
                /* 2 - 2 - 14 - 14 */
                lhSize=4;
                litSize  = ((istart[0] & 15) << 10) + (istart[1] << 2) + (istart[2] >> 6);
                litCSize = ((istart[2] & 63) <<  8) + istart[3];
                break;
            case 3:
                /* 2 - 2 - 18 - 18 */
                lhSize=5;
                litSize  = ((istart[0] & 15) << 14) + (istart[1] << 6) + (istart[2] >> 2);
                litCSize = ((istart[2] &  3) << 16) + (istart[3] << 8) + istart[4];
                break;
            }
            if (litSize > ZSTDv07_BLOCKSIZE_ABSOLUTEMAX) return ERROR(corruption_detected);
            if (litCSize + lhSize > srcSize) return ERROR(corruption_detected);

            if (HUFv07_isError(singleStream ?
                            HUFv07_decompress1X2_DCtx(dctx->hufTable, dctx->litBuffer, litSize, istart+lhSize, litCSize) :
                            HUFv07_decompress4X_hufOnly (dctx->hufTable, dctx->litBuffer, litSize, istart+lhSize, litCSize) ))
                return ERROR(corruption_detected);

            dctx->litPtr = dctx->litBuffer;
            dctx->litSize = litSize;
            dctx->litEntropy = 1;
            memset(dctx->litBuffer + dctx->litSize, 0, WILDCOPY_OVERLENGTH);
            return litCSize + lhSize;
        }
    case lbt_repeat:
        {   size_t litSize, litCSize;
            U32 lhSize = ((istart[0]) >> 4) & 3;
            if (lhSize != 1)  /* only case supported for now : small litSize, single stream */
                return ERROR(corruption_detected);
            if (dctx->litEntropy==0)
                return ERROR(dictionary_corrupted);

            /* 2 - 2 - 10 - 10 */
            lhSize=3;
            litSize  = ((istart[0] & 15) << 6) + (istart[1] >> 2);
            litCSize = ((istart[1] &  3) << 8) + istart[2];
            if (litCSize + lhSize > srcSize) return ERROR(corruption_detected);

            {   size_t const errorCode = HUFv07_decompress1X4_usingDTable(dctx->litBuffer, litSize, istart+lhSize, litCSize, dctx->hufTable);
                if (HUFv07_isError(errorCode)) return ERROR(corruption_detected);
            }
            dctx->litPtr = dctx->litBuffer;
            dctx->litSize = litSize;
            memset(dctx->litBuffer + dctx->litSize, 0, WILDCOPY_OVERLENGTH);
            return litCSize + lhSize;
        }
    case lbt_raw:
        {   size_t litSize;
            U32 lhSize = ((istart[0]) >> 4) & 3;
            switch(lhSize)
            {
            case 0: case 1: default:   /* note : default is impossible, since lhSize into [0..3] */
                lhSize=1;
                litSize = istart[0] & 31;
                break;
            case 2:
                litSize = ((istart[0] & 15) << 8) + istart[1];
                break;
            case 3:
                litSize = ((istart[0] & 15) << 16) + (istart[1] << 8) + istart[2];
                break;
            }

            if (lhSize+litSize+WILDCOPY_OVERLENGTH > srcSize) {  /* risk reading beyond src buffer with wildcopy */
                if (litSize+lhSize > srcSize) return ERROR(corruption_detected);
                memcpy(dctx->litBuffer, istart+lhSize, litSize);
                dctx->litPtr = dctx->litBuffer;
                dctx->litSize = litSize;
                memset(dctx->litBuffer + dctx->litSize, 0, WILDCOPY_OVERLENGTH);
                return lhSize+litSize;
            }
            /* direct reference into compressed stream */
            dctx->litPtr = istart+lhSize;
            dctx->litSize = litSize;
            return lhSize+litSize;
        }
    case lbt_rle:
        {   size_t litSize;
            U32 lhSize = ((istart[0]) >> 4) & 3;
            switch(lhSize)
            {
            case 0: case 1: default:   /* note : default is impossible, since lhSize into [0..3] */
                lhSize = 1;
                litSize = istart[0] & 31;
                break;
            case 2:
                litSize = ((istart[0] & 15) << 8) + istart[1];
                break;
            case 3:
                litSize = ((istart[0] & 15) << 16) + (istart[1] << 8) + istart[2];
                if (srcSize<4) return ERROR(corruption_detected);   /* srcSize >= MIN_CBLOCK_SIZE == 3; here we need lhSize+1 = 4 */
                break;
            }
            if (litSize > ZSTDv07_BLOCKSIZE_ABSOLUTEMAX) return ERROR(corruption_detected);
            memset(dctx->litBuffer, istart[lhSize], litSize + WILDCOPY_OVERLENGTH);
            dctx->litPtr = dctx->litBuffer;
            dctx->litSize = litSize;
            return lhSize+1;
        }
    default:
        return ERROR(corruption_detected);   /* impossible */
    }
}


/*! ZSTDv07_buildSeqTable() :
    @return : nb bytes read from src,
              or an error code if it fails, testable with ZSTDv07_isError()
*/
static size_t ZSTDv07_buildSeqTable(FSEv07_DTable* DTable, U32 type, U32 max, U32 maxLog,
                                 const void* src, size_t srcSize,
                                 const S16* defaultNorm, U32 defaultLog, U32 flagRepeatTable)
{
    switch(type)
    {
    case FSEv07_ENCODING_RLE :
        if (!srcSize) return ERROR(srcSize_wrong);
        if ( (*(const BYTE*)src) > max) return ERROR(corruption_detected);
        FSEv07_buildDTable_rle(DTable, *(const BYTE*)src);   /* if *src > max, data is corrupted */
        return 1;
    case FSEv07_ENCODING_RAW :
        FSEv07_buildDTable(DTable, defaultNorm, max, defaultLog);
        return 0;
    case FSEv07_ENCODING_STATIC:
        if (!flagRepeatTable) return ERROR(corruption_detected);
        return 0;
    default :   /* impossible */
    case FSEv07_ENCODING_DYNAMIC :
        {   U32 tableLog;
            S16 norm[MaxSeq+1];
            size_t const headerSize = FSEv07_readNCount(norm, &max, &tableLog, src, srcSize);
            if (FSEv07_isError(headerSize)) return ERROR(corruption_detected);
            if (tableLog > maxLog) return ERROR(corruption_detected);
            FSEv07_buildDTable(DTable, norm, max, tableLog);
            return headerSize;
    }   }
}


static size_t ZSTDv07_decodeSeqHeaders(int* nbSeqPtr,
                             FSEv07_DTable* DTableLL, FSEv07_DTable* DTableML, FSEv07_DTable* DTableOffb, U32 flagRepeatTable,
                             const void* src, size_t srcSize)
{
    const BYTE* const istart = (const BYTE* const)src;
    const BYTE* const iend = istart + srcSize;
    const BYTE* ip = istart;

    /* check */
    if (srcSize < MIN_SEQUENCES_SIZE) return ERROR(srcSize_wrong);

    /* SeqHead */
    {   int nbSeq = *ip++;
        if (!nbSeq) { *nbSeqPtr=0; return 1; }
        if (nbSeq > 0x7F) {
            if (nbSeq == 0xFF) {
                if (ip+2 > iend) return ERROR(srcSize_wrong);
                nbSeq = MEM_readLE16(ip) + LONGNBSEQ, ip+=2;
            } else {
                if (ip >= iend) return ERROR(srcSize_wrong);
                nbSeq = ((nbSeq-0x80)<<8) + *ip++;
            }
        }
        *nbSeqPtr = nbSeq;
    }

    /* FSE table descriptors */
    {   U32 const LLtype  = *ip >> 6;
        U32 const OFtype = (*ip >> 4) & 3;
        U32 const MLtype  = (*ip >> 2) & 3;
        ip++;

        /* check */
        if (ip > iend-3) return ERROR(srcSize_wrong); /* min : all 3 are "raw", hence no header, but at least xxLog bits per type */

        /* Build DTables */
        {   size_t const llhSize = ZSTDv07_buildSeqTable(DTableLL, LLtype, MaxLL, LLFSELog, ip, iend-ip, LL_defaultNorm, LL_defaultNormLog, flagRepeatTable);
            if (ZSTDv07_isError(llhSize)) return ERROR(corruption_detected);
            ip += llhSize;
        }
        {   size_t const ofhSize = ZSTDv07_buildSeqTable(DTableOffb, OFtype, MaxOff, OffFSELog, ip, iend-ip, OF_defaultNorm, OF_defaultNormLog, flagRepeatTable);
            if (ZSTDv07_isError(ofhSize)) return ERROR(corruption_detected);
            ip += ofhSize;
        }
        {   size_t const mlhSize = ZSTDv07_buildSeqTable(DTableML, MLtype, MaxML, MLFSELog, ip, iend-ip, ML_defaultNorm, ML_defaultNormLog, flagRepeatTable);
            if (ZSTDv07_isError(mlhSize)) return ERROR(corruption_detected);
            ip += mlhSize;
    }   }

    return ip-istart;
}


typedef struct {
    size_t litLength;
    size_t matchLength;
    size_t offset;
} seq_t;

typedef struct {
    BITv07_DStream_t DStream;
    FSEv07_DState_t stateLL;
    FSEv07_DState_t stateOffb;
    FSEv07_DState_t stateML;
    size_t prevOffset[ZSTDv07_REP_INIT];
} seqState_t;


static seq_t ZSTDv07_decodeSequence(seqState_t* seqState)
{
    seq_t seq;

    U32 const llCode = FSEv07_peekSymbol(&(seqState->stateLL));
    U32 const mlCode = FSEv07_peekSymbol(&(seqState->stateML));
    U32 const ofCode = FSEv07_peekSymbol(&(seqState->stateOffb));   /* <= maxOff, by table construction */

    U32 const llBits = LL_bits[llCode];
    U32 const mlBits = ML_bits[mlCode];
    U32 const ofBits = ofCode;
    U32 const totalBits = llBits+mlBits+ofBits;

    static const U32 LL_base[MaxLL+1] = {
                             0,  1,  2,  3,  4,  5,  6,  7,  8,  9,   10,    11,    12,    13,    14,     15,
                            16, 18, 20, 22, 24, 28, 32, 40, 48, 64, 0x80, 0x100, 0x200, 0x400, 0x800, 0x1000,
                            0x2000, 0x4000, 0x8000, 0x10000 };

    static const U32 ML_base[MaxML+1] = {
                             3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13,   14,    15,    16,    17,    18,
                            19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29,   30,    31,    32,    33,    34,
                            35, 37, 39, 41, 43, 47, 51, 59, 67, 83, 99, 0x83, 0x103, 0x203, 0x403, 0x803,
                            0x1003, 0x2003, 0x4003, 0x8003, 0x10003 };

    static const U32 OF_base[MaxOff+1] = {
                 0,        1,       1,       5,     0xD,     0x1D,     0x3D,     0x7D,
                 0xFD,   0x1FD,   0x3FD,   0x7FD,   0xFFD,   0x1FFD,   0x3FFD,   0x7FFD,
                 0xFFFD, 0x1FFFD, 0x3FFFD, 0x7FFFD, 0xFFFFD, 0x1FFFFD, 0x3FFFFD, 0x7FFFFD,
                 0xFFFFFD, 0x1FFFFFD, 0x3FFFFFD, 0x7FFFFFD, 0xFFFFFFD };

    /* sequence */
    {   size_t offset;
        if (!ofCode)
            offset = 0;
        else {
            offset = OF_base[ofCode] + BITv07_readBits(&(seqState->DStream), ofBits);   /* <=  (ZSTDv07_WINDOWLOG_MAX-1) bits */
            if (MEM_32bits()) BITv07_reloadDStream(&(seqState->DStream));
        }

        if (ofCode <= 1) {
            if ((llCode == 0) & (offset <= 1)) offset = 1-offset;
            if (offset) {
                size_t const temp = seqState->prevOffset[offset];
                if (offset != 1) seqState->prevOffset[2] = seqState->prevOffset[1];
                seqState->prevOffset[1] = seqState->prevOffset[0];
                seqState->prevOffset[0] = offset = temp;
            } else {
                offset = seqState->prevOffset[0];
            }
        } else {
            seqState->prevOffset[2] = seqState->prevOffset[1];
            seqState->prevOffset[1] = seqState->prevOffset[0];
            seqState->prevOffset[0] = offset;
        }
        seq.offset = offset;
    }

    seq.matchLength = ML_base[mlCode] + ((mlCode>31) ? BITv07_readBits(&(seqState->DStream), mlBits) : 0);   /* <=  16 bits */
    if (MEM_32bits() && (mlBits+llBits>24)) BITv07_reloadDStream(&(seqState->DStream));

    seq.litLength = LL_base[llCode] + ((llCode>15) ? BITv07_readBits(&(seqState->DStream), llBits) : 0);   /* <=  16 bits */
    if (MEM_32bits() ||
       (totalBits > 64 - 7 - (LLFSELog+MLFSELog+OffFSELog)) ) BITv07_reloadDStream(&(seqState->DStream));

    /* ANS state update */
    FSEv07_updateState(&(seqState->stateLL), &(seqState->DStream));   /* <=  9 bits */
    FSEv07_updateState(&(seqState->stateML), &(seqState->DStream));   /* <=  9 bits */
    if (MEM_32bits()) BITv07_reloadDStream(&(seqState->DStream));     /* <= 18 bits */
    FSEv07_updateState(&(seqState->stateOffb), &(seqState->DStream)); /* <=  8 bits */

    return seq;
}


static
size_t ZSTDv07_execSequence(BYTE* op,
                                BYTE* const oend, seq_t sequence,
                                const BYTE** litPtr, const BYTE* const litLimit,
                                const BYTE* const base, const BYTE* const vBase, const BYTE* const dictEnd)
{
    BYTE* const oLitEnd = op + sequence.litLength;
    size_t const sequenceLength = sequence.litLength + sequence.matchLength;
    BYTE* const oMatchEnd = op + sequenceLength;   /* risk : address space overflow (32-bits) */
    BYTE* const oend_w = oend-WILDCOPY_OVERLENGTH;
    const BYTE* const iLitEnd = *litPtr + sequence.litLength;
    const BYTE* match = oLitEnd - sequence.offset;

    /* check */
    if ((oLitEnd>oend_w) | (oMatchEnd>oend)) return ERROR(dstSize_tooSmall); /* last match must start at a minimum distance of WILDCOPY_OVERLENGTH from oend */
    if (iLitEnd > litLimit) return ERROR(corruption_detected);   /* over-read beyond lit buffer */

    /* copy Literals */
    ZSTDv07_wildcopy(op, *litPtr, sequence.litLength);   /* note : since oLitEnd <= oend-WILDCOPY_OVERLENGTH, no risk of overwrite beyond oend */
    op = oLitEnd;
    *litPtr = iLitEnd;   /* update for next sequence */

    /* copy Match */
    if (sequence.offset > (size_t)(oLitEnd - base)) {
        /* offset beyond prefix */
        if (sequence.offset > (size_t)(oLitEnd - vBase)) return ERROR(corruption_detected);
        match = dictEnd - (base-match);
        if (match + sequence.matchLength <= dictEnd) {
            memmove(oLitEnd, match, sequence.matchLength);
            return sequenceLength;
        }
        /* span extDict & currentPrefixSegment */
        {   size_t const length1 = dictEnd - match;
            memmove(oLitEnd, match, length1);
            op = oLitEnd + length1;
            sequence.matchLength -= length1;
            match = base;
            if (op > oend_w || sequence.matchLength < MINMATCH) {
              while (op < oMatchEnd) *op++ = *match++;
              return sequenceLength;
            }
    }   }
    /* Requirement: op <= oend_w */

    /* match within prefix */
    if (sequence.offset < 8) {
        /* close range match, overlap */
        static const U32 dec32table[] = { 0, 1, 2, 1, 4, 4, 4, 4 };   /* added */
        static const int dec64table[] = { 8, 8, 8, 7, 8, 9,10,11 };   /* substracted */
        int const sub2 = dec64table[sequence.offset];
        op[0] = match[0];
        op[1] = match[1];
        op[2] = match[2];
        op[3] = match[3];
        match += dec32table[sequence.offset];
        ZSTDv07_copy4(op+4, match);
        match -= sub2;
    } else {
        ZSTDv07_copy8(op, match);
    }
    op += 8; match += 8;

    if (oMatchEnd > oend-(16-MINMATCH)) {
        if (op < oend_w) {
            ZSTDv07_wildcopy(op, match, oend_w - op);
            match += oend_w - op;
            op = oend_w;
        }
        while (op < oMatchEnd) *op++ = *match++;
    } else {
        ZSTDv07_wildcopy(op, match, (ptrdiff_t)sequence.matchLength-8);   /* works even if matchLength < 8 */
    }
    return sequenceLength;
}


static size_t ZSTDv07_decompressSequences(
                               ZSTDv07_DCtx* dctx,
                               void* dst, size_t maxDstSize,
                         const void* seqStart, size_t seqSize)
{
    const BYTE* ip = (const BYTE*)seqStart;
    const BYTE* const iend = ip + seqSize;
    BYTE* const ostart = (BYTE* const)dst;
    BYTE* const oend = ostart + maxDstSize;
    BYTE* op = ostart;
    const BYTE* litPtr = dctx->litPtr;
    const BYTE* const litEnd = litPtr + dctx->litSize;
    FSEv07_DTable* DTableLL = dctx->LLTable;
    FSEv07_DTable* DTableML = dctx->MLTable;
    FSEv07_DTable* DTableOffb = dctx->OffTable;
    const BYTE* const base = (const BYTE*) (dctx->base);
    const BYTE* const vBase = (const BYTE*) (dctx->vBase);
    const BYTE* const dictEnd = (const BYTE*) (dctx->dictEnd);
    int nbSeq;

    /* Build Decoding Tables */
    {   size_t const seqHSize = ZSTDv07_decodeSeqHeaders(&nbSeq, DTableLL, DTableML, DTableOffb, dctx->fseEntropy, ip, seqSize);
        if (ZSTDv07_isError(seqHSize)) return seqHSize;
        ip += seqHSize;
    }

    /* Regen sequences */
    if (nbSeq) {
        seqState_t seqState;
        dctx->fseEntropy = 1;
        { U32 i; for (i=0; i<ZSTDv07_REP_INIT; i++) seqState.prevOffset[i] = dctx->rep[i]; }
        { size_t const errorCode = BITv07_initDStream(&(seqState.DStream), ip, iend-ip);
          if (ERR_isError(errorCode)) return ERROR(corruption_detected); }
        FSEv07_initDState(&(seqState.stateLL), &(seqState.DStream), DTableLL);
        FSEv07_initDState(&(seqState.stateOffb), &(seqState.DStream), DTableOffb);
        FSEv07_initDState(&(seqState.stateML), &(seqState.DStream), DTableML);

        for ( ; (BITv07_reloadDStream(&(seqState.DStream)) <= BITv07_DStream_completed) && nbSeq ; ) {
            nbSeq--;
            {   seq_t const sequence = ZSTDv07_decodeSequence(&seqState);
                size_t const oneSeqSize = ZSTDv07_execSequence(op, oend, sequence, &litPtr, litEnd, base, vBase, dictEnd);
                if (ZSTDv07_isError(oneSeqSize)) return oneSeqSize;
                op += oneSeqSize;
        }   }

        /* check if reached exact end */
        if (nbSeq) return ERROR(corruption_detected);
        /* save reps for next block */
        { U32 i; for (i=0; i<ZSTDv07_REP_INIT; i++) dctx->rep[i] = (U32)(seqState.prevOffset[i]); }
    }

    /* last literal segment */
    {   size_t const lastLLSize = litEnd - litPtr;
        //if (litPtr > litEnd) return ERROR(corruption_detected);   /* too many literals already used */
        if (lastLLSize > (size_t)(oend-op)) return ERROR(dstSize_tooSmall);
        memcpy(op, litPtr, lastLLSize);
        op += lastLLSize;
    }

    return op-ostart;
}


static void ZSTDv07_checkContinuity(ZSTDv07_DCtx* dctx, const void* dst)
{
    if (dst != dctx->previousDstEnd) {   /* not contiguous */
        dctx->dictEnd = dctx->previousDstEnd;
        dctx->vBase = (const char*)dst - ((const char*)(dctx->previousDstEnd) - (const char*)(dctx->base));
        dctx->base = dst;
        dctx->previousDstEnd = dst;
    }
}


static size_t ZSTDv07_decompressBlock_internal(ZSTDv07_DCtx* dctx,
                            void* dst, size_t dstCapacity,
                      const void* src, size_t srcSize)
{   /* blockType == blockCompressed */
    const BYTE* ip = (const BYTE*)src;

    if (srcSize >= ZSTDv07_BLOCKSIZE_ABSOLUTEMAX) return ERROR(srcSize_wrong);

    /* Decode literals sub-block */
    {   size_t const litCSize = ZSTDv07_decodeLiteralsBlock(dctx, src, srcSize);
        if (ZSTDv07_isError(litCSize)) return litCSize;
        ip += litCSize;
        srcSize -= litCSize;
    }
    return ZSTDv07_decompressSequences(dctx, dst, dstCapacity, ip, srcSize);
}


size_t ZSTDv07_decompressBlock(ZSTDv07_DCtx* dctx,
                            void* dst, size_t dstCapacity,
                      const void* src, size_t srcSize)
{
    size_t dSize;
    ZSTDv07_checkContinuity(dctx, dst);
    dSize = ZSTDv07_decompressBlock_internal(dctx, dst, dstCapacity, src, srcSize);
    dctx->previousDstEnd = (char*)dst + dSize;
    return dSize;
}


/** ZSTDv07_insertBlock() :
    insert `src` block into `dctx` history. Useful to track uncompressed blocks. */
ZSTDLIBv07_API size_t ZSTDv07_insertBlock(ZSTDv07_DCtx* dctx, const void* blockStart, size_t blockSize)
{
    ZSTDv07_checkContinuity(dctx, blockStart);
    dctx->previousDstEnd = (const char*)blockStart + blockSize;
    return blockSize;
}


static size_t ZSTDv07_generateNxBytes(void* dst, size_t dstCapacity, BYTE byte, size_t length)
{
    if (length > dstCapacity) return ERROR(dstSize_tooSmall);
    memset(dst, byte, length);
    return length;
}


/*! ZSTDv07_decompressFrame() :
*   `dctx` must be properly initialized */
static size_t ZSTDv07_decompressFrame(ZSTDv07_DCtx* dctx,
                                 void* dst, size_t dstCapacity,
                                 const void* src, size_t srcSize)
{
    const BYTE* ip = (const BYTE*)src;
    const BYTE* const iend = ip + srcSize;
    BYTE* const ostart = (BYTE* const)dst;
    BYTE* const oend = ostart + dstCapacity;
    BYTE* op = ostart;
    size_t remainingSize = srcSize;

    /* check */
    if (srcSize < ZSTDv07_frameHeaderSize_min+ZSTDv07_blockHeaderSize) return ERROR(srcSize_wrong);

    /* Frame Header */
    {   size_t const frameHeaderSize = ZSTDv07_frameHeaderSize(src, ZSTDv07_frameHeaderSize_min);
        if (ZSTDv07_isError(frameHeaderSize)) return frameHeaderSize;
        if (srcSize < frameHeaderSize+ZSTDv07_blockHeaderSize) return ERROR(srcSize_wrong);
        if (ZSTDv07_decodeFrameHeader(dctx, src, frameHeaderSize)) return ERROR(corruption_detected);
        ip += frameHeaderSize; remainingSize -= frameHeaderSize;
    }

    /* Loop on each block */
    while (1) {
        size_t decodedSize;
        blockProperties_t blockProperties;
        size_t const cBlockSize = ZSTDv07_getcBlockSize(ip, iend-ip, &blockProperties);
        if (ZSTDv07_isError(cBlockSize)) return cBlockSize;

        ip += ZSTDv07_blockHeaderSize;
        remainingSize -= ZSTDv07_blockHeaderSize;
        if (cBlockSize > remainingSize) return ERROR(srcSize_wrong);

        switch(blockProperties.blockType)
        {
        case bt_compressed:
            decodedSize = ZSTDv07_decompressBlock_internal(dctx, op, oend-op, ip, cBlockSize);
            break;
        case bt_raw :
            decodedSize = ZSTDv07_copyRawBlock(op, oend-op, ip, cBlockSize);
            break;
        case bt_rle :
            decodedSize = ZSTDv07_generateNxBytes(op, oend-op, *ip, blockProperties.origSize);
            break;
        case bt_end :
            /* end of frame */
            if (remainingSize) return ERROR(srcSize_wrong);
            decodedSize = 0;
            break;
        default:
            return ERROR(GENERIC);   /* impossible */
        }
        if (blockProperties.blockType == bt_end) break;   /* bt_end */

        if (ZSTDv07_isError(decodedSize)) return decodedSize;
        if (dctx->fParams.checksumFlag) XXH64_update(&dctx->xxhState, op, decodedSize);
        op += decodedSize;
        ip += cBlockSize;
        remainingSize -= cBlockSize;
    }

    return op-ostart;
}


/*! ZSTDv07_decompress_usingPreparedDCtx() :
*   Same as ZSTDv07_decompress_usingDict, but using a reference context `preparedDCtx`, where dictionary has been loaded.
*   It avoids reloading the dictionary each time.
*   `preparedDCtx` must have been properly initialized using ZSTDv07_decompressBegin_usingDict().
*   Requires 2 contexts : 1 for reference (preparedDCtx), which will not be modified, and 1 to run the decompression operation (dctx) */
static size_t ZSTDv07_decompress_usingPreparedDCtx(ZSTDv07_DCtx* dctx, const ZSTDv07_DCtx* refDCtx,
                                         void* dst, size_t dstCapacity,
                                   const void* src, size_t srcSize)
{
    ZSTDv07_copyDCtx(dctx, refDCtx);
    ZSTDv07_checkContinuity(dctx, dst);
    return ZSTDv07_decompressFrame(dctx, dst, dstCapacity, src, srcSize);
}


size_t ZSTDv07_decompress_usingDict(ZSTDv07_DCtx* dctx,
                                 void* dst, size_t dstCapacity,
                                 const void* src, size_t srcSize,
                                 const void* dict, size_t dictSize)
{
    ZSTDv07_decompressBegin_usingDict(dctx, dict, dictSize);
    ZSTDv07_checkContinuity(dctx, dst);
    return ZSTDv07_decompressFrame(dctx, dst, dstCapacity, src, srcSize);
}


size_t ZSTDv07_decompressDCtx(ZSTDv07_DCtx* dctx, void* dst, size_t dstCapacity, const void* src, size_t srcSize)
{
    return ZSTDv07_decompress_usingDict(dctx, dst, dstCapacity, src, srcSize, NULL, 0);
}


size_t ZSTDv07_decompress(void* dst, size_t dstCapacity, const void* src, size_t srcSize)
{
#if defined(ZSTDv07_HEAPMODE) && (ZSTDv07_HEAPMODE==1)
    size_t regenSize;
    ZSTDv07_DCtx* const dctx = ZSTDv07_createDCtx();
    if (dctx==NULL) return ERROR(memory_allocation);
    regenSize = ZSTDv07_decompressDCtx(dctx, dst, dstCapacity, src, srcSize);
    ZSTDv07_freeDCtx(dctx);
    return regenSize;
#else   /* stack mode */
    ZSTDv07_DCtx dctx;
    return ZSTDv07_decompressDCtx(&dctx, dst, dstCapacity, src, srcSize);
#endif
}

size_t ZSTDv07_findFrameCompressedSize(const void* src, size_t srcSize)
{
    const BYTE* ip = (const BYTE*)src;
    size_t remainingSize = srcSize;

    /* check */
    if (srcSize < ZSTDv07_frameHeaderSize_min+ZSTDv07_blockHeaderSize) return ERROR(srcSize_wrong);

    /* Frame Header */
    {   size_t const frameHeaderSize = ZSTDv07_frameHeaderSize(src, ZSTDv07_frameHeaderSize_min);
        if (ZSTDv07_isError(frameHeaderSize)) return frameHeaderSize;
        if (MEM_readLE32(src) != ZSTDv07_MAGICNUMBER) return ERROR(prefix_unknown);
        if (srcSize < frameHeaderSize+ZSTDv07_blockHeaderSize) return ERROR(srcSize_wrong);
        ip += frameHeaderSize; remainingSize -= frameHeaderSize;
    }

    /* Loop on each block */
    while (1) {
        blockProperties_t blockProperties;
        size_t const cBlockSize = ZSTDv07_getcBlockSize(ip, remainingSize, &blockProperties);
        if (ZSTDv07_isError(cBlockSize)) return cBlockSize;

        ip += ZSTDv07_blockHeaderSize;
        remainingSize -= ZSTDv07_blockHeaderSize;

        if (blockProperties.blockType == bt_end) break;

        if (cBlockSize > remainingSize) return ERROR(srcSize_wrong);

        ip += cBlockSize;
        remainingSize -= cBlockSize;
    }

    return ip - (const BYTE*)src;
}

/*_******************************
*  Streaming Decompression API
********************************/
size_t ZSTDv07_nextSrcSizeToDecompress(ZSTDv07_DCtx* dctx)
{
    return dctx->expected;
}

int ZSTDv07_isSkipFrame(ZSTDv07_DCtx* dctx)
{
    return dctx->stage == ZSTDds_skipFrame;
}

/** ZSTDv07_decompressContinue() :
*   @return : nb of bytes generated into `dst` (necessarily <= `dstCapacity)
*             or an error code, which can be tested using ZSTDv07_isError() */
size_t ZSTDv07_decompressContinue(ZSTDv07_DCtx* dctx, void* dst, size_t dstCapacity, const void* src, size_t srcSize)
{
    /* Sanity check */
    if (srcSize != dctx->expected) return ERROR(srcSize_wrong);
    if (dstCapacity) ZSTDv07_checkContinuity(dctx, dst);

    switch (dctx->stage)
    {
    case ZSTDds_getFrameHeaderSize :
        if (srcSize != ZSTDv07_frameHeaderSize_min) return ERROR(srcSize_wrong);   /* impossible */
        if ((MEM_readLE32(src) & 0xFFFFFFF0U) == ZSTDv07_MAGIC_SKIPPABLE_START) {
            memcpy(dctx->headerBuffer, src, ZSTDv07_frameHeaderSize_min);
            dctx->expected = ZSTDv07_skippableHeaderSize - ZSTDv07_frameHeaderSize_min; /* magic number + skippable frame length */
            dctx->stage = ZSTDds_decodeSkippableHeader;
            return 0;
        }
        dctx->headerSize = ZSTDv07_frameHeaderSize(src, ZSTDv07_frameHeaderSize_min);
        if (ZSTDv07_isError(dctx->headerSize)) return dctx->headerSize;
        memcpy(dctx->headerBuffer, src, ZSTDv07_frameHeaderSize_min);
        if (dctx->headerSize > ZSTDv07_frameHeaderSize_min) {
            dctx->expected = dctx->headerSize - ZSTDv07_frameHeaderSize_min;
            dctx->stage = ZSTDds_decodeFrameHeader;
            return 0;
        }
        dctx->expected = 0;   /* not necessary to copy more */
	/* fall-through */
    case ZSTDds_decodeFrameHeader:
        {   size_t result;
            memcpy(dctx->headerBuffer + ZSTDv07_frameHeaderSize_min, src, dctx->expected);
            result = ZSTDv07_decodeFrameHeader(dctx, dctx->headerBuffer, dctx->headerSize);
            if (ZSTDv07_isError(result)) return result;
            dctx->expected = ZSTDv07_blockHeaderSize;
            dctx->stage = ZSTDds_decodeBlockHeader;
            return 0;
        }
    case ZSTDds_decodeBlockHeader:
        {   blockProperties_t bp;
            size_t const cBlockSize = ZSTDv07_getcBlockSize(src, ZSTDv07_blockHeaderSize, &bp);
            if (ZSTDv07_isError(cBlockSize)) return cBlockSize;
            if (bp.blockType == bt_end) {
                if (dctx->fParams.checksumFlag) {
                    U64 const h64 = XXH64_digest(&dctx->xxhState);
                    U32 const h32 = (U32)(h64>>11) & ((1<<22)-1);
                    const BYTE* const ip = (const BYTE*)src;
                    U32 const check32 = ip[2] + (ip[1] << 8) + ((ip[0] & 0x3F) << 16);
                    if (check32 != h32) return ERROR(checksum_wrong);
                }
                dctx->expected = 0;
                dctx->stage = ZSTDds_getFrameHeaderSize;
            } else {
                dctx->expected = cBlockSize;
                dctx->bType = bp.blockType;
                dctx->stage = ZSTDds_decompressBlock;
            }
            return 0;
        }
    case ZSTDds_decompressBlock:
        {   size_t rSize;
            switch(dctx->bType)
            {
            case bt_compressed:
                rSize = ZSTDv07_decompressBlock_internal(dctx, dst, dstCapacity, src, srcSize);
                break;
            case bt_raw :
                rSize = ZSTDv07_copyRawBlock(dst, dstCapacity, src, srcSize);
                break;
            case bt_rle :
                return ERROR(GENERIC);   /* not yet handled */
                break;
            case bt_end :   /* should never happen (filtered at phase 1) */
                rSize = 0;
                break;
            default:
                return ERROR(GENERIC);   /* impossible */
            }
            dctx->stage = ZSTDds_decodeBlockHeader;
            dctx->expected = ZSTDv07_blockHeaderSize;
            dctx->previousDstEnd = (char*)dst + rSize;
            if (ZSTDv07_isError(rSize)) return rSize;
            if (dctx->fParams.checksumFlag) XXH64_update(&dctx->xxhState, dst, rSize);
            return rSize;
        }
    case ZSTDds_decodeSkippableHeader:
        {   memcpy(dctx->headerBuffer + ZSTDv07_frameHeaderSize_min, src, dctx->expected);
            dctx->expected = MEM_readLE32(dctx->headerBuffer + 4);
            dctx->stage = ZSTDds_skipFrame;
            return 0;
        }
    case ZSTDds_skipFrame:
        {   dctx->expected = 0;
            dctx->stage = ZSTDds_getFrameHeaderSize;
            return 0;
        }
    default:
        return ERROR(GENERIC);   /* impossible */
    }
}


static size_t ZSTDv07_refDictContent(ZSTDv07_DCtx* dctx, const void* dict, size_t dictSize)
{
    dctx->dictEnd = dctx->previousDstEnd;
    dctx->vBase = (const char*)dict - ((const char*)(dctx->previousDstEnd) - (const char*)(dctx->base));
    dctx->base = dict;
    dctx->previousDstEnd = (const char*)dict + dictSize;
    return 0;
}

static size_t ZSTDv07_loadEntropy(ZSTDv07_DCtx* dctx, const void* const dict, size_t const dictSize)
{
    const BYTE* dictPtr = (const BYTE*)dict;
    const BYTE* const dictEnd = dictPtr + dictSize;

    {   size_t const hSize = HUFv07_readDTableX4(dctx->hufTable, dict, dictSize);
        if (HUFv07_isError(hSize)) return ERROR(dictionary_corrupted);
        dictPtr += hSize;
    }

    {   short offcodeNCount[MaxOff+1];
        U32 offcodeMaxValue=MaxOff, offcodeLog;
        size_t const offcodeHeaderSize = FSEv07_readNCount(offcodeNCount, &offcodeMaxValue, &offcodeLog, dictPtr, dictEnd-dictPtr);
        if (FSEv07_isError(offcodeHeaderSize)) return ERROR(dictionary_corrupted);
        if (offcodeLog > OffFSELog) return ERROR(dictionary_corrupted);
        { size_t const errorCode = FSEv07_buildDTable(dctx->OffTable, offcodeNCount, offcodeMaxValue, offcodeLog);
          if (FSEv07_isError(errorCode)) return ERROR(dictionary_corrupted); }
        dictPtr += offcodeHeaderSize;
    }

    {   short matchlengthNCount[MaxML+1];
        unsigned matchlengthMaxValue = MaxML, matchlengthLog;
        size_t const matchlengthHeaderSize = FSEv07_readNCount(matchlengthNCount, &matchlengthMaxValue, &matchlengthLog, dictPtr, dictEnd-dictPtr);
        if (FSEv07_isError(matchlengthHeaderSize)) return ERROR(dictionary_corrupted);
        if (matchlengthLog > MLFSELog) return ERROR(dictionary_corrupted);
        { size_t const errorCode = FSEv07_buildDTable(dctx->MLTable, matchlengthNCount, matchlengthMaxValue, matchlengthLog);
          if (FSEv07_isError(errorCode)) return ERROR(dictionary_corrupted); }
        dictPtr += matchlengthHeaderSize;
    }

    {   short litlengthNCount[MaxLL+1];
        unsigned litlengthMaxValue = MaxLL, litlengthLog;
        size_t const litlengthHeaderSize = FSEv07_readNCount(litlengthNCount, &litlengthMaxValue, &litlengthLog, dictPtr, dictEnd-dictPtr);
        if (FSEv07_isError(litlengthHeaderSize)) return ERROR(dictionary_corrupted);
        if (litlengthLog > LLFSELog) return ERROR(dictionary_corrupted);
        { size_t const errorCode = FSEv07_buildDTable(dctx->LLTable, litlengthNCount, litlengthMaxValue, litlengthLog);
          if (FSEv07_isError(errorCode)) return ERROR(dictionary_corrupted); }
        dictPtr += litlengthHeaderSize;
    }

    if (dictPtr+12 > dictEnd) return ERROR(dictionary_corrupted);
    dctx->rep[0] = MEM_readLE32(dictPtr+0); if (dctx->rep[0] == 0 || dctx->rep[0] >= dictSize) return ERROR(dictionary_corrupted);
    dctx->rep[1] = MEM_readLE32(dictPtr+4); if (dctx->rep[1] == 0 || dctx->rep[1] >= dictSize) return ERROR(dictionary_corrupted);
    dctx->rep[2] = MEM_readLE32(dictPtr+8); if (dctx->rep[2] == 0 || dctx->rep[2] >= dictSize) return ERROR(dictionary_corrupted);
    dictPtr += 12;

    dctx->litEntropy = dctx->fseEntropy = 1;
    return dictPtr - (const BYTE*)dict;
}

static size_t ZSTDv07_decompress_insertDictionary(ZSTDv07_DCtx* dctx, const void* dict, size_t dictSize)
{
    if (dictSize < 8) return ZSTDv07_refDictContent(dctx, dict, dictSize);
    {   U32 const magic = MEM_readLE32(dict);
        if (magic != ZSTDv07_DICT_MAGIC) {
            return ZSTDv07_refDictContent(dctx, dict, dictSize);   /* pure content mode */
    }   }
    dctx->dictID = MEM_readLE32((const char*)dict + 4);

    /* load entropy tables */
    dict = (const char*)dict + 8;
    dictSize -= 8;
    {   size_t const eSize = ZSTDv07_loadEntropy(dctx, dict, dictSize);
        if (ZSTDv07_isError(eSize)) return ERROR(dictionary_corrupted);
        dict = (const char*)dict + eSize;
        dictSize -= eSize;
    }

    /* reference dictionary content */
    return ZSTDv07_refDictContent(dctx, dict, dictSize);
}


size_t ZSTDv07_decompressBegin_usingDict(ZSTDv07_DCtx* dctx, const void* dict, size_t dictSize)
{
    { size_t const errorCode = ZSTDv07_decompressBegin(dctx);
      if (ZSTDv07_isError(errorCode)) return errorCode; }

    if (dict && dictSize) {
        size_t const errorCode = ZSTDv07_decompress_insertDictionary(dctx, dict, dictSize);
        if (ZSTDv07_isError(errorCode)) return ERROR(dictionary_corrupted);
    }

    return 0;
}


struct ZSTDv07_DDict_s {
    void* dict;
    size_t dictSize;
    ZSTDv07_DCtx* refContext;
};  /* typedef'd tp ZSTDv07_CDict within zstd.h */

static ZSTDv07_DDict* ZSTDv07_createDDict_advanced(const void* dict, size_t dictSize, ZSTDv07_customMem customMem)
{
    if (!customMem.customAlloc && !customMem.customFree)
        customMem = defaultCustomMem;

    if (!customMem.customAlloc || !customMem.customFree)
        return NULL;

    {   ZSTDv07_DDict* const ddict = (ZSTDv07_DDict*) customMem.customAlloc(customMem.opaque, sizeof(*ddict));
        void* const dictContent = customMem.customAlloc(customMem.opaque, dictSize);
        ZSTDv07_DCtx* const dctx = ZSTDv07_createDCtx_advanced(customMem);

        if (!dictContent || !ddict || !dctx) {
            customMem.customFree(customMem.opaque, dictContent);
            customMem.customFree(customMem.opaque, ddict);
            customMem.customFree(customMem.opaque, dctx);
            return NULL;
        }

        memcpy(dictContent, dict, dictSize);
        {   size_t const errorCode = ZSTDv07_decompressBegin_usingDict(dctx, dictContent, dictSize);
            if (ZSTDv07_isError(errorCode)) {
                customMem.customFree(customMem.opaque, dictContent);
                customMem.customFree(customMem.opaque, ddict);
                customMem.customFree(customMem.opaque, dctx);
                return NULL;
        }   }

        ddict->dict = dictContent;
        ddict->dictSize = dictSize;
        ddict->refContext = dctx;
        return ddict;
    }
}

/*! ZSTDv07_createDDict() :
*   Create a digested dictionary, ready to start decompression without startup delay.
*   `dict` can be released after `ZSTDv07_DDict` creation */
ZSTDv07_DDict* ZSTDv07_createDDict(const void* dict, size_t dictSize)
{
    ZSTDv07_customMem const allocator = { NULL, NULL, NULL };
    return ZSTDv07_createDDict_advanced(dict, dictSize, allocator);
}

size_t ZSTDv07_freeDDict(ZSTDv07_DDict* ddict)
{
    ZSTDv07_freeFunction const cFree = ddict->refContext->customMem.customFree;
    void* const opaque = ddict->refContext->customMem.opaque;
    ZSTDv07_freeDCtx(ddict->refContext);
    cFree(opaque, ddict->dict);
    cFree(opaque, ddict);
    return 0;
}

/*! ZSTDv07_decompress_usingDDict() :
*   Decompression using a pre-digested Dictionary
*   Use dictionary without significant overhead. */
ZSTDLIBv07_API size_t ZSTDv07_decompress_usingDDict(ZSTDv07_DCtx* dctx,
                                           void* dst, size_t dstCapacity,
                                     const void* src, size_t srcSize,
                                     const ZSTDv07_DDict* ddict)
{
    return ZSTDv07_decompress_usingPreparedDCtx(dctx, ddict->refContext,
                                           dst, dstCapacity,
                                           src, srcSize);
}
/*
    Buffered version of Zstd compression library
    Copyright (C) 2015-2016, Yann Collet.

    BSD 2-Clause License (http://www.opensource.org/licenses/bsd-license.php)

    Redistribution and use in source and binary forms, with or without
    modification, are permitted provided that the following conditions are
    met:
    * Redistributions of source code must retain the above copyright
    notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above
    copyright notice, this list of conditions and the following disclaimer
    in the documentation and/or other materials provided with the
    distribution.
    THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
    "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
    LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
    A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
    OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
    SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
    LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
    DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
    THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
    (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
    OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

    You can contact the author at :
    - zstd homepage : http://www.zstd.net/
*/



/*-***************************************************************************
*  Streaming decompression howto
*
*  A ZBUFFv07_DCtx object is required to track streaming operations.
*  Use ZBUFFv07_createDCtx() and ZBUFFv07_freeDCtx() to create/release resources.
*  Use ZBUFFv07_decompressInit() to start a new decompression operation,
*   or ZBUFFv07_decompressInitDictionary() if decompression requires a dictionary.
*  Note that ZBUFFv07_DCtx objects can be re-init multiple times.
*
*  Use ZBUFFv07_decompressContinue() repetitively to consume your input.
*  *srcSizePtr and *dstCapacityPtr can be any size.
*  The function will report how many bytes were read or written by modifying *srcSizePtr and *dstCapacityPtr.
*  Note that it may not consume the entire input, in which case it's up to the caller to present remaining input again.
*  The content of @dst will be overwritten (up to *dstCapacityPtr) at each function call, so save its content if it matters, or change @dst.
*  @return : a hint to preferred nb of bytes to use as input for next function call (it's only a hint, to help latency),
*            or 0 when a frame is completely decoded,
*            or an error code, which can be tested using ZBUFFv07_isError().
*
*  Hint : recommended buffer sizes (not compulsory) : ZBUFFv07_recommendedDInSize() and ZBUFFv07_recommendedDOutSize()
*  output : ZBUFFv07_recommendedDOutSize==128 KB block size is the internal unit, it ensures it's always possible to write a full block when decoded.
*  input  : ZBUFFv07_recommendedDInSize == 128KB + 3;
*           just follow indications from ZBUFFv07_decompressContinue() to minimize latency. It should always be <= 128 KB + 3 .
* *******************************************************************************/

typedef enum { ZBUFFds_init, ZBUFFds_loadHeader,
               ZBUFFds_read, ZBUFFds_load, ZBUFFds_flush } ZBUFFv07_dStage;

/* *** Resource management *** */
struct ZBUFFv07_DCtx_s {
    ZSTDv07_DCtx* zd;
    ZSTDv07_frameParams fParams;
    ZBUFFv07_dStage stage;
    char*  inBuff;
    size_t inBuffSize;
    size_t inPos;
    char*  outBuff;
    size_t outBuffSize;
    size_t outStart;
    size_t outEnd;
    size_t blockSize;
    BYTE headerBuffer[ZSTDv07_FRAMEHEADERSIZE_MAX];
    size_t lhSize;
    ZSTDv07_customMem customMem;
};   /* typedef'd to ZBUFFv07_DCtx within "zstd_buffered.h" */

ZSTDLIBv07_API ZBUFFv07_DCtx* ZBUFFv07_createDCtx_advanced(ZSTDv07_customMem customMem);

ZBUFFv07_DCtx* ZBUFFv07_createDCtx(void)
{
    return ZBUFFv07_createDCtx_advanced(defaultCustomMem);
}

ZBUFFv07_DCtx* ZBUFFv07_createDCtx_advanced(ZSTDv07_customMem customMem)
{
    ZBUFFv07_DCtx* zbd;

    if (!customMem.customAlloc && !customMem.customFree)
        customMem = defaultCustomMem;

    if (!customMem.customAlloc || !customMem.customFree)
        return NULL;

    zbd = (ZBUFFv07_DCtx*)customMem.customAlloc(customMem.opaque, sizeof(ZBUFFv07_DCtx));
    if (zbd==NULL) return NULL;
    memset(zbd, 0, sizeof(ZBUFFv07_DCtx));
    memcpy(&zbd->customMem, &customMem, sizeof(ZSTDv07_customMem));
    zbd->zd = ZSTDv07_createDCtx_advanced(customMem);
    if (zbd->zd == NULL) { ZBUFFv07_freeDCtx(zbd); return NULL; }
    zbd->stage = ZBUFFds_init;
    return zbd;
}

size_t ZBUFFv07_freeDCtx(ZBUFFv07_DCtx* zbd)
{
    if (zbd==NULL) return 0;   /* support free on null */
    ZSTDv07_freeDCtx(zbd->zd);
    if (zbd->inBuff) zbd->customMem.customFree(zbd->customMem.opaque, zbd->inBuff);
    if (zbd->outBuff) zbd->customMem.customFree(zbd->customMem.opaque, zbd->outBuff);
    zbd->customMem.customFree(zbd->customMem.opaque, zbd);
    return 0;
}


/* *** Initialization *** */

size_t ZBUFFv07_decompressInitDictionary(ZBUFFv07_DCtx* zbd, const void* dict, size_t dictSize)
{
    zbd->stage = ZBUFFds_loadHeader;
    zbd->lhSize = zbd->inPos = zbd->outStart = zbd->outEnd = 0;
    return ZSTDv07_decompressBegin_usingDict(zbd->zd, dict, dictSize);
}

size_t ZBUFFv07_decompressInit(ZBUFFv07_DCtx* zbd)
{
    return ZBUFFv07_decompressInitDictionary(zbd, NULL, 0);
}


/* internal util function */
MEM_STATIC size_t ZBUFFv07_limitCopy(void* dst, size_t dstCapacity, const void* src, size_t srcSize)
{
    size_t const length = MIN(dstCapacity, srcSize);
    memcpy(dst, src, length);
    return length;
}


/* *** Decompression *** */

size_t ZBUFFv07_decompressContinue(ZBUFFv07_DCtx* zbd,
                                void* dst, size_t* dstCapacityPtr,
                          const void* src, size_t* srcSizePtr)
{
    const char* const istart = (const char*)src;
    const char* const iend = istart + *srcSizePtr;
    const char* ip = istart;
    char* const ostart = (char*)dst;
    char* const oend = ostart + *dstCapacityPtr;
    char* op = ostart;
    U32 notDone = 1;

    while (notDone) {
        switch(zbd->stage)
        {
        case ZBUFFds_init :
            return ERROR(init_missing);

        case ZBUFFds_loadHeader :
            {   size_t const hSize = ZSTDv07_getFrameParams(&(zbd->fParams), zbd->headerBuffer, zbd->lhSize);
                if (ZSTDv07_isError(hSize)) return hSize;
                if (hSize != 0) {
                    size_t const toLoad = hSize - zbd->lhSize;   /* if hSize!=0, hSize > zbd->lhSize */
                    if (toLoad > (size_t)(iend-ip)) {   /* not enough input to load full header */
                        memcpy(zbd->headerBuffer + zbd->lhSize, ip, iend-ip);
                        zbd->lhSize += iend-ip;
                        *dstCapacityPtr = 0;
                        return (hSize - zbd->lhSize) + ZSTDv07_blockHeaderSize;   /* remaining header bytes + next block header */
                    }
                    memcpy(zbd->headerBuffer + zbd->lhSize, ip, toLoad); zbd->lhSize = hSize; ip += toLoad;
                    break;
            }   }

            /* Consume header */
            {   size_t const h1Size = ZSTDv07_nextSrcSizeToDecompress(zbd->zd);  /* == ZSTDv07_frameHeaderSize_min */
                size_t const h1Result = ZSTDv07_decompressContinue(zbd->zd, NULL, 0, zbd->headerBuffer, h1Size);
                if (ZSTDv07_isError(h1Result)) return h1Result;
                if (h1Size < zbd->lhSize) {   /* long header */
                    size_t const h2Size = ZSTDv07_nextSrcSizeToDecompress(zbd->zd);
                    size_t const h2Result = ZSTDv07_decompressContinue(zbd->zd, NULL, 0, zbd->headerBuffer+h1Size, h2Size);
                    if (ZSTDv07_isError(h2Result)) return h2Result;
            }   }

            zbd->fParams.windowSize = MAX(zbd->fParams.windowSize, 1U << ZSTDv07_WINDOWLOG_ABSOLUTEMIN);

            /* Frame header instruct buffer sizes */
            {   size_t const blockSize = MIN(zbd->fParams.windowSize, ZSTDv07_BLOCKSIZE_ABSOLUTEMAX);
                zbd->blockSize = blockSize;
                if (zbd->inBuffSize < blockSize) {
                    zbd->customMem.customFree(zbd->customMem.opaque, zbd->inBuff);
                    zbd->inBuffSize = blockSize;
                    zbd->inBuff = (char*)zbd->customMem.customAlloc(zbd->customMem.opaque, blockSize);
                    if (zbd->inBuff == NULL) return ERROR(memory_allocation);
                }
                {   size_t const neededOutSize = zbd->fParams.windowSize + blockSize + WILDCOPY_OVERLENGTH * 2;
                    if (zbd->outBuffSize < neededOutSize) {
                        zbd->customMem.customFree(zbd->customMem.opaque, zbd->outBuff);
                        zbd->outBuffSize = neededOutSize;
                        zbd->outBuff = (char*)zbd->customMem.customAlloc(zbd->customMem.opaque, neededOutSize);
                        if (zbd->outBuff == NULL) return ERROR(memory_allocation);
            }   }   }
            zbd->stage = ZBUFFds_read;
            /* pass-through */
	    /* fall-through */
        case ZBUFFds_read:
            {   size_t const neededInSize = ZSTDv07_nextSrcSizeToDecompress(zbd->zd);
                if (neededInSize==0) {  /* end of frame */
                    zbd->stage = ZBUFFds_init;
                    notDone = 0;
                    break;
                }
                if ((size_t)(iend-ip) >= neededInSize) {  /* decode directly from src */
                    const int isSkipFrame = ZSTDv07_isSkipFrame(zbd->zd);
                    size_t const decodedSize = ZSTDv07_decompressContinue(zbd->zd,
                        zbd->outBuff + zbd->outStart, (isSkipFrame ? 0 : zbd->outBuffSize - zbd->outStart),
                        ip, neededInSize);
                    if (ZSTDv07_isError(decodedSize)) return decodedSize;
                    ip += neededInSize;
                    if (!decodedSize && !isSkipFrame) break;   /* this was just a header */
                    zbd->outEnd = zbd->outStart +  decodedSize;
                    zbd->stage = ZBUFFds_flush;
                    break;
                }
                if (ip==iend) { notDone = 0; break; }   /* no more input */
                zbd->stage = ZBUFFds_load;
            }
	    /* fall-through */
        case ZBUFFds_load:
            {   size_t const neededInSize = ZSTDv07_nextSrcSizeToDecompress(zbd->zd);
                size_t const toLoad = neededInSize - zbd->inPos;   /* should always be <= remaining space within inBuff */
                size_t loadedSize;
                if (toLoad > zbd->inBuffSize - zbd->inPos) return ERROR(corruption_detected);   /* should never happen */
                loadedSize = ZBUFFv07_limitCopy(zbd->inBuff + zbd->inPos, toLoad, ip, iend-ip);
                ip += loadedSize;
                zbd->inPos += loadedSize;
                if (loadedSize < toLoad) { notDone = 0; break; }   /* not enough input, wait for more */

                /* decode loaded input */
                {  const int isSkipFrame = ZSTDv07_isSkipFrame(zbd->zd);
                   size_t const decodedSize = ZSTDv07_decompressContinue(zbd->zd,
                        zbd->outBuff + zbd->outStart, zbd->outBuffSize - zbd->outStart,
                        zbd->inBuff, neededInSize);
                    if (ZSTDv07_isError(decodedSize)) return decodedSize;
                    zbd->inPos = 0;   /* input is consumed */
                    if (!decodedSize && !isSkipFrame) { zbd->stage = ZBUFFds_read; break; }   /* this was just a header */
                    zbd->outEnd = zbd->outStart +  decodedSize;
                    zbd->stage = ZBUFFds_flush;
                    /* break; */
                    /* pass-through */
                }
	    }
	    /* fall-through */
        case ZBUFFds_flush:
            {   size_t const toFlushSize = zbd->outEnd - zbd->outStart;
                size_t const flushedSize = ZBUFFv07_limitCopy(op, oend-op, zbd->outBuff + zbd->outStart, toFlushSize);
                op += flushedSize;
                zbd->outStart += flushedSize;
                if (flushedSize == toFlushSize) {
                    zbd->stage = ZBUFFds_read;
                    if (zbd->outStart + zbd->blockSize > zbd->outBuffSize)
                        zbd->outStart = zbd->outEnd = 0;
                    break;
                }
                /* cannot flush everything */
                notDone = 0;
                break;
            }
        default: return ERROR(GENERIC);   /* impossible */
    }   }

    /* result */
    *srcSizePtr = ip-istart;
    *dstCapacityPtr = op-ostart;
    {   size_t nextSrcSizeHint = ZSTDv07_nextSrcSizeToDecompress(zbd->zd);
        nextSrcSizeHint -= zbd->inPos;   /* already loaded*/
        return nextSrcSizeHint;
    }
}



/* *************************************
*  Tool functions
***************************************/
size_t ZBUFFv07_recommendedDInSize(void)  { return ZSTDv07_BLOCKSIZE_ABSOLUTEMAX + ZSTDv07_blockHeaderSize /* block header size*/ ; }
size_t ZBUFFv07_recommendedDOutSize(void) { return ZSTDv07_BLOCKSIZE_ABSOLUTEMAX; }
