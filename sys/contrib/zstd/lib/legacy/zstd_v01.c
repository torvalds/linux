/*
 * Copyright (c) 2016-present, Yann Collet, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under both the BSD-style license (found in the
 * LICENSE file in the root directory of this source tree) and the GPLv2 (found
 * in the COPYING file in the root directory of this source tree).
 * You may select, at your option, one of the above-listed licenses.
 */


/******************************************
*  Includes
******************************************/
#include <stddef.h>    /* size_t, ptrdiff_t */
#include "zstd_v01.h"
#include "error_private.h"


/******************************************
*  Static allocation
******************************************/
/* You can statically allocate FSE CTable/DTable as a table of unsigned using below macro */
#define FSE_DTABLE_SIZE_U32(maxTableLog)                   (1 + (1<<maxTableLog))

/* You can statically allocate Huff0 DTable as a table of unsigned short using below macro */
#define HUF_DTABLE_SIZE_U16(maxTableLog)   (1 + (1<<maxTableLog))
#define HUF_CREATE_STATIC_DTABLE(DTable, maxTableLog) \
        unsigned short DTable[HUF_DTABLE_SIZE_U16(maxTableLog)] = { maxTableLog }


/******************************************
*  Error Management
******************************************/
#define FSE_LIST_ERRORS(ITEM) \
        ITEM(FSE_OK_NoError) ITEM(FSE_ERROR_GENERIC) \
        ITEM(FSE_ERROR_tableLog_tooLarge) ITEM(FSE_ERROR_maxSymbolValue_tooLarge) ITEM(FSE_ERROR_maxSymbolValue_tooSmall) \
        ITEM(FSE_ERROR_dstSize_tooSmall) ITEM(FSE_ERROR_srcSize_wrong)\
        ITEM(FSE_ERROR_corruptionDetected) \
        ITEM(FSE_ERROR_maxCode)

#define FSE_GENERATE_ENUM(ENUM) ENUM,
typedef enum { FSE_LIST_ERRORS(FSE_GENERATE_ENUM) } FSE_errorCodes;  /* enum is exposed, to detect & handle specific errors; compare function result to -enum value */


/******************************************
*  FSE symbol compression API
******************************************/
/*
   This API consists of small unitary functions, which highly benefit from being inlined.
   You will want to enable link-time-optimization to ensure these functions are properly inlined in your binary.
   Visual seems to do it automatically.
   For gcc or clang, you'll need to add -flto flag at compilation and linking stages.
   If none of these solutions is applicable, include "fse.c" directly.
*/

typedef unsigned FSE_CTable;   /* don't allocate that. It's just a way to be more restrictive than void* */
typedef unsigned FSE_DTable;   /* don't allocate that. It's just a way to be more restrictive than void* */

typedef struct
{
    size_t bitContainer;
    int    bitPos;
    char*  startPtr;
    char*  ptr;
    char*  endPtr;
} FSE_CStream_t;

typedef struct
{
    ptrdiff_t   value;
    const void* stateTable;
    const void* symbolTT;
    unsigned    stateLog;
} FSE_CState_t;

typedef struct
{
    size_t   bitContainer;
    unsigned bitsConsumed;
    const char* ptr;
    const char* start;
} FSE_DStream_t;

typedef struct
{
    size_t      state;
    const void* table;   /* precise table may vary, depending on U16 */
} FSE_DState_t;

typedef enum { FSE_DStream_unfinished = 0,
               FSE_DStream_endOfBuffer = 1,
               FSE_DStream_completed = 2,
               FSE_DStream_tooFar = 3 } FSE_DStream_status;  /* result of FSE_reloadDStream() */
               /* 1,2,4,8 would be better for bitmap combinations, but slows down performance a bit ... ?! */


/****************************************************************
*  Tuning parameters
****************************************************************/
/* MEMORY_USAGE :
*  Memory usage formula : N->2^N Bytes (examples : 10 -> 1KB; 12 -> 4KB ; 16 -> 64KB; 20 -> 1MB; etc.)
*  Increasing memory usage improves compression ratio
*  Reduced memory usage can improve speed, due to cache effect
*  Recommended max value is 14, for 16KB, which nicely fits into Intel x86 L1 cache */
#define FSE_MAX_MEMORY_USAGE 14
#define FSE_DEFAULT_MEMORY_USAGE 13

/* FSE_MAX_SYMBOL_VALUE :
*  Maximum symbol value authorized.
*  Required for proper stack allocation */
#define FSE_MAX_SYMBOL_VALUE 255


/****************************************************************
*  template functions type & suffix
****************************************************************/
#define FSE_FUNCTION_TYPE BYTE
#define FSE_FUNCTION_EXTENSION


/****************************************************************
*  Byte symbol type
****************************************************************/
typedef struct
{
    unsigned short newState;
    unsigned char  symbol;
    unsigned char  nbBits;
} FSE_decode_t;   /* size == U32 */



/****************************************************************
*  Compiler specifics
****************************************************************/
#ifdef _MSC_VER    /* Visual Studio */
#  define FORCE_INLINE static __forceinline
#  include <intrin.h>                    /* For Visual 2005 */
#  pragma warning(disable : 4127)        /* disable: C4127: conditional expression is constant */
#  pragma warning(disable : 4214)        /* disable: C4214: non-int bitfields */
#else
#  define GCC_VERSION (__GNUC__ * 100 + __GNUC_MINOR__)
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


/****************************************************************
*  Includes
****************************************************************/
#include <stdlib.h>     /* malloc, free, qsort */
#include <string.h>     /* memcpy, memset */
#include <stdio.h>      /* printf (debug) */


#ifndef MEM_ACCESS_MODULE
#define MEM_ACCESS_MODULE
/****************************************************************
*  Basic Types
*****************************************************************/
#if defined (__STDC_VERSION__) && __STDC_VERSION__ >= 199901L   /* C99 */
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

#endif   /* MEM_ACCESS_MODULE */

/****************************************************************
*  Memory I/O
*****************************************************************/
/* FSE_FORCE_MEMORY_ACCESS
 * By default, access to unaligned memory is controlled by `memcpy()`, which is safe and portable.
 * Unfortunately, on some target/compiler combinations, the generated assembly is sub-optimal.
 * The below switch allow to select different access method for improved performance.
 * Method 0 (default) : use `memcpy()`. Safe and portable.
 * Method 1 : `__packed` statement. It depends on compiler extension (ie, not portable).
 *            This method is safe if your compiler supports it, and *generally* as fast or faster than `memcpy`.
 * Method 2 : direct access. This method is portable but violate C standard.
 *            It can generate buggy code on targets generating assembly depending on alignment.
 *            But in some circumstances, it's the only known way to get the most performance (ie GCC + ARMv6)
 * See http://fastcompression.blogspot.fr/2015/08/accessing-unaligned-memory.html for details.
 * Prefer these methods in priority order (0 > 1 > 2)
 */
#ifndef FSE_FORCE_MEMORY_ACCESS   /* can be defined externally, on command line for example */
#  if defined(__GNUC__) && ( defined(__ARM_ARCH_6__) || defined(__ARM_ARCH_6J__) || defined(__ARM_ARCH_6K__) || defined(__ARM_ARCH_6Z__) || defined(__ARM_ARCH_6ZK__) || defined(__ARM_ARCH_6T2__) )
#    define FSE_FORCE_MEMORY_ACCESS 2
#  elif (defined(__INTEL_COMPILER) && !defined(WIN32)) || \
  (defined(__GNUC__) && ( defined(__ARM_ARCH_7__) || defined(__ARM_ARCH_7A__) || defined(__ARM_ARCH_7R__) || defined(__ARM_ARCH_7M__) || defined(__ARM_ARCH_7S__) ))
#    define FSE_FORCE_MEMORY_ACCESS 1
#  endif
#endif


static unsigned FSE_32bits(void)
{
    return sizeof(void*)==4;
}

static unsigned FSE_isLittleEndian(void)
{
    const union { U32 i; BYTE c[4]; } one = { 1 };   /* don't use static : performance detrimental  */
    return one.c[0];
}

#if defined(FSE_FORCE_MEMORY_ACCESS) && (FSE_FORCE_MEMORY_ACCESS==2)

static U16 FSE_read16(const void* memPtr) { return *(const U16*) memPtr; }
static U32 FSE_read32(const void* memPtr) { return *(const U32*) memPtr; }
static U64 FSE_read64(const void* memPtr) { return *(const U64*) memPtr; }

#elif defined(FSE_FORCE_MEMORY_ACCESS) && (FSE_FORCE_MEMORY_ACCESS==1)

/* __pack instructions are safer, but compiler specific, hence potentially problematic for some compilers */
/* currently only defined for gcc and icc */
typedef union { U16 u16; U32 u32; U64 u64; } __attribute__((packed)) unalign;

static U16 FSE_read16(const void* ptr) { return ((const unalign*)ptr)->u16; }
static U32 FSE_read32(const void* ptr) { return ((const unalign*)ptr)->u32; }
static U64 FSE_read64(const void* ptr) { return ((const unalign*)ptr)->u64; }

#else

static U16 FSE_read16(const void* memPtr)
{
    U16 val; memcpy(&val, memPtr, sizeof(val)); return val;
}

static U32 FSE_read32(const void* memPtr)
{
    U32 val; memcpy(&val, memPtr, sizeof(val)); return val;
}

static U64 FSE_read64(const void* memPtr)
{
    U64 val; memcpy(&val, memPtr, sizeof(val)); return val;
}

#endif // FSE_FORCE_MEMORY_ACCESS

static U16 FSE_readLE16(const void* memPtr)
{
    if (FSE_isLittleEndian())
        return FSE_read16(memPtr);
    else
    {
        const BYTE* p = (const BYTE*)memPtr;
        return (U16)(p[0] + (p[1]<<8));
    }
}

