/* SPDX-License-Identifier: GPL-2.0+ OR BSD-3-Clause */
/* ******************************************************************
 * bitstream
 * Part of FSE library
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * You can contact the author at :
 * - Source repository : https://github.com/Cyan4973/FiniteStateEntropy
 *
 * This source code is licensed under both the BSD-style license (found in the
 * LICENSE file in the root directory of this source tree) and the GPLv2 (found
 * in the COPYING file in the root directory of this source tree).
 * You may select, at your option, one of the above-listed licenses.
****************************************************************** */
#ifndef BITSTREAM_H_MODULE
#define BITSTREAM_H_MODULE

/*
*  This API consists of small unitary functions, which must be inlined for best performance.
*  Since link-time-optimization is not available for all compilers,
*  these functions are defined into a .h to be included.
*/

/*-****************************************
*  Dependencies
******************************************/
#include "mem.h"            /* unaligned access routines */
#include "compiler.h"       /* UNLIKELY() */
#include "debug.h"          /* assert(), DEBUGLOG(), RAWLOG() */
#include "error_private.h"  /* error codes and messages */
#include "bits.h"           /* ZSTD_highbit32 */

/*=========================================
*  Target specific
=========================================*/

#define STREAM_ACCUMULATOR_MIN_32  25
#define STREAM_ACCUMULATOR_MIN_64  57
#define STREAM_ACCUMULATOR_MIN    ((U32)(MEM_32bits() ? STREAM_ACCUMULATOR_MIN_32 : STREAM_ACCUMULATOR_MIN_64))


/*-******************************************
*  bitStream encoding API (write forward)
********************************************/
typedef size_t BitContainerType;
/* bitStream can mix input from multiple sources.
 * A critical property of these streams is that they encode and decode in **reverse** direction.
 * So the first bit sequence you add will be the last to be read, like a LIFO stack.
 */
typedef struct {
    BitContainerType bitContainer;
    unsigned bitPos;
    char*  startPtr;
    char*  ptr;
    char*  endPtr;
} BIT_CStream_t;

MEM_STATIC size_t BIT_initCStream(BIT_CStream_t* bitC, void* dstBuffer, size_t dstCapacity);
MEM_STATIC void   BIT_addBits(BIT_CStream_t* bitC, BitContainerType value, unsigned nbBits);
MEM_STATIC void   BIT_flushBits(BIT_CStream_t* bitC);
MEM_STATIC size_t BIT_closeCStream(BIT_CStream_t* bitC);

/* Start with initCStream, providing the size of buffer to write into.
*  bitStream will never write outside of this buffer.
*  `dstCapacity` must be >= sizeof(bitD->bitContainer), otherwise @return will be an error code.
*
*  bits are first added to a local register.
*  Local register is BitContainerType, 64-bits on 64-bits systems, or 32-bits on 32-bits systems.
*  Writing data into memory is an explicit operation, performed by the flushBits function.
*  Hence keep track how many bits are potentially stored into local register to avoid register overflow.
*  After a flushBits, a maximum of 7 bits might still be stored into local register.
*
*  Avoid storing elements of more than 24 bits if you want compatibility with 32-bits bitstream readers.
*
*  Last operation is to close the bitStream.
*  The function returns the final size of CStream in bytes.
*  If data couldn't fit into `dstBuffer`, it will return a 0 ( == not storable)
*/


/*-********************************************
*  bitStream decoding API (read backward)
**********************************************/
typedef struct {
    BitContainerType bitContainer;
    unsigned bitsConsumed;
    const char* ptr;
    const char* start;
    const char* limitPtr;
} BIT_DStream_t;

typedef enum { BIT_DStream_unfinished = 0,  /* fully refilled */
               BIT_DStream_endOfBuffer = 1, /* still some bits left in bitstream */
               BIT_DStream_completed = 2,   /* bitstream entirely consumed, bit-exact */
               BIT_DStream_overflow = 3     /* user requested more bits than present in bitstream */
    } BIT_DStream_status;  /* result of BIT_reloadDStream() */

MEM_STATIC size_t   BIT_initDStream(BIT_DStream_t* bitD, const void* srcBuffer, size_t srcSize);
MEM_STATIC BitContainerType BIT_readBits(BIT_DStream_t* bitD, unsigned nbBits);
MEM_STATIC BIT_DStream_status BIT_reloadDStream(BIT_DStream_t* bitD);
MEM_STATIC unsigned BIT_endOfDStream(const BIT_DStream_t* bitD);


