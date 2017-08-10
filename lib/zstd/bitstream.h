/*
 * bitstream
 * Part of FSE library
 * header file (to include)
 * Copyright (C) 2013-2016, Yann Collet.
 *
 * BSD 2-Clause License (http://www.opensource.org/licenses/bsd-license.php)
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *   * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License version 2 as published by the
 * Free Software Foundation. This program is dual-licensed; you may select
 * either version 2 of the GNU General Public License ("GPL") or BSD license
 * ("BSD").
 *
 * You can contact the author at :
 * - Source repository : https://github.com/Cyan4973/FiniteStateEntropy
 */
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
#include "error_private.h" /* error codes and messages */
#include "mem.h"	   /* unaligned access routines */

/*=========================================
*  Target specific
=========================================*/
#define STREAM_ACCUMULATOR_MIN_32 25
#define STREAM_ACCUMULATOR_MIN_64 57
#define STREAM_ACCUMULATOR_MIN ((U32)(ZSTD_32bits() ? STREAM_ACCUMULATOR_MIN_32 : STREAM_ACCUMULATOR_MIN_64))

/*-******************************************
*  bitStream encoding API (write forward)
********************************************/
/* bitStream can mix input from multiple sources.
*  A critical property of these streams is that they encode and decode in **reverse** direction.
*  So the first bit sequence you add will be the last to be read, like a LIFO stack.
*/
typedef struct {
	size_t bitContainer;
	int bitPos;
	char *startPtr;
	char *ptr;
	char *endPtr;
} BIT_CStream_t;

ZSTD_STATIC size_t BIT_initCStream(BIT_CStream_t *bitC, void *dstBuffer, size_t dstCapacity);
ZSTD_STATIC void BIT_addBits(BIT_CStream_t *bitC, size_t value, unsigned nbBits);
ZSTD_STATIC void BIT_flushBits(BIT_CStream_t *bitC);
ZSTD_STATIC size_t BIT_closeCStream(BIT_CStream_t *bitC);

/* Start with initCStream, providing the size of buffer to write into.
*  bitStream will never write outside of this buffer.
*  `dstCapacity` must be >= sizeof(bitD->bitContainer), otherwise @return will be an error code.
*
*  bits are first added to a local register.
*  Local register is size_t, hence 64-bits on 64-bits systems, or 32-bits on 32-bits systems.
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
	size_t bitContainer;
	unsigned bitsConsumed;
	const char *ptr;
	const char *start;
} BIT_DStream_t;

typedef enum {
	BIT_DStream_unfinished = 0,
	BIT_DStream_endOfBuffer = 1,
	BIT_DStream_completed = 2,
	BIT_DStream_overflow = 3
} BIT_DStream_status; /* result of BIT_reloadDStream() */
/* 1,2,4,8 would be better for bitmap combinations, but slows down performance a bit ... :( */

ZSTD_STATIC size_t BIT_initDStream(BIT_DStream_t *bitD, const void *srcBuffer, size_t srcSize);
ZSTD_STATIC size_t BIT_readBits(BIT_DStream_t *bitD, unsigned nbBits);
ZSTD_STATIC BIT_DStream_status BIT_reloadDStream(BIT_DStream_t *bitD);
ZSTD_STATIC unsigned BIT_endOfDStream(const BIT_DStream_t *bitD);

/* Start by invoking BIT_initDStream().
*  A chunk of the bitStream is then stored into a local register.
*  Local register size is 64-bits on 64-bits systems, 32-bits on 32-bits systems (size_t).
*  You can then retrieve bitFields stored into the local register, **in reverse order**.
*  Local register is explicitly reloaded from memory by the BIT_reloadDStream() method.
*  A reload guarantee a minimum of ((8*sizeof(bitD->bitContainer))-7) bits when its result is BIT_DStream_unfinished.
*  Otherwise, it can be less than that, so proceed accordingly.
*  Checking if DStream has reached its end can be performed with BIT_endOfDStream().
*/