static U32 FSE_readLE32(const void* memPtr)
{
    if (FSE_isLittleEndian())
        return FSE_read32(memPtr);
    else
    {
        const BYTE* p = (const BYTE*)memPtr;
        return (U32)((U32)p[0] + ((U32)p[1]<<8) + ((U32)p[2]<<16) + ((U32)p[3]<<24));
    }
}


static U64 FSE_readLE64(const void* memPtr)
{
    if (FSE_isLittleEndian())
        return FSE_read64(memPtr);
    else
    {
        const BYTE* p = (const BYTE*)memPtr;
        return (U64)((U64)p[0] + ((U64)p[1]<<8) + ((U64)p[2]<<16) + ((U64)p[3]<<24)
                     + ((U64)p[4]<<32) + ((U64)p[5]<<40) + ((U64)p[6]<<48) + ((U64)p[7]<<56));
    }
}

static size_t FSE_readLEST(const void* memPtr)
{
    if (FSE_32bits())
        return (size_t)FSE_readLE32(memPtr);
    else
        return (size_t)FSE_readLE64(memPtr);
}



/****************************************************************
*  Constants
*****************************************************************/
#define FSE_MAX_TABLELOG  (FSE_MAX_MEMORY_USAGE-2)
#define FSE_MAX_TABLESIZE (1U<<FSE_MAX_TABLELOG)
#define FSE_MAXTABLESIZE_MASK (FSE_MAX_TABLESIZE-1)
#define FSE_DEFAULT_TABLELOG (FSE_DEFAULT_MEMORY_USAGE-2)
#define FSE_MIN_TABLELOG 5

#define FSE_TABLELOG_ABSOLUTE_MAX 15
#if FSE_MAX_TABLELOG > FSE_TABLELOG_ABSOLUTE_MAX
#error "FSE_MAX_TABLELOG > FSE_TABLELOG_ABSOLUTE_MAX is not supported"
#endif


/****************************************************************
*  Error Management
****************************************************************/
#define FSE_STATIC_ASSERT(c) { enum { FSE_static_assert = 1/(int)(!!(c)) }; }   /* use only *after* variable declarations */


/****************************************************************
*  Complex types
****************************************************************/
typedef struct
{
    int deltaFindState;
    U32 deltaNbBits;
} FSE_symbolCompressionTransform; /* total 8 bytes */

typedef U32 DTable_max_t[FSE_DTABLE_SIZE_U32(FSE_MAX_TABLELOG)];

/****************************************************************
*  Internal functions
****************************************************************/
FORCE_INLINE unsigned FSE_highbit32 (U32 val)
{
#   if defined(_MSC_VER)   /* Visual */
    unsigned long r;
    _BitScanReverse ( &r, val );
    return (unsigned) r;
#   elif defined(__GNUC__) && (GCC_VERSION >= 304)   /* GCC Intrinsic */
    return 31 - __builtin_clz (val);
#   else   /* Software version */
    static const unsigned DeBruijnClz[32] = { 0, 9, 1, 10, 13, 21, 2, 29, 11, 14, 16, 18, 22, 25, 3, 30, 8, 12, 20, 28, 15, 17, 24, 7, 19, 27, 23, 6, 26, 5, 4, 31 };
    U32 v = val;
    unsigned r;
    v |= v >> 1;
    v |= v >> 2;
    v |= v >> 4;
    v |= v >> 8;
    v |= v >> 16;
    r = DeBruijnClz[ (U32) (v * 0x07C4ACDDU) >> 27];
    return r;
#   endif
}


/****************************************************************
*  Templates
****************************************************************/
/*
  designed to be included
  for type-specific functions (template emulation in C)
  Objective is to write these functions only once, for improved maintenance
*/

/* safety checks */
#ifndef FSE_FUNCTION_EXTENSION
#  error "FSE_FUNCTION_EXTENSION must be defined"
#endif
#ifndef FSE_FUNCTION_TYPE
#  error "FSE_FUNCTION_TYPE must be defined"
#endif

/* Function names */
#define FSE_CAT(X,Y) X##Y
#define FSE_FUNCTION_NAME(X,Y) FSE_CAT(X,Y)
#define FSE_TYPE_NAME(X,Y) FSE_CAT(X,Y)



static U32 FSE_tableStep(U32 tableSize) { return (tableSize>>1) + (tableSize>>3) + 3; }

#define FSE_DECODE_TYPE FSE_decode_t


typedef struct {
    U16 tableLog;
    U16 fastMode;
} FSE_DTableHeader;   /* sizeof U32 */

static size_t FSE_buildDTable
(FSE_DTable* dt, const short* normalizedCounter, unsigned maxSymbolValue, unsigned tableLog)
{
    void* ptr = dt;
    FSE_DTableHeader* const DTableH = (FSE_DTableHeader*)ptr;
    FSE_DECODE_TYPE* const tableDecode = (FSE_DECODE_TYPE*)(ptr) + 1;   /* because dt is unsigned, 32-bits aligned on 32-bits */
    const U32 tableSize = 1 << tableLog;
    const U32 tableMask = tableSize-1;
    const U32 step = FSE_tableStep(tableSize);
    U16 symbolNext[FSE_MAX_SYMBOL_VALUE+1];
    U32 position = 0;
    U32 highThreshold = tableSize-1;
    const S16 largeLimit= (S16)(1 << (tableLog-1));
    U32 noLarge = 1;
    U32 s;

    /* Sanity Checks */
    if (maxSymbolValue > FSE_MAX_SYMBOL_VALUE) return (size_t)-FSE_ERROR_maxSymbolValue_tooLarge;
    if (tableLog > FSE_MAX_TABLELOG) return (size_t)-FSE_ERROR_tableLog_tooLarge;

    /* Init, lay down lowprob symbols */
    DTableH[0].tableLog = (U16)tableLog;
    for (s=0; s<=maxSymbolValue; s++)
    {
        if (normalizedCounter[s]==-1)
        {
            tableDecode[highThreshold--].symbol = (FSE_FUNCTION_TYPE)s;
            symbolNext[s] = 1;
        }
        else
        {
            if (normalizedCounter[s] >= largeLimit) noLarge=0;
            symbolNext[s] = normalizedCounter[s];
        }
    }

    /* Spread symbols */
    for (s=0; s<=maxSymbolValue; s++)
    {
        int i;
        for (i=0; i<normalizedCounter[s]; i++)
        {
            tableDecode[position].symbol = (FSE_FUNCTION_TYPE)s;
            position = (position + step) & tableMask;
            while (position > highThreshold) position = (position + step) & tableMask;   /* lowprob area */
        }
    }

    if (position!=0) return (size_t)-FSE_ERROR_GENERIC;   /* position must reach all cells once, otherwise normalizedCounter is incorrect */

    /* Build Decoding table */
    {
        U32 i;
        for (i=0; i<tableSize; i++)
        {
            FSE_FUNCTION_TYPE symbol = (FSE_FUNCTION_TYPE)(tableDecode[i].symbol);
            U16 nextState = symbolNext[symbol]++;
            tableDecode[i].nbBits = (BYTE) (tableLog - FSE_highbit32 ((U32)nextState) );
            tableDecode[i].newState = (U16) ( (nextState << tableDecode[i].nbBits) - tableSize);
        }
    }

    DTableH->fastMode = (U16)noLarge;
    return 0;
}


/******************************************
*  FSE byte symbol
******************************************/
#ifndef FSE_COMMONDEFS_ONLY

static unsigned FSE_isError(size_t code) { return (code > (size_t)(-FSE_ERROR_maxCode)); }

static short FSE_abs(short a)
{
    return a<0? -a : a;
}


/****************************************************************
*  Header bitstream management
****************************************************************/
static size_t FSE_readNCount (short* normalizedCounter, unsigned* maxSVPtr, unsigned* tableLogPtr,
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

    if (hbSize < 4) return (size_t)-FSE_ERROR_srcSize_wrong;
    bitStream = FSE_readLE32(ip);
    nbBits = (bitStream & 0xF) + FSE_MIN_TABLELOG;   /* extract tableLog */
    if (nbBits > FSE_TABLELOG_ABSOLUTE_MAX) return (size_t)-FSE_ERROR_tableLog_tooLarge;
    bitStream >>= 4;
    bitCount = 4;
    *tableLogPtr = nbBits;
    remaining = (1<<nbBits)+1;
    threshold = 1<<nbBits;
    nbBits++;

    while ((remaining>1) && (charnum<=*maxSVPtr))
    {
        if (previous0)
        {
            unsigned n0 = charnum;
            while ((bitStream & 0xFFFF) == 0xFFFF)
            {
                n0+=24;
                if (ip < iend-5)
                {
                    ip+=2;
                    bitStream = FSE_readLE32(ip) >> bitCount;
                }
                else
                {
                    bitStream >>= 16;
                    bitCount+=16;
                }
            }
            while ((bitStream & 3) == 3)
            {
                n0+=3;
                bitStream>>=2;
                bitCount+=2;
            }
            n0 += bitStream & 3;
            bitCount += 2;
            if (n0 > *maxSVPtr) return (size_t)-FSE_ERROR_maxSymbolValue_tooSmall;
            while (charnum < n0) normalizedCounter[charnum++] = 0;
            if ((ip <= iend-7) || (ip + (bitCount>>3) <= iend-4))
            {
                ip += bitCount>>3;
                bitCount &= 7;
                bitStream = FSE_readLE32(ip) >> bitCount;
            }
            else
                bitStream >>= 2;
        }
        {
            const short max = (short)((2*threshold-1)-remaining);
            short count;

            if ((bitStream & (threshold-1)) < (U32)max)
            {
                count = (short)(bitStream & (threshold-1));
                bitCount   += nbBits-1;
            }
            else
            {
                count = (short)(bitStream & (2*threshold-1));
                if (count >= threshold) count -= max;
                bitCount   += nbBits;
            }

            count--;   /* extra accuracy */
            remaining -= FSE_abs(count);
            normalizedCounter[charnum++] = count;
            previous0 = !count;
            while (remaining < threshold)
            {
                nbBits--;
                threshold >>= 1;
            }

            {
                if ((ip <= iend-7) || (ip + (bitCount>>3) <= iend-4))
                {
                    ip += bitCount>>3;
                    bitCount &= 7;
                }
                else
                {
                    bitCount -= (int)(8 * (iend - 4 - ip));
                    ip = iend - 4;
                }
                bitStream = FSE_readLE32(ip) >> (bitCount & 31);
            }
        }
    }
    if (remaining != 1) return (size_t)-FSE_ERROR_GENERIC;
    *maxSVPtr = charnum-1;

    ip += (bitCount+7)>>3;
    if ((size_t)(ip-istart) > hbSize) return (size_t)-FSE_ERROR_srcSize_wrong;
    return ip-istart;
}


