/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2006-2009 Ariff Abdullah <ariff@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _SND_PCM_H_
#define _SND_PCM_H_

#include <sys/param.h>

/*
 * Macros for reading/writing PCM sample / int values from bytes array.
 * Since every process is done using signed integer (and to make our life
 * less miserable), unsigned sample will be converted to its signed
 * counterpart and restored during writing back. To avoid overflow,
 * we truncate 32bit (and only 32bit) samples down to 24bit (see below
 * for the reason), unless SND_PCM_64 is defined.
 */

/*
 * Automatically turn on 64bit arithmetic on suitable archs
 * (amd64 64bit, etc..) for wider 32bit samples / integer processing.
 */
#if LONG_BIT >= 64
#undef SND_PCM_64
#define SND_PCM_64	1
#endif

typedef int32_t intpcm_t;

typedef int32_t intpcm8_t;
typedef int32_t intpcm16_t;
typedef int32_t intpcm24_t;

typedef uint32_t uintpcm_t;

typedef uint32_t uintpcm8_t;
typedef uint32_t uintpcm16_t;
typedef uint32_t uintpcm24_t;

#ifdef SND_PCM_64
typedef int64_t  intpcm32_t;
typedef uint64_t uintpcm32_t;
#else
typedef int32_t  intpcm32_t;
typedef uint32_t uintpcm32_t;
#endif

typedef int64_t intpcm64_t;
typedef uint64_t uintpcm64_t;

/* 32bit fixed point shift */
#define	PCM_FXSHIFT	8

#define PCM_S8_MAX	  0x7f
#define PCM_S8_MIN	 -0x80
#define PCM_S16_MAX	  0x7fff
#define PCM_S16_MIN	 -0x8000
#define PCM_S24_MAX	  0x7fffff
#define PCM_S24_MIN	 -0x800000
#ifdef SND_PCM_64
#if LONG_BIT >= 64
#define PCM_S32_MAX	  0x7fffffffL
#define PCM_S32_MIN	 -0x80000000L
#else
#define PCM_S32_MAX	  0x7fffffffLL
#define PCM_S32_MIN	 -0x80000000LL
#endif
#else
#define PCM_S32_MAX	  0x7fffffff
#define PCM_S32_MIN	(-0x7fffffff - 1)
#endif

/* Bytes-per-sample definition */
#define PCM_8_BPS	1
#define PCM_16_BPS	2
#define PCM_24_BPS	3
#define PCM_32_BPS	4

#define INTPCM_T(v)	((intpcm_t)(v))
#define INTPCM8_T(v)	((intpcm8_t)(v))
#define INTPCM16_T(v)	((intpcm16_t)(v))
#define INTPCM24_T(v)	((intpcm24_t)(v))
#define INTPCM32_T(v)	((intpcm32_t)(v))

#if BYTE_ORDER == LITTLE_ENDIAN
#define _PCM_READ_S16_LE(b8)		INTPCM_T(*((int16_t *)(b8)))
#define _PCM_READ_S32_LE(b8)		INTPCM_T(*((int32_t *)(b8)))
#define _PCM_READ_S16_BE(b8)						\
	INTPCM_T((b8)[1] | (((int8_t)((b8)[0])) << 8))
#define _PCM_READ_S32_BE(b8)						\
	INTPCM_T((b8)[3] | ((b8)[2] << 8) | ((b8)[1] << 16) |		\
	    (((int8_t)((b8)[0])) << 24))

#define _PCM_WRITE_S16_LE(b8, val)	do {				\
	*((int16_t *)(b8)) = (val);					\
} while (0)
#define _PCM_WRITE_S32_LE(b8, val)	do {				\
	*((int32_t *)(b8)) = (val);					\
} while (0)
#define _PCM_WRITE_S16_BE(bb8, vval)	do {				\
	intpcm_t val = (vval); 						\
	uint8_t *b8 = (bb8);						\
	b8[1] = val;							\
	b8[0] = val >> 8;						\
} while (0)
#define _PCM_WRITE_S32_BE(bb8, vval)	do {				\
	intpcm_t val = (vval);						\
	uint8_t *b8 = (bb8);						\
	b8[3] = val;							\
	b8[2] = val >> 8;						\
	b8[1] = val >> 16;						\
	b8[0] = val >> 24;						\
} while (0)

#define _PCM_READ_U16_LE(b8)						\
	INTPCM_T((int16_t)(*((uint16_t *)(b8)) ^ 0x8000))
#define _PCM_READ_U32_LE(b8)						\
	INTPCM_T((int32_t)(*((uint32_t *)(b8)) ^ 0x80000000))