/* Start by invoking BIT_initDStream().
*  A chunk of the bitStream is then stored into a local register.
*  Local register size is 64-bits on 64-bits systems, 32-bits on 32-bits systems (BitContainerType).
*  You can then retrieve bitFields stored into the local register, **in reverse order**.
*  Local register is explicitly reloaded from memory by the BIT_reloadDStream() method.
*  A reload guarantee a minimum of ((8*sizeof(bitD->bitContainer))-7) bits when its result is BIT_DStream_unfinished.
*  Otherwise, it can be less than that, so proceed accordingly.
*  Checking if DStream has reached its end can be performed with BIT_endOfDStream().
*/


/*-****************************************
*  unsafe API
******************************************/
MEM_STATIC void BIT_addBitsFast(BIT_CStream_t* bitC, BitContainerType value, unsigned nbBits);
/* faster, but works only if value is "clean", meaning all high bits above nbBits are 0 */

MEM_STATIC void BIT_flushBitsFast(BIT_CStream_t* bitC);
/* unsafe version; does not check buffer overflow */

MEM_STATIC size_t BIT_readBitsFast(BIT_DStream_t* bitD, unsigned nbBits);
/* faster, but works only if nbBits >= 1 */

/*=====    Local Constants   =====*/
static const unsigned BIT_mask[] = {
    0,          1,         3,         7,         0xF,       0x1F,
    0x3F,       0x7F,      0xFF,      0x1FF,     0x3FF,     0x7FF,
    0xFFF,      0x1FFF,    0x3FFF,    0x7FFF,    0xFFFF,    0x1FFFF,
    0x3FFFF,    0x7FFFF,   0xFFFFF,   0x1FFFFF,  0x3FFFFF,  0x7FFFFF,
    0xFFFFFF,   0x1FFFFFF, 0x3FFFFFF, 0x7FFFFFF, 0xFFFFFFF, 0x1FFFFFFF,
    0x3FFFFFFF, 0x7FFFFFFF}; /* up to 31 bits */
#define BIT_MASK_SIZE (sizeof(BIT_mask) / sizeof(BIT_mask[0]))

/*-**************************************************************
*  bitStream encoding
****************************************************************/
/*! BIT_initCStream() :
 *  `dstCapacity` must be > sizeof(size_t)
 *  @return : 0 if success,
 *            otherwise an error code (can be tested using ERR_isError()) */
MEM_STATIC size_t BIT_initCStream(BIT_CStream_t* bitC,
                                  void* startPtr, size_t dstCapacity)
{
    bitC->bitContainer = 0;
    bitC->bitPos = 0;
    bitC->startPtr = (char*)startPtr;
    bitC->ptr = bitC->startPtr;
    bitC->endPtr = bitC->startPtr + dstCapacity - sizeof(bitC->bitContainer);
    if (dstCapacity <= sizeof(bitC->bitContainer)) return ERROR(dstSize_tooSmall);
    return 0;
}

FORCE_INLINE_TEMPLATE BitContainerType BIT_getLowerBits(BitContainerType bitContainer, U32 const nbBits)
{
    assert(nbBits < BIT_MASK_SIZE);
    return bitContainer & BIT_mask[nbBits];
}

/*! BIT_addBits() :
 *  can add up to 31 bits into `bitC`.
 *  Note : does not check for register overflow ! */
MEM_STATIC void BIT_addBits(BIT_CStream_t* bitC,
                            BitContainerType value, unsigned nbBits)
{
    DEBUG_STATIC_ASSERT(BIT_MASK_SIZE == 32);
    assert(nbBits < BIT_MASK_SIZE);
    assert(nbBits + bitC->bitPos < sizeof(bitC->bitContainer) * 8);
    bitC->bitContainer |= BIT_getLowerBits(value, nbBits) << bitC->bitPos;
    bitC->bitPos += nbBits;
}

/*! BIT_addBitsFast() :
 *  works only if `value` is _clean_,
 *  meaning all high bits above nbBits are 0 */
MEM_STATIC void BIT_addBitsFast(BIT_CStream_t* bitC,
                                BitContainerType value, unsigned nbBits)
{
    assert((value>>nbBits) == 0);
    assert(nbBits + bitC->bitPos < sizeof(bitC->bitContainer) * 8);
    bitC->bitContainer |= value << bitC->bitPos;
    bitC->bitPos += nbBits;
}