/*********************************************************
*  Decompression (Byte symbols)
*********************************************************/
static size_t FSE_buildDTable_rle (FSE_DTable* dt, BYTE symbolValue)
{
    void* ptr = dt;
    FSE_DTableHeader* const DTableH = (FSE_DTableHeader*)ptr;
    FSE_decode_t* const cell = (FSE_decode_t*)(ptr) + 1;   /* because dt is unsigned */

    DTableH->tableLog = 0;
    DTableH->fastMode = 0;

    cell->newState = 0;
    cell->symbol = symbolValue;
    cell->nbBits = 0;

    return 0;
}


static size_t FSE_buildDTable_raw (FSE_DTable* dt, unsigned nbBits)
{
    void* ptr = dt;
    FSE_DTableHeader* const DTableH = (FSE_DTableHeader*)ptr;
    FSE_decode_t* const dinfo = (FSE_decode_t*)(ptr) + 1;   /* because dt is unsigned */
    const unsigned tableSize = 1 << nbBits;
    const unsigned tableMask = tableSize - 1;
    const unsigned maxSymbolValue = tableMask;
    unsigned s;

    /* Sanity checks */
    if (nbBits < 1) return (size_t)-FSE_ERROR_GENERIC;             /* min size */

    /* Build Decoding Table */
    DTableH->tableLog = (U16)nbBits;
    DTableH->fastMode = 1;
    for (s=0; s<=maxSymbolValue; s++)
    {
        dinfo[s].newState = 0;
        dinfo[s].symbol = (BYTE)s;
        dinfo[s].nbBits = (BYTE)nbBits;
    }

    return 0;
}


/* FSE_initDStream
 * Initialize a FSE_DStream_t.
 * srcBuffer must point at the beginning of an FSE block.
 * The function result is the size of the FSE_block (== srcSize).
 * If srcSize is too small, the function will return an errorCode;
 */
static size_t FSE_initDStream(FSE_DStream_t* bitD, const void* srcBuffer, size_t srcSize)
{
    if (srcSize < 1) return (size_t)-FSE_ERROR_srcSize_wrong;

    if (srcSize >=  sizeof(size_t))
    {
        U32 contain32;
        bitD->start = (const char*)srcBuffer;
        bitD->ptr   = (const char*)srcBuffer + srcSize - sizeof(size_t);
        bitD->bitContainer = FSE_readLEST(bitD->ptr);
        contain32 = ((const BYTE*)srcBuffer)[srcSize-1];
        if (contain32 == 0) return (size_t)-FSE_ERROR_GENERIC;   /* stop bit not present */
        bitD->bitsConsumed = 8 - FSE_highbit32(contain32);
    }
    else
    {
        U32 contain32;
        bitD->start = (const char*)srcBuffer;
        bitD->ptr   = bitD->start;
        bitD->bitContainer = *(const BYTE*)(bitD->start);
        switch(srcSize)
        {
            case 7: bitD->bitContainer += (size_t)(((const BYTE*)(bitD->start))[6]) << (sizeof(size_t)*8 - 16);
                    /* fallthrough */
            case 6: bitD->bitContainer += (size_t)(((const BYTE*)(bitD->start))[5]) << (sizeof(size_t)*8 - 24);
                    /* fallthrough */
            case 5: bitD->bitContainer += (size_t)(((const BYTE*)(bitD->start))[4]) << (sizeof(size_t)*8 - 32);
                    /* fallthrough */
            case 4: bitD->bitContainer += (size_t)(((const BYTE*)(bitD->start))[3]) << 24;
                    /* fallthrough */
            case 3: bitD->bitContainer += (size_t)(((const BYTE*)(bitD->start))[2]) << 16;
                    /* fallthrough */
            case 2: bitD->bitContainer += (size_t)(((const BYTE*)(bitD->start))[1]) <<  8;
                    /* fallthrough */
            default:;
        }
        contain32 = ((const BYTE*)srcBuffer)[srcSize-1];
        if (contain32 == 0) return (size_t)-FSE_ERROR_GENERIC;   /* stop bit not present */
        bitD->bitsConsumed = 8 - FSE_highbit32(contain32);
        bitD->bitsConsumed += (U32)(sizeof(size_t) - srcSize)*8;
    }

    return srcSize;
}


/*!FSE_lookBits
 * Provides next n bits from the bitContainer.
 * bitContainer is not modified (bits are still present for next read/look)
 * On 32-bits, maxNbBits==25
 * On 64-bits, maxNbBits==57
 * return : value extracted.
 */
static size_t FSE_lookBits(FSE_DStream_t* bitD, U32 nbBits)
{
    const U32 bitMask = sizeof(bitD->bitContainer)*8 - 1;
    return ((bitD->bitContainer << (bitD->bitsConsumed & bitMask)) >> 1) >> ((bitMask-nbBits) & bitMask);
}

static size_t FSE_lookBitsFast(FSE_DStream_t* bitD, U32 nbBits)   /* only if nbBits >= 1 !! */
{
    const U32 bitMask = sizeof(bitD->bitContainer)*8 - 1;
    return (bitD->bitContainer << (bitD->bitsConsumed & bitMask)) >> (((bitMask+1)-nbBits) & bitMask);
}

static void FSE_skipBits(FSE_DStream_t* bitD, U32 nbBits)
{
    bitD->bitsConsumed += nbBits;
}


/*!FSE_readBits
 * Read next n bits from the bitContainer.
 * On 32-bits, don't read more than maxNbBits==25
 * On 64-bits, don't read more than maxNbBits==57
 * Use the fast variant *only* if n >= 1.
 * return : value extracted.
 */
static size_t FSE_readBits(FSE_DStream_t* bitD, U32 nbBits)
{
    size_t value = FSE_lookBits(bitD, nbBits);
    FSE_skipBits(bitD, nbBits);
    return value;
}

static size_t FSE_readBitsFast(FSE_DStream_t* bitD, U32 nbBits)   /* only if nbBits >= 1 !! */
{
    size_t value = FSE_lookBitsFast(bitD, nbBits);
    FSE_skipBits(bitD, nbBits);
    return value;
}

static unsigned FSE_reloadDStream(FSE_DStream_t* bitD)
{
    if (bitD->bitsConsumed > (sizeof(bitD->bitContainer)*8))  /* should never happen */
        return FSE_DStream_tooFar;

    if (bitD->ptr >= bitD->start + sizeof(bitD->bitContainer))
    {
        bitD->ptr -= bitD->bitsConsumed >> 3;
        bitD->bitsConsumed &= 7;
        bitD->bitContainer = FSE_readLEST(bitD->ptr);
        return FSE_DStream_unfinished;
    }
    if (bitD->ptr == bitD->start)
    {
        if (bitD->bitsConsumed < sizeof(bitD->bitContainer)*8) return FSE_DStream_endOfBuffer;
        return FSE_DStream_completed;
    }
    {
        U32 nbBytes = bitD->bitsConsumed >> 3;
        U32 result = FSE_DStream_unfinished;
        if (bitD->ptr - nbBytes < bitD->start)
        {
            nbBytes = (U32)(bitD->ptr - bitD->start);  /* ptr > start */
            result = FSE_DStream_endOfBuffer;
        }
        bitD->ptr -= nbBytes;
        bitD->bitsConsumed -= nbBytes*8;
        bitD->bitContainer = FSE_readLEST(bitD->ptr);   /* reminder : srcSize > sizeof(bitD) */
        return result;
    }
}


static void FSE_initDState(FSE_DState_t* DStatePtr, FSE_DStream_t* bitD, const FSE_DTable* dt)
{
    const void* ptr = dt;
    const FSE_DTableHeader* const DTableH = (const FSE_DTableHeader*)ptr;
    DStatePtr->state = FSE_readBits(bitD, DTableH->tableLog);
    FSE_reloadDStream(bitD);
    DStatePtr->table = dt + 1;
}

static BYTE FSE_decodeSymbol(FSE_DState_t* DStatePtr, FSE_DStream_t* bitD)
{
    const FSE_decode_t DInfo = ((const FSE_decode_t*)(DStatePtr->table))[DStatePtr->state];
    const U32  nbBits = DInfo.nbBits;
    BYTE symbol = DInfo.symbol;
    size_t lowBits = FSE_readBits(bitD, nbBits);

    DStatePtr->state = DInfo.newState + lowBits;
    return symbol;
}

static BYTE FSE_decodeSymbolFast(FSE_DState_t* DStatePtr, FSE_DStream_t* bitD)
{
    const FSE_decode_t DInfo = ((const FSE_decode_t*)(DStatePtr->table))[DStatePtr->state];
    const U32 nbBits = DInfo.nbBits;
    BYTE symbol = DInfo.symbol;
    size_t lowBits = FSE_readBitsFast(bitD, nbBits);

    DStatePtr->state = DInfo.newState + lowBits;
    return symbol;
}

/* FSE_endOfDStream
   Tells if bitD has reached end of bitStream or not */

static unsigned FSE_endOfDStream(const FSE_DStream_t* bitD)
{
    return ((bitD->ptr == bitD->start) && (bitD->bitsConsumed == sizeof(bitD->bitContainer)*8));
}