#define _PCM_READ_U16_BE(b8)						\
	INTPCM_T((b8)[1] | (((int8_t)((b8)[0] ^ 0x80)) << 8))
#define _PCM_READ_U32_BE(b8)						\
	INTPCM_T((b8)[3] | ((b8)[2] << 8) | ((b8)[1] << 16) |		\
	    (((int8_t)((b8)[0] ^ 0x80)) << 24))

#define _PCM_WRITE_U16_LE(b8, val)	do {				\
	*((uint16_t *)(b8)) = (val) ^ 0x8000;				\
} while (0)
#define _PCM_WRITE_U32_LE(b8, val)	do {				\
	*((uint32_t *)(b8)) = (val) ^ 0x80000000;			\
} while (0)
#define _PCM_WRITE_U16_BE(bb8, vval)	do {				\
	intpcm_t val = (vval);						\
	uint8_t *b8 = (bb8);						\
	b8[1] = val;							\
	b8[0] = (val >> 8) ^ 0x80;					\
} while (0)
#define _PCM_WRITE_U32_BE(bb8, vval)	do {				\
	intpcm_t val = (vval);						\
	uint8_t *b8 = (bb8);						\
	b8[3] = val;							\
	b8[2] = val >> 8;						\
	b8[1] = val >> 16;						\
	b8[0] = (val >> 24) ^ 0x80;					\
} while (0)

#define _PCM_READ_S16_NE(b8)	_PCM_READ_S16_LE(b8)
#define _PCM_READ_U16_NE(b8)	_PCM_READ_U16_LE(b8)
#define _PCM_READ_S32_NE(b8)	_PCM_READ_S32_LE(b8)
#define _PCM_READ_U32_NE(b8)	_PCM_READ_U32_LE(b8)
#define _PCM_WRITE_S16_NE(b6)	_PCM_WRITE_S16_LE(b8)
#define _PCM_WRITE_U16_NE(b6)	_PCM_WRITE_U16_LE(b8)
#define _PCM_WRITE_S32_NE(b6)	_PCM_WRITE_S32_LE(b8)
#define _PCM_WRITE_U32_NE(b6)	_PCM_WRITE_U32_LE(b8)
#else	/* !LITTLE_ENDIAN */
#define _PCM_READ_S16_LE(b8)						\
	INTPCM_T((b8)[0] | (((int8_t)((b8)[1])) << 8))
#define _PCM_READ_S32_LE(b8)						\
	INTPCM_T((b8)[0] | ((b8)[1] << 8) | ((b8)[2] << 16) |		\
	    (((int8_t)((b8)[3])) << 24))
#define _PCM_READ_S16_BE(b8)		INTPCM_T(*((int16_t *)(b8)))
#define _PCM_READ_S32_BE(b8)		INTPCM_T(*((int32_t *)(b8)))

#define _PCM_WRITE_S16_LE(bb8, vval)	do {				\
	intpcm_t val = (vval);						\
	uint8_t *b8 = (bb8);						\
	b8[0] = val;							\
	b8[1] = val >> 8;						\
} while (0)
#define _PCM_WRITE_S32_LE(bb8, vval)	do {				\
	intpcm_t val = (vval);						\
	uint8_t *b8 = (bb8);						\
	b8[0] = val;							\
	b8[1] = val >> 8;						\
	b8[2] = val >> 16;						\
	b8[3] = val >> 24;						\
} while (0)
#define _PCM_WRITE_S16_BE(b8, val)	do {				\
	*((int16_t *)(b8)) = (val);					\
} while (0)
#define _PCM_WRITE_S32_BE(b8, val)	do {				\
	*((int32_t *)(b8)) = (val);					\
} while (0)

#define _PCM_READ_U16_LE(b8)						\
	INTPCM_T((b8)[0] | (((int8_t)((b8)[1] ^ 0x80)) << 8))
#define _PCM_READ_U32_LE(b8)						\
	INTPCM_T((b8)[0] | ((b8)[1] << 8) | ((b8)[2] << 16) |		\
	    (((int8_t)((b8)[3] ^ 0x80)) << 24))
#define _PCM_READ_U16_BE(b8)						\
	INTPCM_T((int16_t)(*((uint16_t *)(b8)) ^ 0x8000))
#define _PCM_READ_U32_BE(b8)						\
	INTPCM_T((int32_t)(*((uint32_t *)(b8)) ^ 0x80000000))