/*-****************************************
*  unsafe API
******************************************/
ZSTD_STATIC void BIT_addBitsFast(BIT_CStream_t *bitC, size_t value, unsigned nbBits);
/* faster, but works only if value is "clean", meaning all high bits above nbBits are 0 */

ZSTD_STATIC void BIT_flushBitsFast(BIT_CStream_t *bitC);
/* unsafe version; does not check buffer overflow */

ZSTD_STATIC size_t BIT_readBitsFast(BIT_DStream_t *bitD, unsigned nbBits);
/* faster, but works only if nbBits >= 1 */

/*-**************************************************************
*  Internal functions
****************************************************************/
ZSTD_STATIC unsigned BIT_highbit32(register U32 val) { return 31 - __builtin_clz(val); }

/*=====    Local Constants   =====*/
static const unsigned BIT_mask[] = {0,       1,       3,       7,	0xF,      0x1F,     0x3F,     0x7F,      0xFF,
				    0x1FF,   0x3FF,   0x7FF,   0xFFF,    0x1FFF,   0x3FFF,   0x7FFF,   0xFFFF,    0x1FFFF,
				    0x3FFFF, 0x7FFFF, 0xFFFFF, 0x1FFFFF, 0x3FFFFF, 0x7FFFFF, 0xFFFFFF, 0x1FFFFFF, 0x3FFFFFF}; /* up to 26 bits */

/*-**************************************************************
*  bitStream encoding
****************************************************************/
/*! BIT_initCStream() :
 *  `dstCapacity` must be > sizeof(void*)
 *  @return : 0 if success,
			  otherwise an error code (can be tested using ERR_isError() ) */
ZSTD_STATIC size_t BIT_initCStream(BIT_CStream_t *bitC, void *startPtr, size_t dstCapacity)
{
	bitC->bitContainer = 0;
	bitC->bitPos = 0;
	bitC->startPtr = (char *)startPtr;
	bitC->ptr = bitC->startPtr;
	bitC->endPtr = bitC->startPtr + dstCapacity - sizeof(bitC->ptr);
	if (dstCapacity <= sizeof(bitC->ptr))
		return ERROR(dstSize_tooSmall);
	return 0;
}

/*! BIT_addBits() :
	can add up to 26 bits into `bitC`.
	Does not check for register overflow ! */
ZSTD_STATIC void BIT_addBits(BIT_CStream_t *bitC, size_t value, unsigned nbBits)
{
	bitC->bitContainer |= (value & BIT_mask[nbBits]) << bitC->bitPos;
	bitC->bitPos += nbBits;
}

/*! BIT_addBitsFast() :
 *  works only if `value` is _clean_, meaning all high bits above nbBits are 0 */
ZSTD_STATIC void BIT_addBitsFast(BIT_CStream_t *bitC, size_t value, unsigned nbBits)
{
	bitC->bitContainer |= value << bitC->bitPos;
	bitC->bitPos += nbBits;
}

/*! BIT_flushBitsFast() :
 *  unsafe version; does not check buffer overflow */
ZSTD_STATIC void BIT_flushBitsFast(BIT_CStream_t *bitC)
{
	size_t const nbBytes = bitC->bitPos >> 3;
	ZSTD_writeLEST(bitC->ptr, bitC->bitContainer);
	bitC->ptr += nbBytes;
	bitC->bitPos &= 7;
	bitC->bitContainer >>= nbBytes * 8; /* if bitPos >= sizeof(bitContainer)*8 --> undefined behavior */
}

/*! BIT_flushBits() :
 *  safe version; check for buffer overflow, and prevents it.
 *  note : does not signal buffer overflow. This will be revealed later on using BIT_closeCStream() */
ZSTD_STATIC void BIT_flushBits(BIT_CStream_t *bitC)
{
	size_t const nbBytes = bitC->bitPos >> 3;
	ZSTD_writeLEST(bitC->ptr, bitC->bitContainer);
	bitC->ptr += nbBytes;
	if (bitC->ptr > bitC->endPtr)
		bitC->ptr = bitC->endPtr;
	bitC->bitPos &= 7;
	bitC->bitContainer >>= nbBytes * 8; /* if bitPos >= sizeof(bitContainer)*8 --> undefined behavior */
}