static unsigned FSE_endOfDState(const FSE_DState_t* DStatePtr)
{
    return DStatePtr->state == 0;
}


FORCE_INLINE size_t FSE_decompress_usingDTable_generic(
          void* dst, size_t maxDstSize,
    const void* cSrc, size_t cSrcSize,
    const FSE_DTable* dt, const unsigned fast)
{
    BYTE* const ostart = (BYTE*) dst;
    BYTE* op = ostart;
    BYTE* const omax = op + maxDstSize;
    BYTE* const olimit = omax-3;

    FSE_DStream_t bitD;
    FSE_DState_t state1;
    FSE_DState_t state2;
    size_t errorCode;

    /* Init */
    errorCode = FSE_initDStream(&bitD, cSrc, cSrcSize);   /* replaced last arg by maxCompressed Size */
    if (FSE_isError(errorCode)) return errorCode;

    FSE_initDState(&state1, &bitD, dt);
    FSE_initDState(&state2, &bitD, dt);

#define FSE_GETSYMBOL(statePtr) fast ? FSE_decodeSymbolFast(statePtr, &bitD) : FSE_decodeSymbol(statePtr, &bitD)

    /* 4 symbols per loop */
    for ( ; (FSE_reloadDStream(&bitD)==FSE_DStream_unfinished) && (op<olimit) ; op+=4)
    {
        op[0] = FSE_GETSYMBOL(&state1);

        if (FSE_MAX_TABLELOG*2+7 > sizeof(bitD.bitContainer)*8)    /* This test must be static */
            FSE_reloadDStream(&bitD);

        op[1] = FSE_GETSYMBOL(&state2);

        if (FSE_MAX_TABLELOG*4+7 > sizeof(bitD.bitContainer)*8)    /* This test must be static */
            { if (FSE_reloadDStream(&bitD) > FSE_DStream_unfinished) { op+=2; break; } }

        op[2] = FSE_GETSYMBOL(&state1);

        if (FSE_MAX_TABLELOG*2+7 > sizeof(bitD.bitContainer)*8)    /* This test must be static */
            FSE_reloadDStream(&bitD);

        op[3] = FSE_GETSYMBOL(&state2);
    }

    /* tail */
    /* note : FSE_reloadDStream(&bitD) >= FSE_DStream_partiallyFilled; Ends at exactly FSE_DStream_completed */
    while (1)
    {
        if ( (FSE_reloadDStream(&bitD)>FSE_DStream_completed) || (op==omax) || (FSE_endOfDStream(&bitD) && (fast || FSE_endOfDState(&state1))) )
            break;

        *op++ = FSE_GETSYMBOL(&state1);

        if ( (FSE_reloadDStream(&bitD)>FSE_DStream_completed) || (op==omax) || (FSE_endOfDStream(&bitD) && (fast || FSE_endOfDState(&state2))) )
            break;

        *op++ = FSE_GETSYMBOL(&state2);
    }

    /* end ? */
    if (FSE_endOfDStream(&bitD) && FSE_endOfDState(&state1) && FSE_endOfDState(&state2))
        return op-ostart;

    if (op==omax) return (size_t)-FSE_ERROR_dstSize_tooSmall;   /* dst buffer is full, but cSrc unfinished */

    return (size_t)-FSE_ERROR_corruptionDetected;
}


static size_t FSE_decompress_usingDTable(void* dst, size_t originalSize,
                            const void* cSrc, size_t cSrcSize,
                            const FSE_DTable* dt)
{
    FSE_DTableHeader DTableH;
    memcpy(&DTableH, dt, sizeof(DTableH));   /* memcpy() into local variable, to avoid strict aliasing warning */

    /* select fast mode (static) */
    if (DTableH.fastMode) return FSE_decompress_usingDTable_generic(dst, originalSize, cSrc, cSrcSize, dt, 1);
    return FSE_decompress_usingDTable_generic(dst, originalSize, cSrc, cSrcSize, dt, 0);
}


static size_t FSE_decompress(void* dst, size_t maxDstSize, const void* cSrc, size_t cSrcSize)
{
    const BYTE* const istart = (const BYTE*)cSrc;
    const BYTE* ip = istart;
    short counting[FSE_MAX_SYMBOL_VALUE+1];
    DTable_max_t dt;   /* Static analyzer seems unable to understand this table will be properly initialized later */
    unsigned tableLog;
    unsigned maxSymbolValue = FSE_MAX_SYMBOL_VALUE;
    size_t errorCode;

    if (cSrcSize<2) return (size_t)-FSE_ERROR_srcSize_wrong;   /* too small input size */

    /* normal FSE decoding mode */
    errorCode = FSE_readNCount (counting, &maxSymbolValue, &tableLog, istart, cSrcSize);
    if (FSE_isError(errorCode)) return errorCode;
    if (errorCode >= cSrcSize) return (size_t)-FSE_ERROR_srcSize_wrong;   /* too small input size */
    ip += errorCode;
    cSrcSize -= errorCode;

    errorCode = FSE_buildDTable (dt, counting, maxSymbolValue, tableLog);
    if (FSE_isError(errorCode)) return errorCode;

    /* always return, even if it is an error code */
    return FSE_decompress_usingDTable (dst, maxDstSize, ip, cSrcSize, dt);
}



/* *******************************************************
*  Huff0 : Huffman block compression
*********************************************************/
#define HUF_MAX_SYMBOL_VALUE 255
#define HUF_DEFAULT_TABLELOG  12       /* used by default, when not specified */
#define HUF_MAX_TABLELOG  12           /* max possible tableLog; for allocation purpose; can be modified */
#define HUF_ABSOLUTEMAX_TABLELOG  16   /* absolute limit of HUF_MAX_TABLELOG. Beyond that value, code does not work */
#if (HUF_MAX_TABLELOG > HUF_ABSOLUTEMAX_TABLELOG)
#  error "HUF_MAX_TABLELOG is too large !"
#endif

typedef struct HUF_CElt_s {
  U16  val;
  BYTE nbBits;
} HUF_CElt ;

typedef struct nodeElt_s {
    U32 count;
    U16 parent;
    BYTE byte;
    BYTE nbBits;
} nodeElt;


/* *******************************************************
*  Huff0 : Huffman block decompression
*********************************************************/
typedef struct {
    BYTE byte;
    BYTE nbBits;
} HUF_DElt;

static size_t HUF_readDTable (U16* DTable, const void* src, size_t srcSize)
{
    BYTE huffWeight[HUF_MAX_SYMBOL_VALUE + 1];
    U32 rankVal[HUF_ABSOLUTEMAX_TABLELOG + 1];  /* large enough for values from 0 to 16 */
    U32 weightTotal;
    U32 maxBits;
    const BYTE* ip = (const BYTE*) src;
    size_t iSize;
    size_t oSize;
    U32 n;
    U32 nextRankStart;
    void* ptr = DTable+1;
    HUF_DElt* const dt = (HUF_DElt*)ptr;

    if (!srcSize) return (size_t)-FSE_ERROR_srcSize_wrong;
    iSize = ip[0];

    FSE_STATIC_ASSERT(sizeof(HUF_DElt) == sizeof(U16));   /* if compilation fails here, assertion is false */
    //memset(huffWeight, 0, sizeof(huffWeight));   /* should not be necessary, but some analyzer complain ... */
    if (iSize >= 128)  /* special header */
    {
        if (iSize >= (242))   /* RLE */
        {
            static int l[14] = { 1, 2, 3, 4, 7, 8, 15, 16, 31, 32, 63, 64, 127, 128 };
            oSize = l[iSize-242];
            memset(huffWeight, 1, sizeof(huffWeight));
            iSize = 0;
        }
        else   /* Incompressible */
        {
            oSize = iSize - 127;
            iSize = ((oSize+1)/2);
            if (iSize+1 > srcSize) return (size_t)-FSE_ERROR_srcSize_wrong;
            ip += 1;
            for (n=0; n<oSize; n+=2)
            {
                huffWeight[n]   = ip[n/2] >> 4;
                huffWeight[n+1] = ip[n/2] & 15;
            }
        }
    }
    else  /* header compressed with FSE (normal case) */
    {
        if (iSize+1 > srcSize) return (size_t)-FSE_ERROR_srcSize_wrong;
        oSize = FSE_decompress(huffWeight, HUF_MAX_SYMBOL_VALUE, ip+1, iSize);   /* max 255 values decoded, last one is implied */
        if (FSE_isError(oSize)) return oSize;
    }

    /* collect weight stats */
    memset(rankVal, 0, sizeof(rankVal));
    weightTotal = 0;
    for (n=0; n<oSize; n++)
    {
        if (huffWeight[n] >= HUF_ABSOLUTEMAX_TABLELOG) return (size_t)-FSE_ERROR_corruptionDetected;
        rankVal[huffWeight[n]]++;
        weightTotal += (1 << huffWeight[n]) >> 1;
    }
    if (weightTotal == 0) return (size_t)-FSE_ERROR_corruptionDetected;

    /* get last non-null symbol weight (implied, total must be 2^n) */
    maxBits = FSE_highbit32(weightTotal) + 1;
    if (maxBits > DTable[0]) return (size_t)-FSE_ERROR_tableLog_tooLarge;   /* DTable is too small */
    DTable[0] = (U16)maxBits;
    {
        U32 total = 1 << maxBits;
        U32 rest = total - weightTotal;
        U32 verif = 1 << FSE_highbit32(rest);
        U32 lastWeight = FSE_highbit32(rest) + 1;
        if (verif != rest) return (size_t)-FSE_ERROR_corruptionDetected;    /* last value must be a clean power of 2 */
        huffWeight[oSize] = (BYTE)lastWeight;
        rankVal[lastWeight]++;
    }

    /* check tree construction validity */
    if ((rankVal[1] < 2) || (rankVal[1] & 1)) return (size_t)-FSE_ERROR_corruptionDetected;   /* by construction : at least 2 elts of rank 1, must be even */

    /* Prepare ranks */
    nextRankStart = 0;
    for (n=1; n<=maxBits; n++)
    {
        U32 current = nextRankStart;
        nextRankStart += (rankVal[n] << (n-1));
        rankVal[n] = current;
    }

    /* fill DTable */
    for (n=0; n<=oSize; n++)
    {
        const U32 w = huffWeight[n];
        const U32 length = (1 << w) >> 1;
        U32 i;
        HUF_DElt D;
        D.byte = (BYTE)n; D.nbBits = (BYTE)(maxBits + 1 - w);
        for (i = rankVal[w]; i < rankVal[w] + length; i++)
            dt[i] = D;
        rankVal[w] += length;
    }

    return iSize+1;
}


