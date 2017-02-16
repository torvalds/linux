#ifndef __LZ4DEFS_H__
#define __LZ4DEFS_H__

/*
 * lz4defs.h -- common and architecture specific defines for the kernel usage

 * LZ4 - Fast LZ compression algorithm
 * Copyright (C) 2011-2016, Yann Collet.
 * BSD 2-Clause License (http://www.opensource.org/licenses/bsd-license.php)
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *	* Redistributions of source code must retain the above copyright
 *	  notice, this list of conditions and the following disclaimer.
 *	* Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
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
 * You can contact the author at :
 *	- LZ4 homepage : http://www.lz4.org
 *	- LZ4 source repository : https://github.com/lz4/lz4
 *
 *	Changed for kernel usage by:
 *	Sven Schmidt <4sschmid@informatik.uni-hamburg.de>
 */

#include <asm/unaligned.h>
#include <linux/string.h>	 /* memset, memcpy */

/*
 * Detects 64 bits mode
*/
#if defined(CONFIG_64BIT)
#define LZ4_ARCH64 1
#else
#define LZ4_ARCH64 0
#endif

/*-************************************
 *	Basic Types
 **************************************/
#include <linux/types.h>

typedef	uint8_t BYTE;
typedef uint16_t U16;
typedef uint32_t U32;
typedef	int32_t S32;
typedef uint64_t U64;
typedef uintptr_t uptrval;

/*-************************************
 *	Constants
 **************************************/
#define MINMATCH 4

#define WILDCOPYLENGTH 8
#define LASTLITERALS 5
#define MFLIMIT (WILDCOPYLENGTH+MINMATCH)
static const int LZ4_minLength = (MFLIMIT+1);

#define KB (1<<10)
#define MB (1<<20)
#define GB (1U<<30)

#define MAXD_LOG 16
#define MAX_DISTANCE ((1<<MAXD_LOG) - 1)
#define STEPSIZE sizeof(size_t)

#define ML_BITS	4
#define ML_MASK	((1U<<ML_BITS)-1)
#define RUN_BITS (8-ML_BITS)
#define RUN_MASK ((1U<<RUN_BITS)-1)

static const int LZ4_64Klimit = ((64 * KB) + (MFLIMIT-1));
static const U32 LZ4_skipTrigger = 6;

/*-************************************
 *	Reading and writing into memory
 **************************************/

static inline U16 LZ4_read16(const void *memPtr)
{
	U16 val;

	memcpy(&val, memPtr, sizeof(val));

	return val;
}

static inline U32 LZ4_read32(const void *memPtr)
{
	U32 val;

	memcpy(&val, memPtr, sizeof(val));

	return val;
}

static inline size_t LZ4_read_ARCH(const void *memPtr)
{
	size_t val;

	memcpy(&val, memPtr, sizeof(val));

	return val;
}

static inline void LZ4_write16(void *memPtr, U16 value)
{
	memcpy(memPtr, &value, sizeof(value));
}

static inline void LZ4_write32(void *memPtr, U32 value)
{
	memcpy(memPtr, &value, sizeof(value));
}

static inline U16 LZ4_readLE16(const void *memPtr)
{
#ifdef __LITTLE_ENDIAN__
	return LZ4_read16(memPtr);
#else
	const BYTE *p = (const BYTE *)memPtr;

	return (U16)((U16)p[0] + (p[1] << 8));
#endif
}

static inline void LZ4_writeLE16(void *memPtr, U16 value)
{
#ifdef __LITTLE_ENDIAN__
	LZ4_write16(memPtr, value);
#else
	BYTE *p = (BYTE *)memPtr;

	p[0] = (BYTE) value;
	p[1] = (BYTE)(value>>8);
#endif
}

static inline void LZ4_copy8(void *dst, const void *src)
{
	memcpy(dst, src, 8);
}

/*
 * customized variant of memcpy,
 * which can overwrite up to 7 bytes beyond dstEnd
 */
static inline void LZ4_wildCopy(void *dstPtr, const void *srcPtr, void *dstEnd)
{
	BYTE *d = (BYTE *)dstPtr;
	const BYTE *s = (const BYTE *)srcPtr;
	BYTE *const e = (BYTE *)dstEnd;

	do {
		LZ4_copy8(d, s);
		d += 8;
		s += 8;
	} while (d < e);
}

#if LZ4_ARCH64
#ifdef __BIG_ENDIAN__
#define LZ4_NBCOMMONBYTES(val) (__builtin_clzll(val) >> 3)
#else
#define LZ4_NBCOMMONBYTES(val) (__builtin_ctzll(val) >> 3)
#endif
#else
#ifdef __BIG_ENDIAN__
#define LZ4_NBCOMMONBYTES(val) (__builtin_clz(val) >> 3)
#else
#define LZ4_NBCOMMONBYTES(val) (__builtin_ctz(val) >> 3)
#endif
#endif

static inline unsigned int LZ4_count(const BYTE *pIn, const BYTE *pMatch,
	const BYTE *pInLimit)
{
	const BYTE *const pStart = pIn;

	while (likely(pIn < pInLimit-(STEPSIZE-1))) {
		size_t diff = LZ4_read_ARCH(pMatch) ^ LZ4_read_ARCH(pIn);

		if (!diff) {
			pIn += STEPSIZE;
			pMatch += STEPSIZE;
			continue;
		}
		pIn += LZ4_NBCOMMONBYTES(diff);
		return (unsigned int)(pIn - pStart);
	}

#ifdef LZ4_ARCH64
	if ((pIn < (pInLimit-3))
		&& (LZ4_read32(pMatch) == LZ4_read32(pIn))) {
		pIn += 4; pMatch += 4;
	}
#endif
	if ((pIn < (pInLimit-1))
		&& (LZ4_read16(pMatch) == LZ4_read16(pIn))) {
		pIn += 2; pMatch += 2;
	}
	if ((pIn < pInLimit) && (*pMatch == *pIn))
		pIn++;
	return (unsigned int)(pIn - pStart);
}

typedef enum { noLimit = 0, limitedOutput = 1 } limitedOutput_directive;
typedef enum { byPtr, byU32, byU16 } tableType_t;

typedef enum { noDict = 0, withPrefix64k, usingExtDict } dict_directive;
typedef enum { noDictIssue = 0, dictSmall } dictIssue_directive;

typedef enum { endOnOutputSize = 0, endOnInputSize = 1 } endCondition_directive;
typedef enum { full = 0, partial = 1 } earlyEnd_directive;

#endif