/*! BIT_flushBitsFast() :
 *  assumption : bitContainer has not overflowed
 *  unsafe version; does not check buffer overflow */
MEM_STATIC void BIT_flushBitsFast(BIT_CStream_t* bitC)
{
    size_t const nbBytes = bitC->bitPos >> 3;
    assert(bitC->bitPos < sizeof(bitC->bitContainer) * 8);
    assert(bitC->ptr <= bitC->endPtr);
    MEM_writeLEST(bitC->ptr, bitC->bitContainer);
    bitC->ptr += nbBytes;
    bitC->bitPos &= 7;
    bitC->bitContainer >>= nbBytes*8;
}

/*! BIT_flushBits() :
 *  assumption : bitContainer has not overflowed
 *  safe version; check for buffer overflow, and prevents it.
 *  note : does not signal buffer overflow.
 *  overflow will be revealed later on using BIT_closeCStream() */
MEM_STATIC void BIT_flushBits(BIT_CStream_t* bitC)
{
    size_t const nbBytes = bitC->bitPos >> 3;
    assert(bitC->bitPos < sizeof(bitC->bitContainer) * 8);
    assert(bitC->ptr <= bitC->endPtr);
    MEM_writeLEST(bitC->ptr, bitC->bitContainer);
    bitC->ptr += nbBytes;
    if (bitC->ptr > bitC->endPtr) bitC->ptr = bitC->endPtr;
    bitC->bitPos &= 7;
    bitC->bitContainer >>= nbBytes*8;
}

/*! BIT_closeCStream() :
 *  @return : size of CStream, in bytes,
 *            or 0 if it could not fit into dstBuffer */
MEM_STATIC size_t BIT_closeCStream(BIT_CStream_t* bitC)
{
    BIT_addBitsFast(bitC, 1, 1);   /* endMark */
    BIT_flushBits(bitC);
    if (bitC->ptr >= bitC->endPtr) return 0; /* overflow detected */
    return (size_t)(bitC->ptr - bitC->startPtr) + (bitC->bitPos > 0);
}


/*-********************************************************
*  bitStream decoding
**********************************************************/
/*! BIT_initDStream() :
 *  Initialize a BIT_DStream_t.
 * `bitD` : a pointer to an already allocated BIT_DStream_t structure.
 * `srcSize` must be the *exact* size of the bitStream, in bytes.
 * @return : size of stream (== srcSize), or an errorCode if a problem is detected
 */
MEM_STATIC size_t BIT_initDStream(BIT_DStream_t* bitD, const void* srcBuffer, size_t srcSize)
{
    if (srcSize < 1) { ZSTD_memset(bitD, 0, sizeof(*bitD)); return ERROR(srcSize_wrong); }

    bitD->start = (const char*)srcBuffer;
    bitD->limitPtr = bitD->start + sizeof(bitD->bitContainer);

    if (srcSize >=  sizeof(bitD->bitContainer)) {  /* normal case */
        bitD->ptr   = (const char*)srcBuffer + srcSize - sizeof(bitD->bitContainer);
        bitD->bitContainer = MEM_readLEST(bitD->ptr);
        { BYTE const lastByte = ((const BYTE*)srcBuffer)[srcSize-1];
          bitD->bitsConsumed = lastByte ? 8 - ZSTD_highbit32(lastByte) : 0;  /* ensures bitsConsumed is always set */
          if (lastByte == 0) return ERROR(GENERIC); /* endMark not present */ }
    } else {
        bitD->ptr   = bitD->start;
        bitD->bitContainer = *(const BYTE*)(bitD->start);
        switch(srcSize)
        {
        case 7: bitD->bitContainer += (BitContainerType)(((const BYTE*)(srcBuffer))[6]) << (sizeof(bitD->bitContainer)*8 - 16);
                ZSTD_FALLTHROUGH;

        case 6: bitD->bitContainer += (BitContainerType)(((const BYTE*)(srcBuffer))[5]) << (sizeof(bitD->bitContainer)*8 - 24);
                ZSTD_FALLTHROUGH;

        case 5: bitD->bitContainer += (BitContainerType)(((const BYTE*)(srcBuffer))[4]) << (sizeof(bitD->bitContainer)*8 - 32);
                ZSTD_FALLTHROUGH;

        case 4: bitD->bitContainer += (BitContainerType)(((const BYTE*)(srcBuffer))[3]) << 24;
                ZSTD_FALLTHROUGH;

        case 3: bitD->bitContainer += (BitContainerType)(((const BYTE*)(srcBuffer))[2]) << 16;
                ZSTD_FALLTHROUGH;

        case 2: bitD->bitContainer += (BitContainerType)(((const BYTE*)(srcBuffer))[1]) <<  8;
                ZSTD_FALLTHROUGH;

        default: break;
        }
        {   BYTE const lastByte = ((const BYTE*)srcBuffer)[srcSize-1];
            bitD->bitsConsumed = lastByte ? 8 - ZSTD_highbit32(lastByte) : 0;
            if (lastByte == 0) return ERROR(corruption_detected);  /* endMark not present */
        }
        bitD->bitsConsumed += (U32)(sizeof(bitD->bitContainer) - srcSize)*8;
    }

    return srcSize;
}