static BYTE HUF_decodeSymbol(FSE_DStream_t* Dstream, const HUF_DElt* dt, const U32 dtLog)
{
        const size_t val = FSE_lookBitsFast(Dstream, dtLog); /* note : dtLog >= 1 */
        const BYTE c = dt[val].byte;
        FSE_skipBits(Dstream, dt[val].nbBits);
        return c;
}

static size_t HUF_decompress_usingDTable(   /* -3% slower when non static */
          void* dst, size_t maxDstSize,
    const void* cSrc, size_t cSrcSize,
    const U16* DTable)
{
    BYTE* const ostart = (BYTE*) dst;
    BYTE* op = ostart;
    BYTE* const omax = op + maxDstSize;
    BYTE* const olimit = omax-15;

    const void* ptr = DTable;
    const HUF_DElt* const dt = (const HUF_DElt*)(ptr)+1;
    const U32 dtLog = DTable[0];
    size_t errorCode;
    U32 reloadStatus;

    /* Init */

    const U16* jumpTable = (const U16*)cSrc;
    const size_t length1 = FSE_readLE16(jumpTable);
    const size_t length2 = FSE_readLE16(jumpTable+1);
    const size_t length3 = FSE_readLE16(jumpTable+2);
    const size_t length4 = cSrcSize - 6 - length1 - length2 - length3;   // check coherency !!
    const char* const start1 = (const char*)(cSrc) + 6;
    const char* const start2 = start1 + length1;
    const char* const start3 = start2 + length2;
    const char* const start4 = start3 + length3;
    FSE_DStream_t bitD1, bitD2, bitD3, bitD4;

    if (length1+length2+length3+6 >= cSrcSize) return (size_t)-FSE_ERROR_srcSize_wrong;

    errorCode = FSE_initDStream(&bitD1, start1, length1);
    if (FSE_isError(errorCode)) return errorCode;
    errorCode = FSE_initDStream(&bitD2, start2, length2);
    if (FSE_isError(errorCode)) return errorCode;
    errorCode = FSE_initDStream(&bitD3, start3, length3);
    if (FSE_isError(errorCode)) return errorCode;
    errorCode = FSE_initDStream(&bitD4, start4, length4);
    if (FSE_isError(errorCode)) return errorCode;

    reloadStatus=FSE_reloadDStream(&bitD2);

    /* 16 symbols per loop */
    for ( ; (reloadStatus<FSE_DStream_completed) && (op<olimit);  /* D2-3-4 are supposed to be synchronized and finish together */
        op+=16, reloadStatus = FSE_reloadDStream(&bitD2) | FSE_reloadDStream(&bitD3) | FSE_reloadDStream(&bitD4), FSE_reloadDStream(&bitD1))
    {
#define HUF_DECODE_SYMBOL_0(n, Dstream) \
        op[n] = HUF_decodeSymbol(&Dstream, dt, dtLog);

#define HUF_DECODE_SYMBOL_1(n, Dstream) \
        op[n] = HUF_decodeSymbol(&Dstream, dt, dtLog); \
        if (FSE_32bits() && (HUF_MAX_TABLELOG>12)) FSE_reloadDStream(&Dstream)

#define HUF_DECODE_SYMBOL_2(n, Dstream) \
        op[n] = HUF_decodeSymbol(&Dstream, dt, dtLog); \
        if (FSE_32bits()) FSE_reloadDStream(&Dstream)

        HUF_DECODE_SYMBOL_1( 0, bitD1);
        HUF_DECODE_SYMBOL_1( 1, bitD2);
        HUF_DECODE_SYMBOL_1( 2, bitD3);
        HUF_DECODE_SYMBOL_1( 3, bitD4);
        HUF_DECODE_SYMBOL_2( 4, bitD1);
        HUF_DECODE_SYMBOL_2( 5, bitD2);
        HUF_DECODE_SYMBOL_2( 6, bitD3);
        HUF_DECODE_SYMBOL_2( 7, bitD4);
        HUF_DECODE_SYMBOL_1( 8, bitD1);
        HUF_DECODE_SYMBOL_1( 9, bitD2);
        HUF_DECODE_SYMBOL_1(10, bitD3);
        HUF_DECODE_SYMBOL_1(11, bitD4);
        HUF_DECODE_SYMBOL_0(12, bitD1);
        HUF_DECODE_SYMBOL_0(13, bitD2);
        HUF_DECODE_SYMBOL_0(14, bitD3);
        HUF_DECODE_SYMBOL_0(15, bitD4);
    }

    if (reloadStatus!=FSE_DStream_completed)   /* not complete : some bitStream might be FSE_DStream_unfinished */
        return (size_t)-FSE_ERROR_corruptionDetected;

    /* tail */
    {
        // bitTail = bitD1;   // *much* slower : -20% !??!
        FSE_DStream_t bitTail;
        bitTail.ptr = bitD1.ptr;
        bitTail.bitsConsumed = bitD1.bitsConsumed;
        bitTail.bitContainer = bitD1.bitContainer;   // required in case of FSE_DStream_endOfBuffer
        bitTail.start = start1;
        for ( ; (FSE_reloadDStream(&bitTail) < FSE_DStream_completed) && (op<omax) ; op++)
        {
            HUF_DECODE_SYMBOL_0(0, bitTail);
        }

        if (FSE_endOfDStream(&bitTail))
            return op-ostart;
    }

    if (op==omax) return (size_t)-FSE_ERROR_dstSize_tooSmall;   /* dst buffer is full, but cSrc unfinished */

    return (size_t)-FSE_ERROR_corruptionDetected;
}


static size_t HUF_decompress (void* dst, size_t maxDstSize, const void* cSrc, size_t cSrcSize)
{
    HUF_CREATE_STATIC_DTABLE(DTable, HUF_MAX_TABLELOG);
    const BYTE* ip = (const BYTE*) cSrc;
    size_t errorCode;

    errorCode = HUF_readDTable (DTable, cSrc, cSrcSize);
    if (FSE_isError(errorCode)) return errorCode;
    if (errorCode >= cSrcSize) return (size_t)-FSE_ERROR_srcSize_wrong;
    ip += errorCode;
    cSrcSize -= errorCode;

    return HUF_decompress_usingDTable (dst, maxDstSize, ip, cSrcSize, DTable);
}


#endif   /* FSE_COMMONDEFS_ONLY */

/*
    zstd - standard compression library
    Copyright (C) 2014-2015, Yann Collet.

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
    - zstd source repository : https://github.com/Cyan4973/zstd
    - ztsd public forum : https://groups.google.com/forum/#!forum/lz4c
*/

/****************************************************************
*  Tuning parameters
*****************************************************************/
/* MEMORY_USAGE :
*  Memory usage formula : N->2^N Bytes (examples : 10 -> 1KB; 12 -> 4KB ; 16 -> 64KB; 20 -> 1MB; etc.)
*  Increasing memory usage improves compression ratio
*  Reduced memory usage can improve speed, due to cache effect */
#define ZSTD_MEMORY_USAGE 17


/**************************************
   CPU Feature Detection
**************************************/
/*
 * Automated efficient unaligned memory access detection
 * Based on known hardware architectures
 * This list will be updated thanks to feedbacks
 */
#if defined(CPU_HAS_EFFICIENT_UNALIGNED_MEMORY_ACCESS) \
    || defined(__ARM_FEATURE_UNALIGNED) \
    || defined(__i386__) || defined(__x86_64__) \
    || defined(_M_IX86) || defined(_M_X64) \
    || defined(__ARM_ARCH_7__) || defined(__ARM_ARCH_8__) \
    || (defined(_M_ARM) && (_M_ARM >= 7))
#  define ZSTD_UNALIGNED_ACCESS 1
#else
#  define ZSTD_UNALIGNED_ACCESS 0
#endif


/********************************************************
*  Includes
*********************************************************/
#include <stdlib.h>      /* calloc */
#include <string.h>      /* memcpy, memmove */
#include <stdio.h>       /* debug : printf */


/********************************************************
*  Compiler specifics
*********************************************************/
#ifdef __AVX2__
#  include <immintrin.h>   /* AVX2 intrinsics */
#endif

#ifdef _MSC_VER    /* Visual Studio */
#  include <intrin.h>                    /* For Visual 2005 */
#  pragma warning(disable : 4127)        /* disable: C4127: conditional expression is constant */
#  pragma warning(disable : 4324)        /* disable: C4324: padded structure */
#endif


#ifndef MEM_ACCESS_MODULE
#define MEM_ACCESS_MODULE
/********************************************************
*  Basic Types
*********************************************************/
#if defined (__STDC_VERSION__) && __STDC_VERSION__ >= 199901L   /* C99 */
# include <stdint.h>
typedef  uint8_t BYTE;
typedef uint16_t U16;
typedef  int16_t S16;
typedef uint32_t U32;
typedef  int32_t S32;
typedef uint64_t U64;
#else
typedef unsigned char       BYTE;
typedef unsigned short      U16;
typedef   signed short      S16;
typedef unsigned int        U32;
typedef   signed int        S32;
typedef unsigned long long  U64;
#endif