#define _PCM_WRITE_U16_LE(bb8, vval)	do {				\
	intpcm_t val = (vval);						\
	uint8_t *b8 = (bb8);						\
	b8[0] = val;							\
	b8[1] = (val >> 8) ^ 0x80;					\
} while (0)
#define _PCM_WRITE_U32_LE(bb8, vval)	do {				\
	intpcm_t val = (vval);						\
	uint8_t *b8 = (bb8);						\
	b8[0] = val;							\
	b8[1] = val >> 8;						\
	b8[2] = val >> 16;						\
	b8[3] = (val >> 24) ^ 0x80;					\
} while (0)
#define _PCM_WRITE_U16_BE(b8, val)	do {				\
	*((uint16_t *)(b8)) = (val) ^ 0x8000;				\
} while (0)
#define _PCM_WRITE_U32_BE(b8, val)	do {				\
	*((uint32_t *)(b8)) = (val) ^ 0x80000000;			\
} while (0)

#define _PCM_READ_S16_NE(b8)	_PCM_READ_S16_BE(b8)
#define _PCM_READ_U16_NE(b8)	_PCM_READ_U16_BE(b8)
#define _PCM_READ_S32_NE(b8)	_PCM_READ_S32_BE(b8)
#define _PCM_READ_U32_NE(b8)	_PCM_READ_U32_BE(b8)
#define _PCM_WRITE_S16_NE(b6)	_PCM_WRITE_S16_BE(b8)
#define _PCM_WRITE_U16_NE(b6)	_PCM_WRITE_U16_BE(b8)
#define _PCM_WRITE_S32_NE(b6)	_PCM_WRITE_S32_BE(b8)
#define _PCM_WRITE_U32_NE(b6)	_PCM_WRITE_U32_BE(b8)
#endif	/* LITTLE_ENDIAN */

#define _PCM_READ_S24_LE(b8)						\
	INTPCM_T((b8)[0] | ((b8)[1] << 8) | (((int8_t)((b8)[2])) << 16))
#define _PCM_READ_S24_BE(b8)						\
	INTPCM_T((b8)[2] | ((b8)[1] << 8) | (((int8_t)((b8)[0])) << 16))

#define _PCM_WRITE_S24_LE(bb8, vval)	do {				\
	intpcm_t val = (vval);						\
	uint8_t *b8 = (bb8);						\
	b8[0] = val;							\
	b8[1] = val >> 8;						\
	b8[2] = val >> 16;						\
} while (0)
#define _PCM_WRITE_S24_BE(bb8, vval)	do {				\
	intpcm_t val = (vval);						\
	uint8_t *b8 = (bb8);						\
	b8[2] = val;							\
	b8[1] = val >> 8;						\
	b8[0] = val >> 16;						\
} while (0)

#define _PCM_READ_U24_LE(b8)						\
	INTPCM_T((b8)[0] | ((b8)[1] << 8) |				\
	    (((int8_t)((b8)[2] ^ 0x80)) << 16))
#define _PCM_READ_U24_BE(b8)						\
	INTPCM_T((b8)[2] | ((b8)[1] << 8) |				\
	    (((int8_t)((b8)[0] ^ 0x80)) << 16))

#define _PCM_WRITE_U24_LE(bb8, vval)	do {				\
	intpcm_t val = (vval);						\
	uint8_t *b8 = (bb8);						\
	b8[0] = val;							\
	b8[1] = val >> 8;						\
	b8[2] = (val >> 16) ^ 0x80;					\
} while (0)
#define _PCM_WRITE_U24_BE(bb8, vval)	do {				\
	intpcm_t val = (vval);						\
	uint8_t *b8 = (bb8);						\
	b8[2] = val;							\
	b8[1] = val >> 8;						\
	b8[0] = (val >> 16) ^ 0x80;					\
} while (0)

#if BYTE_ORDER == LITTLE_ENDIAN
#define _PCM_READ_S24_NE(b8)	_PCM_READ_S24_LE(b8)
#define _PCM_READ_U24_NE(b8)	_PCM_READ_U24_LE(b8)
#define _PCM_WRITE_S24_NE(b6)	_PCM_WRITE_S24_LE(b8)
#define _PCM_WRITE_U24_NE(b6)	_PCM_WRITE_U24_LE(b8)
#else	/* !LITTLE_ENDIAN */
#define _PCM_READ_S24_NE(b8)	_PCM_READ_S24_BE(b8)
#define _PCM_READ_U24_NE(b8)	_PCM_READ_U24_BE(b8)
#define _PCM_WRITE_S24_NE(b6)	_PCM_WRITE_S24_BE(b8)
#define _PCM_WRITE_U24_NE(b6)	_PCM_WRITE_U24_BE(b8)
#endif	/* LITTLE_ENDIAN */
/*
 * 8bit sample is pretty much useless since it doesn't provide
 * sufficient dynamic range throughout our filtering process.
 * For the sake of completeness, declare it anyway.
 */