FORCE_INLINE_TEMPLATE BitContainerType BIT_getUpperBits(BitContainerType bitContainer, U32 const start)
{
    return bitContainer >> start;
}

FORCE_INLINE_TEMPLATE BitContainerType BIT_getMiddleBits(BitContainerType bitContainer, U32 const start, U32 const nbBits)
{
    U32 const regMask = sizeof(bitContainer)*8 - 1;
    /* if start > regMask, bitstream is corrupted, and result is undefined */
    assert(nbBits < BIT_MASK_SIZE);
    /* x86 transform & ((1 << nbBits) - 1) to bzhi instruction, it is better
     * than accessing memory. When bmi2 instruction is not present, we consider
     * such cpus old (pre-Haswell, 2013) and their performance is not of that
     * importance.
     */
#if defined(__x86_64__) || defined(_M_X64)
    return (bitContainer >> (start & regMask)) & ((((U64)1) << nbBits) - 1);
#else
    return (bitContainer >> (start & regMask)) & BIT_mask[nbBits];
#endif
}

/*! BIT_lookBits() :
 *  Provides next n bits from local register.
 *  local register is not modified.
 *  On 32-bits, maxNbBits==24.
 *  On 64-bits, maxNbBits==56.
 * @return : value extracted */
FORCE_INLINE_TEMPLATE BitContainerType BIT_lookBits(const BIT_DStream_t*  bitD, U32 nbBits)
{
    /* arbitrate between double-shift and shift+mask */
#if 1
    /* if bitD->bitsConsumed + nbBits > sizeof(bitD->bitContainer)*8,
     * bitstream is likely corrupted, and result is undefined */
    return BIT_getMiddleBits(bitD->bitContainer, (sizeof(bitD->bitContainer)*8) - bitD->bitsConsumed - nbBits, nbBits);
#else
    /* this code path is slower on my os-x laptop */
    U32 const regMask = sizeof(bitD->bitContainer)*8 - 1;
    return ((bitD->bitContainer << (bitD->bitsConsumed & regMask)) >> 1) >> ((regMask-nbBits) & regMask);
#endif
}

/*! BIT_lookBitsFast() :
 *  unsafe version; only works if nbBits >= 1 */
MEM_STATIC BitContainerType BIT_lookBitsFast(const BIT_DStream_t* bitD, U32 nbBits)
{
    U32 const regMask = sizeof(bitD->bitContainer)*8 - 1;
    assert(nbBits >= 1);
    return (bitD->bitContainer << (bitD->bitsConsumed & regMask)) >> (((regMask+1)-nbBits) & regMask);
}

FORCE_INLINE_TEMPLATE void BIT_skipBits(BIT_DStream_t* bitD, U32 nbBits)
{
    bitD->bitsConsumed += nbBits;
}

/*! BIT_readBits() :
 *  Read (consume) next n bits from local register and update.
 *  Pay attention to not read more than nbBits contained into local register.
 * @return : extracted value. */
FORCE_INLINE_TEMPLATE BitContainerType BIT_readBits(BIT_DStream_t* bitD, unsigned nbBits)
{
    BitContainerType const value = BIT_lookBits(bitD, nbBits);
    BIT_skipBits(bitD, nbBits);
    return value;
}