#endif   /* MEM_ACCESS_MODULE */


/********************************************************
*  Constants
*********************************************************/
static const U32 ZSTD_magicNumber = 0xFD2FB51E;   /* 3rd version : seqNb header */

#define HASH_LOG (ZSTD_MEMORY_USAGE - 2)
#define HASH_TABLESIZE (1 << HASH_LOG)
#define HASH_MASK (HASH_TABLESIZE - 1)

#define KNUTH 2654435761

#define BIT7 128
#define BIT6  64
#define BIT5  32
#define BIT4  16

#define KB *(1 <<10)
#define MB *(1 <<20)
#define GB *(1U<<30)

#define BLOCKSIZE (128 KB)                 /* define, for static allocation */

#define WORKPLACESIZE (BLOCKSIZE*3)
#define MINMATCH 4
#define MLbits   7
#define LLbits   6
#define Offbits  5
#define MaxML  ((1<<MLbits )-1)
#define MaxLL  ((1<<LLbits )-1)
#define MaxOff ((1<<Offbits)-1)
#define LitFSELog  11
#define MLFSELog   10
#define LLFSELog   10
#define OffFSELog   9
#define MAX(a,b) ((a)<(b)?(b):(a))
#define MaxSeq MAX(MaxLL, MaxML)

#define LITERAL_NOENTROPY 63
#define COMMAND_NOENTROPY 7   /* to remove */

static const size_t ZSTD_blockHeaderSize = 3;
static const size_t ZSTD_frameHeaderSize = 4;


/********************************************************
*  Memory operations
*********************************************************/
static unsigned ZSTD_32bits(void) { return sizeof(void*)==4; }

static unsigned ZSTD_isLittleEndian(void)
{
    const union { U32 i; BYTE c[4]; } one = { 1 };   /* don't use static : performance detrimental  */
    return one.c[0];
}

static U16    ZSTD_read16(const void* p) { U16 r; memcpy(&r, p, sizeof(r)); return r; }

static U32    ZSTD_read32(const void* p) { U32 r; memcpy(&r, p, sizeof(r)); return r; }

static void   ZSTD_copy4(void* dst, const void* src) { memcpy(dst, src, 4); }

static void   ZSTD_copy8(void* dst, const void* src) { memcpy(dst, src, 8); }

#define COPY8(d,s)    { ZSTD_copy8(d,s); d+=8; s+=8; }

static void ZSTD_wildcopy(void* dst, const void* src, ptrdiff_t length)
{
    const BYTE* ip = (const BYTE*)src;
    BYTE* op = (BYTE*)dst;
    BYTE* const oend = op + length;
    while (op < oend) COPY8(op, ip);
}

static U16 ZSTD_readLE16(const void* memPtr)
{
    if (ZSTD_isLittleEndian()) return ZSTD_read16(memPtr);
    else
    {
        const BYTE* p = (const BYTE*)memPtr;
        return (U16)((U16)p[0] + ((U16)p[1]<<8));
    }
}


static U32 ZSTD_readLE32(const void* memPtr)
{
    if (ZSTD_isLittleEndian())
        return ZSTD_read32(memPtr);
    else
    {
        const BYTE* p = (const BYTE*)memPtr;
        return (U32)((U32)p[0] + ((U32)p[1]<<8) + ((U32)p[2]<<16) + ((U32)p[3]<<24));
    }
}

static U32 ZSTD_readBE32(const void* memPtr)
{
    const BYTE* p = (const BYTE*)memPtr;
    return (U32)(((U32)p[0]<<24) + ((U32)p[1]<<16) + ((U32)p[2]<<8) + ((U32)p[3]<<0));
}


/**************************************
*  Local structures
***************************************/
typedef struct ZSTD_Cctx_s ZSTD_Cctx;

typedef enum { bt_compressed, bt_raw, bt_rle, bt_end } blockType_t;

typedef struct
{
    blockType_t blockType;
    U32 origSize;
} blockProperties_t;

typedef struct {
    void* buffer;
    U32*  offsetStart;
    U32*  offset;
    BYTE* offCodeStart;
    BYTE* offCode;
    BYTE* litStart;
    BYTE* lit;
    BYTE* litLengthStart;
    BYTE* litLength;
    BYTE* matchLengthStart;
    BYTE* matchLength;
    BYTE* dumpsStart;
    BYTE* dumps;
} seqStore_t;


typedef struct ZSTD_Cctx_s
{
    const BYTE* base;
    U32 current;
    U32 nextUpdate;
    seqStore_t seqStore;
#ifdef __AVX2__
    __m256i hashTable[HASH_TABLESIZE>>3];
#else
    U32 hashTable[HASH_TABLESIZE];
#endif
    BYTE buffer[WORKPLACESIZE];
} cctxi_t;




/**************************************
*  Error Management
**************************************/
/* published entry point */
unsigned ZSTDv01_isError(size_t code) { return ERR_isError(code); }


/**************************************
*  Tool functions
**************************************/
#define ZSTD_VERSION_MAJOR    0    /* for breaking interface changes  */
#define ZSTD_VERSION_MINOR    1    /* for new (non-breaking) interface capabilities */
#define ZSTD_VERSION_RELEASE  3    /* for tweaks, bug-fixes, or development */
#define ZSTD_VERSION_NUMBER  (ZSTD_VERSION_MAJOR *100*100 + ZSTD_VERSION_MINOR *100 + ZSTD_VERSION_RELEASE)

/**************************************************************
*   Decompression code
**************************************************************/

static size_t ZSTDv01_getcBlockSize(const void* src, size_t srcSize, blockProperties_t* bpPtr)
{
    const BYTE* const in = (const BYTE* const)src;
    BYTE headerFlags;
    U32 cSize;

    if (srcSize < 3) return ERROR(srcSize_wrong);

    headerFlags = *in;
    cSize = in[2] + (in[1]<<8) + ((in[0] & 7)<<16);

    bpPtr->blockType = (blockType_t)(headerFlags >> 6);
    bpPtr->origSize = (bpPtr->blockType == bt_rle) ? cSize : 0;

    if (bpPtr->blockType == bt_end) return 0;
    if (bpPtr->blockType == bt_rle) return 1;
    return cSize;
}


static size_t ZSTD_copyUncompressedBlock(void* dst, size_t maxDstSize, const void* src, size_t srcSize)
{
    if (srcSize > maxDstSize) return ERROR(dstSize_tooSmall);
    memcpy(dst, src, srcSize);
    return srcSize;
}


static size_t ZSTD_decompressLiterals(void* ctx,
                                      void* dst, size_t maxDstSize,
                                const void* src, size_t srcSize)
{
    BYTE* op = (BYTE*)dst;
    BYTE* const oend = op + maxDstSize;
    const BYTE* ip = (const BYTE*)src;
    size_t errorCode;
    size_t litSize;

    /* check : minimum 2, for litSize, +1, for content */
    if (srcSize <= 3) return ERROR(corruption_detected);

    litSize = ip[1] + (ip[0]<<8);
    litSize += ((ip[-3] >> 3) & 7) << 16;   // mmmmh....
    op = oend - litSize;

    (void)ctx;
    if (litSize > maxDstSize) return ERROR(dstSize_tooSmall);
    errorCode = HUF_decompress(op, litSize, ip+2, srcSize-2);
    if (FSE_isError(errorCode)) return ERROR(GENERIC);
    return litSize;
}


static size_t ZSTDv01_decodeLiteralsBlock(void* ctx,
                                void* dst, size_t maxDstSize,
                          const BYTE** litStart, size_t* litSize,
                          const void* src, size_t srcSize)
{
    const BYTE* const istart = (const BYTE* const)src;
    const BYTE* ip = istart;
    BYTE* const ostart = (BYTE* const)dst;
    BYTE* const oend = ostart + maxDstSize;
    blockProperties_t litbp;

    size_t litcSize = ZSTDv01_getcBlockSize(src, srcSize, &litbp);
    if (ZSTDv01_isError(litcSize)) return litcSize;
    if (litcSize > srcSize - ZSTD_blockHeaderSize) return ERROR(srcSize_wrong);
    ip += ZSTD_blockHeaderSize;

    switch(litbp.blockType)
    {
    case bt_raw:
        *litStart = ip;
        ip += litcSize;
        *litSize = litcSize;
        break;
    case bt_rle:
        {
            size_t rleSize = litbp.origSize;
            if (rleSize>maxDstSize) return ERROR(dstSize_tooSmall);
            if (!srcSize) return ERROR(srcSize_wrong);
            memset(oend - rleSize, *ip, rleSize);
            *litStart = oend - rleSize;
            *litSize = rleSize;
            ip++;
            break;
        }
    case bt_compressed:
        {
            size_t decodedLitSize = ZSTD_decompressLiterals(ctx, dst, maxDstSize, ip, litcSize);
            if (ZSTDv01_isError(decodedLitSize)) return decodedLitSize;
            *litStart = oend - decodedLitSize;
            *litSize = decodedLitSize;
            ip += litcSize;
            break;
        }
    case bt_end:
    default:
        return ERROR(GENERIC);
    }

    return ip-istart;
}