#define _PCM_READ_S8_NE(b8)		INTPCM_T(*((int8_t *)(b8)))
#define _PCM_READ_U8_NE(b8)						\
	INTPCM_T((int8_t)(*((uint8_t *)(b8)) ^ 0x80))

#define _PCM_WRITE_S8_NE(b8, val)	do {				\
	*((int8_t *)(b8)) = (val);					\
} while (0)
#define _PCM_WRITE_U8_NE(b8, val)	do {				\
	*((uint8_t *)(b8)) = (val) ^ 0x80;				\
} while (0)

/*
 * Common macross. Use this instead of "_", unless we want
 * the real sample value.
 */

/* 8bit */
#define PCM_READ_S8_NE(b8)		_PCM_READ_S8_NE(b8)
#define PCM_READ_U8_NE(b8)		_PCM_READ_U8_NE(b8)
#define PCM_WRITE_S8_NE(b8, val)	_PCM_WRITE_S8_NE(b8, val)
#define PCM_WRITE_U8_NE(b8, val)	_PCM_WRITE_U8_NE(b8, val)

/* 16bit */
#define PCM_READ_S16_LE(b8)		_PCM_READ_S16_LE(b8)
#define PCM_READ_S16_BE(b8)		_PCM_READ_S16_BE(b8)
#define PCM_READ_U16_LE(b8)		_PCM_READ_U16_LE(b8)
#define PCM_READ_U16_BE(b8)		_PCM_READ_U16_BE(b8)

#define PCM_WRITE_S16_LE(b8, val)	_PCM_WRITE_S16_LE(b8, val)
#define PCM_WRITE_S16_BE(b8, val)	_PCM_WRITE_S16_BE(b8, val)
#define PCM_WRITE_U16_LE(b8, val)	_PCM_WRITE_U16_LE(b8, val)
#define PCM_WRITE_U16_BE(b8, val)	_PCM_WRITE_U16_BE(b8, val)

#define PCM_READ_S16_NE(b8)		_PCM_READ_S16_NE(b8)
#define PCM_READ_U16_NE(b8)		_PCM_READ_U16_NE(b8)
#define PCM_WRITE_S16_NE(b8)		_PCM_WRITE_S16_NE(b8)
#define PCM_WRITE_U16_NE(b8)		_PCM_WRITE_U16_NE(b8)

/* 24bit */
#define PCM_READ_S24_LE(b8)		_PCM_READ_S24_LE(b8)
#define PCM_READ_S24_BE(b8)		_PCM_READ_S24_BE(b8)
#define PCM_READ_U24_LE(b8)		_PCM_READ_U24_LE(b8)
#define PCM_READ_U24_BE(b8)		_PCM_READ_U24_BE(b8)

#define PCM_WRITE_S24_LE(b8, val)	_PCM_WRITE_S24_LE(b8, val)
#define PCM_WRITE_S24_BE(b8, val)	_PCM_WRITE_S24_BE(b8, val)
#define PCM_WRITE_U24_LE(b8, val)	_PCM_WRITE_U24_LE(b8, val)
#define PCM_WRITE_U24_BE(b8, val)	_PCM_WRITE_U24_BE(b8, val)

#define PCM_READ_S24_NE(b8)		_PCM_READ_S24_NE(b8)
#define PCM_READ_U24_NE(b8)		_PCM_READ_U24_NE(b8)
#define PCM_WRITE_S24_NE(b8)		_PCM_WRITE_S24_NE(b8)
#define PCM_WRITE_U24_NE(b8)		_PCM_WRITE_U24_NE(b8)

/* 32bit */
#ifdef SND_PCM_64
#define PCM_READ_S32_LE(b8)		_PCM_READ_S32_LE(b8)
#define PCM_READ_S32_BE(b8)		_PCM_READ_S32_BE(b8)
#define PCM_READ_U32_LE(b8)		_PCM_READ_U32_LE(b8)
#define PCM_READ_U32_BE(b8)		_PCM_READ_U32_BE(b8)

#define PCM_WRITE_S32_LE(b8, val)	_PCM_WRITE_S32_LE(b8, val)
#define PCM_WRITE_S32_BE(b8, val)	_PCM_WRITE_S32_BE(b8, val)
#define PCM_WRITE_U32_LE(b8, val)	_PCM_WRITE_U32_LE(b8, val)
#define PCM_WRITE_U32_BE(b8, val)	_PCM_WRITE_U32_BE(b8, val)