/*! BIT_closeCStream() :
 *  @return : size of CStream, in bytes,
			  or 0 if it could not fit into dstBuffer */
ZSTD_STATIC size_t BIT_closeCStream(BIT_CStream_t *bitC)
{
	BIT_addBitsFast(bitC, 1, 1); /* endMark */
	BIT_flushBits(bitC);

	if (bitC->ptr >= bitC->endPtr)
		return 0; /* doesn't fit within authorized budget : cancel */

	return (bitC->ptr - bitC->startPtr) + (bitC->bitPos > 0);
}

/*-********************************************************
* bitStream decoding
**********************************************************/
/*! BIT_initDStream() :
*   Initialize a BIT_DStream_t.
*   `bitD` : a pointer to an already allocated BIT_DStream_t structure.
*   `srcSize` must be the *exact* size of the bitStream, in bytes.
*   @return : size of stream (== srcSize) or an errorCode if a problem is detected
*/
ZSTD_STATIC size_t BIT_initDStream(BIT_DStream_t *bitD, const void *srcBuffer, size_t srcSize)
{
	if (srcSize < 1) {
		memset(bitD, 0, sizeof(*bitD));
		return ERROR(srcSize_wrong);
	}

	if (srcSize >= sizeof(bitD->bitContainer)) { /* normal case */
		bitD->start = (const char *)srcBuffer;
		bitD->ptr = (const char *)srcBuffer + srcSize - sizeof(bitD->bitContainer);
		bitD->bitContainer = ZSTD_readLEST(bitD->ptr);
		{
			BYTE const lastByte = ((const BYTE *)srcBuffer)[srcSize - 1];
			bitD->bitsConsumed = lastByte ? 8 - BIT_highbit32(lastByte) : 0; /* ensures bitsConsumed is always set */
			if (lastByte == 0)
				return ERROR(GENERIC); /* endMark not present */
		}
	} else {
		bitD->start = (const char *)srcBuffer;
		bitD->ptr = bitD->start;
		bitD->bitContainer = *(const BYTE *)(bitD->start);
		switch (srcSize) {
		case 7: bitD->bitContainer += (size_t)(((const BYTE *)(srcBuffer))[6]) << (sizeof(bitD->bitContainer) * 8 - 16);
		case 6: bitD->bitContainer += (size_t)(((const BYTE *)(srcBuffer))[5]) << (sizeof(bitD->bitContainer) * 8 - 24);
		case 5: bitD->bitContainer += (size_t)(((const BYTE *)(srcBuffer))[4]) << (sizeof(bitD->bitContainer) * 8 - 32);
		case 4: bitD->bitContainer += (size_t)(((const BYTE *)(srcBuffer))[3]) << 24;
		case 3: bitD->bitContainer += (size_t)(((const BYTE *)(srcBuffer))[2]) << 16;
		case 2: bitD->bitContainer += (size_t)(((const BYTE *)(srcBuffer))[1]) << 8;
		default:;
		}
		{
			BYTE const lastByte = ((const BYTE *)srcBuffer)[srcSize - 1];
			bitD->bitsConsumed = lastByte ? 8 - BIT_highbit32(lastByte) : 0;
			if (lastByte == 0)
				return ERROR(GENERIC); /* endMark not present */
		}
		bitD->bitsConsumed += (U32)(sizeof(bitD->bitContainer) - srcSize) * 8;
	}

	return srcSize;
}

ZSTD_STATIC size_t BIT_getUpperBits(size_t bitContainer, U32 const start) { return bitContainer >> start; }

ZSTD_STATIC size_t BIT_getMiddleBits(size_t bitContainer, U32 const start, U32 const nbBits) { return (bitContainer >> start) & BIT_mask[nbBits]; }

ZSTD_STATIC size_t BIT_getLowerBits(size_t bitContainer, U32 const nbBits) { return bitContainer & BIT_mask[nbBits]; }

/*! BIT_lookBits() :
 *  Provides next n bits from local register.
 *  local register is not modified.
 *  On 32-bits, maxNbBits==24.
 *  On 64-bits, maxNbBits==56.
 *  @return : value extracted
 */