static size_t ZSTDv01_decodeSeqHeaders(int* nbSeq, const BYTE** dumpsPtr, size_t* dumpsLengthPtr,
                         FSE_DTable* DTableLL, FSE_DTable* DTableML, FSE_DTable* DTableOffb,
                         const void* src, size_t srcSize)
{
    const BYTE* const istart = (const BYTE* const)src;
    const BYTE* ip = istart;
    const BYTE* const iend = istart + srcSize;
    U32 LLtype, Offtype, MLtype;
    U32 LLlog, Offlog, MLlog;
    size_t dumpsLength;

    /* check */
    if (srcSize < 5) return ERROR(srcSize_wrong);

    /* SeqHead */
    *nbSeq = ZSTD_readLE16(ip); ip+=2;
    LLtype  = *ip >> 6;
    Offtype = (*ip >> 4) & 3;
    MLtype  = (*ip >> 2) & 3;
    if (*ip & 2)
    {
        dumpsLength  = ip[2];
        dumpsLength += ip[1] << 8;
        ip += 3;
    }
    else
    {
        dumpsLength  = ip[1];
        dumpsLength += (ip[0] & 1) << 8;
        ip += 2;
    }
    *dumpsPtr = ip;
    ip += dumpsLength;
    *dumpsLengthPtr = dumpsLength;

    /* check */
    if (ip > iend-3) return ERROR(srcSize_wrong); /* min : all 3 are "raw", hence no header, but at least xxLog bits per type */

    /* sequences */
    {
        S16 norm[MaxML+1];    /* assumption : MaxML >= MaxLL and MaxOff */
        size_t headerSize;

        /* Build DTables */
        switch(LLtype)
        {
        case bt_rle :
            LLlog = 0;
            FSE_buildDTable_rle(DTableLL, *ip++); break;
        case bt_raw :
            LLlog = LLbits;
            FSE_buildDTable_raw(DTableLL, LLbits); break;
        default :
            {   U32 max = MaxLL;
                headerSize = FSE_readNCount(norm, &max, &LLlog, ip, iend-ip);
                if (FSE_isError(headerSize)) return ERROR(GENERIC);
                if (LLlog > LLFSELog) return ERROR(corruption_detected);
                ip += headerSize;
                FSE_buildDTable(DTableLL, norm, max, LLlog);
        }   }

        switch(Offtype)
        {
        case bt_rle :
            Offlog = 0;
            if (ip > iend-2) return ERROR(srcSize_wrong); /* min : "raw", hence no header, but at least xxLog bits */
            FSE_buildDTable_rle(DTableOffb, *ip++); break;
        case bt_raw :
            Offlog = Offbits;
            FSE_buildDTable_raw(DTableOffb, Offbits); break;
        default :
            {   U32 max = MaxOff;
                headerSize = FSE_readNCount(norm, &max, &Offlog, ip, iend-ip);
                if (FSE_isError(headerSize)) return ERROR(GENERIC);
                if (Offlog > OffFSELog) return ERROR(corruption_detected);
                ip += headerSize;
                FSE_buildDTable(DTableOffb, norm, max, Offlog);
        }   }

        switch(MLtype)
        {
        case bt_rle :
            MLlog = 0;
            if (ip > iend-2) return ERROR(srcSize_wrong); /* min : "raw", hence no header, but at least xxLog bits */
            FSE_buildDTable_rle(DTableML, *ip++); break;
        case bt_raw :
            MLlog = MLbits;
            FSE_buildDTable_raw(DTableML, MLbits); break;
        default :
            {   U32 max = MaxML;
                headerSize = FSE_readNCount(norm, &max, &MLlog, ip, iend-ip);
                if (FSE_isError(headerSize)) return ERROR(GENERIC);
                if (MLlog > MLFSELog) return ERROR(corruption_detected);
                ip += headerSize;
                FSE_buildDTable(DTableML, norm, max, MLlog);
    }   }   }

    return ip-istart;
}


typedef struct {
    size_t litLength;
    size_t offset;
    size_t matchLength;
} seq_t;

typedef struct {
    FSE_DStream_t DStream;
    FSE_DState_t stateLL;
    FSE_DState_t stateOffb;
    FSE_DState_t stateML;
    size_t prevOffset;
    const BYTE* dumps;
    const BYTE* dumpsEnd;
} seqState_t;


static void ZSTD_decodeSequence(seq_t* seq, seqState_t* seqState)
{
    size_t litLength;
    size_t prevOffset;
    size_t offset;
    size_t matchLength;
    const BYTE* dumps = seqState->dumps;
    const BYTE* const de = seqState->dumpsEnd;

    /* Literal length */
    litLength = FSE_decodeSymbol(&(seqState->stateLL), &(seqState->DStream));
    prevOffset = litLength ? seq->offset : seqState->prevOffset;
    seqState->prevOffset = seq->offset;
    if (litLength == MaxLL)
    {
        U32 add = dumps<de ? *dumps++ : 0;
        if (add < 255) litLength += add;
        else
        {
            if (dumps<=(de-3))
            {
                litLength = ZSTD_readLE32(dumps) & 0xFFFFFF;  /* no pb : dumps is always followed by seq tables > 1 byte */
                dumps += 3;
            }
        }
    }

    /* Offset */
    {
        U32 offsetCode, nbBits;
        offsetCode = FSE_decodeSymbol(&(seqState->stateOffb), &(seqState->DStream));
        if (ZSTD_32bits()) FSE_reloadDStream(&(seqState->DStream));
        nbBits = offsetCode - 1;
        if (offsetCode==0) nbBits = 0;   /* cmove */
        offset = ((size_t)1 << (nbBits & ((sizeof(offset)*8)-1))) + FSE_readBits(&(seqState->DStream), nbBits);
        if (ZSTD_32bits()) FSE_reloadDStream(&(seqState->DStream));
        if (offsetCode==0) offset = prevOffset;
    }

    /* MatchLength */
    matchLength = FSE_decodeSymbol(&(seqState->stateML), &(seqState->DStream));
    if (matchLength == MaxML)
    {
        U32 add = dumps<de ? *dumps++ : 0;
        if (add < 255) matchLength += add;
        else
        {
            if (dumps<=(de-3))
            {
                matchLength = ZSTD_readLE32(dumps) & 0xFFFFFF;  /* no pb : dumps is always followed by seq tables > 1 byte */
                dumps += 3;
            }
        }
    }
    matchLength += MINMATCH;

    /* save result */
    seq->litLength = litLength;
    seq->offset = offset;
    seq->matchLength = matchLength;
    seqState->dumps = dumps;
}


static size_t ZSTD_execSequence(BYTE* op,
                                seq_t sequence,
                                const BYTE** litPtr, const BYTE* const litLimit,
                                BYTE* const base, BYTE* const oend)
{
    static const int dec32table[] = {0, 1, 2, 1, 4, 4, 4, 4};   /* added */
    static const int dec64table[] = {8, 8, 8, 7, 8, 9,10,11};   /* substracted */
    const BYTE* const ostart = op;
    const size_t litLength = sequence.litLength;
    BYTE* const endMatch = op + litLength + sequence.matchLength;    /* risk : address space overflow (32-bits) */
    const BYTE* const litEnd = *litPtr + litLength;

    /* check */
    if (endMatch > oend) return ERROR(dstSize_tooSmall);   /* overwrite beyond dst buffer */
    if (litEnd > litLimit) return ERROR(corruption_detected);
    if (sequence.matchLength > (size_t)(*litPtr-op))  return ERROR(dstSize_tooSmall);    /* overwrite literal segment */

    /* copy Literals */
    if (((size_t)(*litPtr - op) < 8) || ((size_t)(oend-litEnd) < 8) || (op+litLength > oend-8))
        memmove(op, *litPtr, litLength);   /* overwrite risk */
    else
        ZSTD_wildcopy(op, *litPtr, litLength);
    op += litLength;
    *litPtr = litEnd;   /* update for next sequence */

    /* check : last match must be at a minimum distance of 8 from end of dest buffer */
    if (oend-op < 8) return ERROR(dstSize_tooSmall);

    /* copy Match */
    {
        const U32 overlapRisk = (((size_t)(litEnd - endMatch)) < 12);
        const BYTE* match = op - sequence.offset;            /* possible underflow at op - offset ? */
        size_t qutt = 12;
        U64 saved[2];

        /* check */
        if (match < base) return ERROR(corruption_detected);
        if (sequence.offset > (size_t)base) return ERROR(corruption_detected);

        /* save beginning of literal sequence, in case of write overlap */
        if (overlapRisk)
        {
            if ((endMatch + qutt) > oend) qutt = oend-endMatch;
            memcpy(saved, endMatch, qutt);
        }

        if (sequence.offset < 8)
        {
            const int dec64 = dec64table[sequence.offset];
            op[0] = match[0];
            op[1] = match[1];
            op[2] = match[2];
            op[3] = match[3];
            match += dec32table[sequence.offset];
            ZSTD_copy4(op+4, match);
            match -= dec64;
        } else { ZSTD_copy8(op, match); }
        op += 8; match += 8;

        if (endMatch > oend-(16-MINMATCH))
        {
            if (op < oend-8)
            {
                ZSTD_wildcopy(op, match, (oend-8) - op);
                match += (oend-8) - op;
                op = oend-8;
            }
            while (op<endMatch) *op++ = *match++;
        }
        else
            ZSTD_wildcopy(op, match, (ptrdiff_t)sequence.matchLength-8);   /* works even if matchLength < 8 */

        /* restore, in case of overlap */
        if (overlapRisk) memcpy(endMatch, saved, qutt);
    }

    return endMatch-ostart;
}

typedef struct ZSTDv01_Dctx_s
{
    U32 LLTable[FSE_DTABLE_SIZE_U32(LLFSELog)];
    U32 OffTable[FSE_DTABLE_SIZE_U32(OffFSELog)];
    U32 MLTable[FSE_DTABLE_SIZE_U32(MLFSELog)];
    void* previousDstEnd;
    void* base;
    size_t expected;
    blockType_t bType;
    U32 phase;
} dctx_t;