#define PCM_READ_S32_NE(b8)		_PCM_READ_S32_NE(b8)
#define PCM_READ_U32_NE(b8)		_PCM_READ_U32_NE(b8)
#define PCM_WRITE_S32_NE(b8)		_PCM_WRITE_S32_NE(b8)
#define PCM_WRITE_U32_NE(b8)		_PCM_WRITE_U32_NE(b8)
#else	/* !SND_PCM_64 */
/*
 * 24bit integer ?!? This is quite unfortunate, eh? Get the fact straight:
 * Dynamic range for:
 *	1) Human =~ 140db
 *	2) 16bit = 96db (close enough)
 *	3) 24bit = 144db (perfect)
 *	4) 32bit = 196db (way too much)
 *	5) Bugs Bunny = Gazillion!@%$Erbzzztt-EINVAL db
 * Since we're not Bugs Bunny ..uh..err.. avoiding 64bit arithmetic, 24bit
 * is pretty much sufficient for our signed integer processing.
 */
#define PCM_READ_S32_LE(b8)		(_PCM_READ_S32_LE(b8) >> PCM_FXSHIFT)
#define PCM_READ_S32_BE(b8)		(_PCM_READ_S32_BE(b8) >> PCM_FXSHIFT)
#define PCM_READ_U32_LE(b8)		(_PCM_READ_U32_LE(b8) >> PCM_FXSHIFT)
#define PCM_READ_U32_BE(b8)		(_PCM_READ_U32_BE(b8) >> PCM_FXSHIFT)

#define PCM_READ_S32_NE(b8)		(_PCM_READ_S32_NE(b8) >> PCM_FXSHIFT)
#define PCM_READ_U32_NE(b8)		(_PCM_READ_U32_NE(b8) >> PCM_FXSHIFT)

#define PCM_WRITE_S32_LE(b8, val)					\
			_PCM_WRITE_S32_LE(b8, (val) << PCM_FXSHIFT)
#define PCM_WRITE_S32_BE(b8, val)					\
			_PCM_WRITE_S32_BE(b8, (val) << PCM_FXSHIFT)
#define PCM_WRITE_U32_LE(b8, val)					\
			_PCM_WRITE_U32_LE(b8, (val) << PCM_FXSHIFT)
#define PCM_WRITE_U32_BE(b8, val)					\
			_PCM_WRITE_U32_BE(b8, (val) << PCM_FXSHIFT)

#define PCM_WRITE_S32_NE(b8, val)					\
			_PCM_WRITE_S32_NE(b8, (val) << PCM_FXSHIFT)
#define PCM_WRITE_U32_NE(b8, val)					\
			_PCM_WRITE_U32_NE(b8, (val) << PCM_FXSHIFT)
#endif	/* SND_PCM_64 */

#define PCM_CLAMP_S8(val)						\
			(((val) > PCM_S8_MAX) ? PCM_S8_MAX :		\
			 (((val) < PCM_S8_MIN) ? PCM_S8_MIN : (val)))
#define PCM_CLAMP_S16(val)						\
			(((val) > PCM_S16_MAX) ? PCM_S16_MAX :		\
			 (((val) < PCM_S16_MIN) ? PCM_S16_MIN : (val)))
#define PCM_CLAMP_S24(val)						\
			(((val) > PCM_S24_MAX) ? PCM_S24_MAX :		\
			 (((val) < PCM_S24_MIN) ? PCM_S24_MIN : (val)))

#ifdef SND_PCM_64
#define PCM_CLAMP_S32(val)						\
			(((val) > PCM_S32_MAX) ? PCM_S32_MAX :		\
			 (((val) < PCM_S32_MIN) ? PCM_S32_MIN : (val)))
#else	/* !SND_PCM_64 */
#define PCM_CLAMP_S32(val)						\
			(((val) > PCM_S24_MAX) ? PCM_S32_MAX :		\
			 (((val) < PCM_S24_MIN) ? PCM_S32_MIN :		\
			 ((val) << PCM_FXSHIFT)))
#endif	/* SND_PCM_64 */

#define PCM_CLAMP_U8(val)	PCM_CLAMP_S8(val)
#define PCM_CLAMP_U16(val)	PCM_CLAMP_S16(val)
#define PCM_CLAMP_U24(val)	PCM_CLAMP_S24(val)
#define PCM_CLAMP_U32(val)	PCM_CLAMP_S32(val)

#endif	/* !_SND_PCM_H_ */