ZSTD_STATIC size_t BIT_lookBits(const BIT_DStream_t *bitD, U32 nbBits)
{
	U32 const bitMask = sizeof(bitD->bitContainer) * 8 - 1;
	return ((bitD->bitContainer << (bitD->bitsConsumed & bitMask)) >> 1) >> ((bitMask - nbBits) & bitMask);
}

/*! BIT_lookBitsFast() :
*   unsafe version; only works only if nbBits >= 1 */
ZSTD_STATIC size_t BIT_lookBitsFast(const BIT_DStream_t *bitD, U32 nbBits)
{
	U32 const bitMask = sizeof(bitD->bitContainer) * 8 - 1;
	return (bitD->bitContainer << (bitD->bitsConsumed & bitMask)) >> (((bitMask + 1) - nbBits) & bitMask);
}

ZSTD_STATIC void BIT_skipBits(BIT_DStream_t *bitD, U32 nbBits) { bitD->bitsConsumed += nbBits; }

/*! BIT_readBits() :
 *  Read (consume) next n bits from local register and update.
 *  Pay attention to not read more than nbBits contained into local register.
 *  @return : extracted value.
 */
ZSTD_STATIC size_t BIT_readBits(BIT_DStream_t *bitD, U32 nbBits)
{
	size_t const value = BIT_lookBits(bitD, nbBits);
	BIT_skipBits(bitD, nbBits);
	return value;
}

/*! BIT_readBitsFast() :
*   unsafe version; only works only if nbBits >= 1 */
ZSTD_STATIC size_t BIT_readBitsFast(BIT_DStream_t *bitD, U32 nbBits)
{
	size_t const value = BIT_lookBitsFast(bitD, nbBits);
	BIT_skipBits(bitD, nbBits);
	return value;
}

/*! BIT_reloadDStream() :
*   Refill `bitD` from buffer previously set in BIT_initDStream() .
*   This function is safe, it guarantees it will not read beyond src buffer.
*   @return : status of `BIT_DStream_t` internal register.
			  if status == BIT_DStream_unfinished, internal register is filled with >= (sizeof(bitD->bitContainer)*8 - 7) bits */
ZSTD_STATIC BIT_DStream_status BIT_reloadDStream(BIT_DStream_t *bitD)
{
	if (bitD->bitsConsumed > (sizeof(bitD->bitContainer) * 8)) /* should not happen => corruption detected */
		return BIT_DStream_overflow;

	if (bitD->ptr >= bitD->start + sizeof(bitD->bitContainer)) {
		bitD->ptr -= bitD->bitsConsumed >> 3;
		bitD->bitsConsumed &= 7;
		bitD->bitContainer = ZSTD_readLEST(bitD->ptr);
		return BIT_DStream_unfinished;
	}
	if (bitD->ptr == bitD->start) {
		if (bitD->bitsConsumed < sizeof(bitD->bitContainer) * 8)
			return BIT_DStream_endOfBuffer;
		return BIT_DStream_completed;
	}
	{
		U32 nbBytes = bitD->bitsConsumed >> 3;
		BIT_DStream_status result = BIT_DStream_unfinished;
		if (bitD->ptr - nbBytes < bitD->start) {
			nbBytes = (U32)(bitD->ptr - bitD->start); /* ptr > start */
			result = BIT_DStream_endOfBuffer;
		}
		bitD->ptr -= nbBytes;
		bitD->bitsConsumed -= nbBytes * 8;
		bitD->bitContainer = ZSTD_readLEST(bitD->ptr); /* reminder : srcSize > sizeof(bitD) */
		return result;
	}
}

/*! BIT_endOfDStream() :
*   @return Tells if DStream has exactly reached its end (all bits consumed).
*/
ZSTD_STATIC unsigned BIT_endOfDStream(const BIT_DStream_t *DStream)
{
	return ((DStream->ptr == DStream->start) && (DStream->bitsConsumed == sizeof(DStream->bitContainer) * 8));
}

#endif /* BITSTREAM_H_MODULE */