static size_t ZSTD_decompressSequences(
                               void* ctx,
                               void* dst, size_t maxDstSize,
                         const void* seqStart, size_t seqSize,
                         const BYTE* litStart, size_t litSize)
{
    dctx_t* dctx = (dctx_t*)ctx;
    const BYTE* ip = (const BYTE*)seqStart;
    const BYTE* const iend = ip + seqSize;
    BYTE* const ostart = (BYTE* const)dst;
    BYTE* op = ostart;
    BYTE* const oend = ostart + maxDstSize;
    size_t errorCode, dumpsLength;
    const BYTE* litPtr = litStart;
    const BYTE* const litEnd = litStart + litSize;
    int nbSeq;
    const BYTE* dumps;
    U32* DTableLL = dctx->LLTable;
    U32* DTableML = dctx->MLTable;
    U32* DTableOffb = dctx->OffTable;
    BYTE* const base = (BYTE*) (dctx->base);

    /* Build Decoding Tables */
    errorCode = ZSTDv01_decodeSeqHeaders(&nbSeq, &dumps, &dumpsLength,
                                      DTableLL, DTableML, DTableOffb,
                                      ip, iend-ip);
    if (ZSTDv01_isError(errorCode)) return errorCode;
    ip += errorCode;

    /* Regen sequences */
    {
        seq_t sequence;
        seqState_t seqState;

        memset(&sequence, 0, sizeof(sequence));
        seqState.dumps = dumps;
        seqState.dumpsEnd = dumps + dumpsLength;
        seqState.prevOffset = 1;
        errorCode = FSE_initDStream(&(seqState.DStream), ip, iend-ip);
        if (FSE_isError(errorCode)) return ERROR(corruption_detected);
        FSE_initDState(&(seqState.stateLL), &(seqState.DStream), DTableLL);
        FSE_initDState(&(seqState.stateOffb), &(seqState.DStream), DTableOffb);
        FSE_initDState(&(seqState.stateML), &(seqState.DStream), DTableML);

        for ( ; (FSE_reloadDStream(&(seqState.DStream)) <= FSE_DStream_completed) && (nbSeq>0) ; )
        {
            size_t oneSeqSize;
            nbSeq--;
            ZSTD_decodeSequence(&sequence, &seqState);
            oneSeqSize = ZSTD_execSequence(op, sequence, &litPtr, litEnd, base, oend);
            if (ZSTDv01_isError(oneSeqSize)) return oneSeqSize;
            op += oneSeqSize;
        }

        /* check if reached exact end */
        if ( !FSE_endOfDStream(&(seqState.DStream)) ) return ERROR(corruption_detected);   /* requested too much : data is corrupted */
        if (nbSeq<0) return ERROR(corruption_detected);   /* requested too many sequences : data is corrupted */

        /* last literal segment */
        {
            size_t lastLLSize = litEnd - litPtr;
            if (op+lastLLSize > oend) return ERROR(dstSize_tooSmall);
            if (op != litPtr) memmove(op, litPtr, lastLLSize);
            op += lastLLSize;
        }
    }

    return op-ostart;
}


static size_t ZSTD_decompressBlock(
                            void* ctx,
                            void* dst, size_t maxDstSize,
                      const void* src, size_t srcSize)
{
    /* blockType == blockCompressed, srcSize is trusted */
    const BYTE* ip = (const BYTE*)src;
    const BYTE* litPtr = NULL;
    size_t litSize = 0;
    size_t errorCode;

    /* Decode literals sub-block */
    errorCode = ZSTDv01_decodeLiteralsBlock(ctx, dst, maxDstSize, &litPtr, &litSize, src, srcSize);
    if (ZSTDv01_isError(errorCode)) return errorCode;
    ip += errorCode;
    srcSize -= errorCode;

    return ZSTD_decompressSequences(ctx, dst, maxDstSize, ip, srcSize, litPtr, litSize);
}


size_t ZSTDv01_decompressDCtx(void* ctx, void* dst, size_t maxDstSize, const void* src, size_t srcSize)
{
    const BYTE* ip = (const BYTE*)src;
    const BYTE* iend = ip + srcSize;
    BYTE* const ostart = (BYTE* const)dst;
    BYTE* op = ostart;
    BYTE* const oend = ostart + maxDstSize;
    size_t remainingSize = srcSize;
    U32 magicNumber;
    size_t errorCode=0;
    blockProperties_t blockProperties;

    /* Frame Header */
    if (srcSize < ZSTD_frameHeaderSize+ZSTD_blockHeaderSize) return ERROR(srcSize_wrong);
    magicNumber = ZSTD_readBE32(src);
    if (magicNumber != ZSTD_magicNumber) return ERROR(prefix_unknown);
    ip += ZSTD_frameHeaderSize; remainingSize -= ZSTD_frameHeaderSize;

    /* Loop on each block */
    while (1)
    {
        size_t blockSize = ZSTDv01_getcBlockSize(ip, iend-ip, &blockProperties);
        if (ZSTDv01_isError(blockSize)) return blockSize;

        ip += ZSTD_blockHeaderSize;
        remainingSize -= ZSTD_blockHeaderSize;
        if (blockSize > remainingSize) return ERROR(srcSize_wrong);

        switch(blockProperties.blockType)
        {
        case bt_compressed:
            errorCode = ZSTD_decompressBlock(ctx, op, oend-op, ip, blockSize);
            break;
        case bt_raw :
            errorCode = ZSTD_copyUncompressedBlock(op, oend-op, ip, blockSize);
            break;
        case bt_rle :
            return ERROR(GENERIC);   /* not yet supported */
            break;
        case bt_end :
            /* end of frame */
            if (remainingSize) return ERROR(srcSize_wrong);
            break;
        default:
            return ERROR(GENERIC);
        }
        if (blockSize == 0) break;   /* bt_end */

        if (ZSTDv01_isError(errorCode)) return errorCode;
        op += errorCode;
        ip += blockSize;
        remainingSize -= blockSize;
    }

    return op-ostart;
}

size_t ZSTDv01_decompress(void* dst, size_t maxDstSize, const void* src, size_t srcSize)
{
    dctx_t ctx;
    ctx.base = dst;
    return ZSTDv01_decompressDCtx(&ctx, dst, maxDstSize, src, srcSize);
}

size_t ZSTDv01_findFrameCompressedSize(const void* src, size_t srcSize)
{
    const BYTE* ip = (const BYTE*)src;
    size_t remainingSize = srcSize;
    U32 magicNumber;
    blockProperties_t blockProperties;

    /* Frame Header */
    if (srcSize < ZSTD_frameHeaderSize+ZSTD_blockHeaderSize) return ERROR(srcSize_wrong);
    magicNumber = ZSTD_readBE32(src);
    if (magicNumber != ZSTD_magicNumber) return ERROR(prefix_unknown);
    ip += ZSTD_frameHeaderSize; remainingSize -= ZSTD_frameHeaderSize;

    /* Loop on each block */
    while (1)
    {
        size_t blockSize = ZSTDv01_getcBlockSize(ip, remainingSize, &blockProperties);
        if (ZSTDv01_isError(blockSize)) return blockSize;

        ip += ZSTD_blockHeaderSize;
        remainingSize -= ZSTD_blockHeaderSize;
        if (blockSize > remainingSize) return ERROR(srcSize_wrong);

        if (blockSize == 0) break;   /* bt_end */

        ip += blockSize;
        remainingSize -= blockSize;
    }

    return ip - (const BYTE*)src;
}

/*******************************
*  Streaming Decompression API
*******************************/

size_t ZSTDv01_resetDCtx(ZSTDv01_Dctx* dctx)
{
    dctx->expected = ZSTD_frameHeaderSize;
    dctx->phase = 0;
    dctx->previousDstEnd = NULL;
    dctx->base = NULL;
    return 0;
}

ZSTDv01_Dctx* ZSTDv01_createDCtx(void)
{
    ZSTDv01_Dctx* dctx = (ZSTDv01_Dctx*)malloc(sizeof(ZSTDv01_Dctx));
    if (dctx==NULL) return NULL;
    ZSTDv01_resetDCtx(dctx);
    return dctx;
}

size_t ZSTDv01_freeDCtx(ZSTDv01_Dctx* dctx)
{
    free(dctx);
    return 0;
}

size_t ZSTDv01_nextSrcSizeToDecompress(ZSTDv01_Dctx* dctx)
{
    return ((dctx_t*)dctx)->expected;
}

size_t ZSTDv01_decompressContinue(ZSTDv01_Dctx* dctx, void* dst, size_t maxDstSize, const void* src, size_t srcSize)
{
    dctx_t* ctx = (dctx_t*)dctx;

    /* Sanity check */
    if (srcSize != ctx->expected) return ERROR(srcSize_wrong);
    if (dst != ctx->previousDstEnd)  /* not contiguous */
        ctx->base = dst;

    /* Decompress : frame header */
    if (ctx->phase == 0)
    {
        /* Check frame magic header */
        U32 magicNumber = ZSTD_readBE32(src);
        if (magicNumber != ZSTD_magicNumber) return ERROR(prefix_unknown);
        ctx->phase = 1;
        ctx->expected = ZSTD_blockHeaderSize;
        return 0;
    }

    /* Decompress : block header */
    if (ctx->phase == 1)
    {
        blockProperties_t bp;
        size_t blockSize = ZSTDv01_getcBlockSize(src, ZSTD_blockHeaderSize, &bp);
        if (ZSTDv01_isError(blockSize)) return blockSize;
        if (bp.blockType == bt_end)
        {
            ctx->expected = 0;
            ctx->phase = 0;
        }
        else
        {
            ctx->expected = blockSize;
            ctx->bType = bp.blockType;
            ctx->phase = 2;
        }

        return 0;
    }

    /* Decompress : block content */
    {
        size_t rSize;
        switch(ctx->bType)
        {
        case bt_compressed:
            rSize = ZSTD_decompressBlock(ctx, dst, maxDstSize, src, srcSize);
            break;
        case bt_raw :
            rSize = ZSTD_copyUncompressedBlock(dst, maxDstSize, src, srcSize);
            break;
        case bt_rle :
            return ERROR(GENERIC);   /* not yet handled */
            break;
        case bt_end :   /* should never happen (filtered at phase 1) */
            rSize = 0;
            break;
        default:
            return ERROR(GENERIC);
        }
        ctx->phase = 1;
        ctx->expected = ZSTD_blockHeaderSize;
        ctx->previousDstEnd = (void*)( ((char*)dst) + rSize);
        return rSize;
    }

}