/*! BIT_readBitsFast() :
 *  unsafe version; only works if nbBits >= 1 */
MEM_STATIC BitContainerType BIT_readBitsFast(BIT_DStream_t* bitD, unsigned nbBits)
{
    BitContainerType const value = BIT_lookBitsFast(bitD, nbBits);
    assert(nbBits >= 1);
    BIT_skipBits(bitD, nbBits);
    return value;
}

/*! BIT_reloadDStream_internal() :
 *  Simple variant of BIT_reloadDStream(), with two conditions:
 *  1. bitstream is valid : bitsConsumed <= sizeof(bitD->bitContainer)*8
 *  2. look window is valid after shifted down : bitD->ptr >= bitD->start
 */
MEM_STATIC BIT_DStream_status BIT_reloadDStream_internal(BIT_DStream_t* bitD)
{
    assert(bitD->bitsConsumed <= sizeof(bitD->bitContainer)*8);
    bitD->ptr -= bitD->bitsConsumed >> 3;
    assert(bitD->ptr >= bitD->start);
    bitD->bitsConsumed &= 7;
    bitD->bitContainer = MEM_readLEST(bitD->ptr);
    return BIT_DStream_unfinished;
}

/*! BIT_reloadDStreamFast() :
 *  Similar to BIT_reloadDStream(), but with two differences:
 *  1. bitsConsumed <= sizeof(bitD->bitContainer)*8 must hold!
 *  2. Returns BIT_DStream_overflow when bitD->ptr < bitD->limitPtr, at this
 *     point you must use BIT_reloadDStream() to reload.
 */
MEM_STATIC BIT_DStream_status BIT_reloadDStreamFast(BIT_DStream_t* bitD)
{
    if (UNLIKELY(bitD->ptr < bitD->limitPtr))
        return BIT_DStream_overflow;
    return BIT_reloadDStream_internal(bitD);
}

/*! BIT_reloadDStream() :
 *  Refill `bitD` from buffer previously set in BIT_initDStream() .
 *  This function is safe, it guarantees it will not never beyond src buffer.
 * @return : status of `BIT_DStream_t` internal register.
 *           when status == BIT_DStream_unfinished, internal register is filled with at least 25 or 57 bits */
FORCE_INLINE_TEMPLATE BIT_DStream_status BIT_reloadDStream(BIT_DStream_t* bitD)
{
    /* note : once in overflow mode, a bitstream remains in this mode until it's reset */
    if (UNLIKELY(bitD->bitsConsumed > (sizeof(bitD->bitContainer)*8))) {
        static const BitContainerType zeroFilled = 0;
        bitD->ptr = (const char*)&zeroFilled; /* aliasing is allowed for char */
        /* overflow detected, erroneous scenario or end of stream: no update */
        return BIT_DStream_overflow;
    }

    assert(bitD->ptr >= bitD->start);

    if (bitD->ptr >= bitD->limitPtr) {
        return BIT_reloadDStream_internal(bitD);
    }
    if (bitD->ptr == bitD->start) {
        /* reached end of bitStream => no update */
        if (bitD->bitsConsumed < sizeof(bitD->bitContainer)*8) return BIT_DStream_endOfBuffer;
        return BIT_DStream_completed;
    }
    /* start < ptr < limitPtr => cautious update */
    {   U32 nbBytes = bitD->bitsConsumed >> 3;
        BIT_DStream_status result = BIT_DStream_unfinished;
        if (bitD->ptr - nbBytes < bitD->start) {
            nbBytes = (U32)(bitD->ptr - bitD->start);  /* ptr > start */
            result = BIT_DStream_endOfBuffer;
        }
        bitD->ptr -= nbBytes;
        bitD->bitsConsumed -= nbBytes*8;
        bitD->bitContainer = MEM_readLEST(bitD->ptr);   /* reminder : srcSize > sizeof(bitD->bitContainer), otherwise bitD->ptr == bitD->start */
        return result;
    }
}

/*! BIT_endOfDStream() :
 * @return : 1 if DStream has _exactly_ reached its end (all bits consumed).
 */
MEM_STATIC unsigned BIT_endOfDStream(const BIT_DStream_t* DStream)
{
    return ((DStream->ptr == DStream->start) && (DStream->bitsConsumed == sizeof(DStream->bitContainer)*8));
}

#endif /* BITSTREAM_H_MODULE */
