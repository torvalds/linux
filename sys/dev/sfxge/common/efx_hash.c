/*-
 * Copyright 2006 Bob Jenkins
 *
 * Derived from public domain source, see
 *     <http://burtleburtle.net/bob/c/lookup3.c>:
 *
 * "lookup3.c, by Bob Jenkins, May 2006, Public Domain.
 *
 *  These are functions for producing 32-bit hashes for hash table lookup...
 *  ...You can use this free for any purpose.  It's in the public domain.
 *  It has no warranty."
 *
 * Copyright (c) 2014-2016 Solarflare Communications Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * The views and conclusions contained in the software and documentation are
 * those of the authors and should not be interpreted as representing official
 * policies, either expressed or implied, of the FreeBSD Project.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "efx.h"
#include "efx_impl.h"

/* Hash initial value */
#define	EFX_HASH_INITIAL_VALUE	0xdeadbeef

/*
 * Rotate a 32-bit value left
 *
 * Allow platform to provide an intrinsic or optimised routine and
 * fall-back to a simple shift based implementation.
 */
#if EFSYS_HAS_ROTL_DWORD

#define	EFX_HASH_ROTATE(_value, _shift)					\
	EFSYS_ROTL_DWORD(_value, _shift)

#else

#define	EFX_HASH_ROTATE(_value, _shift)					\
	(((_value) << (_shift)) | ((_value) >> (32 - (_shift))))

#endif

/* Mix three 32-bit values reversibly */
#define	EFX_HASH_MIX(_a, _b, _c)					\
	do {								\
		_a -= _c;						\
		_a ^= EFX_HASH_ROTATE(_c, 4);				\
		_c += _b;						\
		_b -= _a;						\
		_b ^= EFX_HASH_ROTATE(_a, 6);				\
		_a += _c;						\
		_c -= _b;						\
		_c ^= EFX_HASH_ROTATE(_b, 8);				\
		_b += _a;						\
		_a -= _c;						\
		_a ^= EFX_HASH_ROTATE(_c, 16);				\
		_c += _b;						\
		_b -= _a;						\
		_b ^= EFX_HASH_ROTATE(_a, 19);				\
		_a += _c;						\
		_c -= _b;						\
		_c ^= EFX_HASH_ROTATE(_b, 4);				\
		_b += _a;						\
	_NOTE(CONSTANTCONDITION)					\
	} while (B_FALSE)

/* Final mixing of three 32-bit values into one (_c) */
#define	EFX_HASH_FINALISE(_a, _b, _c)					\
	do {								\
		_c ^= _b;						\
		_c -= EFX_HASH_ROTATE(_b, 14);				\
		_a ^= _c;						\
		_a -= EFX_HASH_ROTATE(_c, 11);				\
		_b ^= _a;						\
		_b -= EFX_HASH_ROTATE(_a, 25);				\
		_c ^= _b;						\
		_c -= EFX_HASH_ROTATE(_b, 16);				\
		_a ^= _c;						\
		_a -= EFX_HASH_ROTATE(_c, 4);				\
		_b ^= _a;						\
		_b -= EFX_HASH_ROTATE(_a, 14);				\
		_c ^= _b;						\
		_c -= EFX_HASH_ROTATE(_b, 24);				\
	_NOTE(CONSTANTCONDITION)					\
	} while (B_FALSE)


/* Produce a 32-bit hash from 32-bit aligned input */
	__checkReturn		uint32_t
efx_hash_dwords(
	__in_ecount(count)	uint32_t const *input,
	__in			size_t count,
	__in			uint32_t init)
{
	uint32_t a;
	uint32_t b;
	uint32_t c;

	/* Set up the initial internal state */
	a = b = c = EFX_HASH_INITIAL_VALUE +
		(((uint32_t)count) * sizeof (uint32_t)) + init;

	/* Handle all but the last three dwords of the input */
	while (count > 3) {
		a += input[0];
		b += input[1];
		c += input[2];
		EFX_HASH_MIX(a, b, c);

		count -= 3;
		input += 3;
	}

	/* Handle the left-overs */
	switch (count) {
	case 3:
		c += input[2];
		/* Fall-through */
	case 2:
		b += input[1];
		/* Fall-through */
	case 1:
		a += input[0];
		EFX_HASH_FINALISE(a, b, c);
		break;

	case 0:
		/* Should only get here if count parameter was zero */
		break;
	}

	return (c);
}

#if EFSYS_IS_BIG_ENDIAN

/* Produce a 32-bit hash from arbitrarily aligned input */
	__checkReturn		uint32_t
efx_hash_bytes(
	__in_ecount(length)	uint8_t const *input,
	__in			size_t length,
	__in			uint32_t init)
{
	uint32_t a;
	uint32_t b;
	uint32_t c;

	/* Set up the initial internal state */
	a = b = c = EFX_HASH_INITIAL_VALUE + (uint32_t)length + init;

	/* Handle all but the last twelve bytes of the input */
	while (length > 12) {
		a += ((uint32_t)input[0]) << 24;
		a += ((uint32_t)input[1]) << 16;
		a += ((uint32_t)input[2]) << 8;
		a += ((uint32_t)input[3]);
		b += ((uint32_t)input[4]) << 24;
		b += ((uint32_t)input[5]) << 16;
		b += ((uint32_t)input[6]) << 8;
		b += ((uint32_t)input[7]);
		c += ((uint32_t)input[8]) << 24;
		c += ((uint32_t)input[9]) << 16;
		c += ((uint32_t)input[10]) << 8;
		c += ((uint32_t)input[11]);
		EFX_HASH_MIX(a, b, c);
		length -= 12;
		input += 12;
	}

	/* Handle the left-overs */
	switch (length) {
	case 12:
		c += ((uint32_t)input[11]);
		/* Fall-through */
	case 11:
		c += ((uint32_t)input[10]) << 8;
		/* Fall-through */
	case 10:
		c += ((uint32_t)input[9]) << 16;
		/* Fall-through */
	case 9:
		c += ((uint32_t)input[8]) << 24;
		/* Fall-through */
	case 8:
		b += ((uint32_t)input[7]);
		/* Fall-through */
	case 7:
		b += ((uint32_t)input[6]) << 8;
		/* Fall-through */
	case 6:
		b += ((uint32_t)input[5]) << 16;
		/* Fall-through */
	case 5:
		b += ((uint32_t)input[4]) << 24;
		/* Fall-through */
	case 4:
		a += ((uint32_t)input[3]);
		/* Fall-through */
	case 3:
		a += ((uint32_t)input[2]) << 8;
		/* Fall-through */
	case 2:
		a += ((uint32_t)input[1]) << 16;
		/* Fall-through */
	case 1:
		a += ((uint32_t)input[0]) << 24;
		EFX_HASH_FINALISE(a, b, c);
		break;

	case 0:
		/* Should only get here if length parameter was zero */
		break;
	}

	return (c);
}

#elif EFSYS_IS_LITTLE_ENDIAN

/* Produce a 32-bit hash from arbitrarily aligned input */
	__checkReturn		uint32_t
efx_hash_bytes(
	__in_ecount(length)	uint8_t const *input,
	__in			size_t length,
	__in			uint32_t init)
{
	uint32_t a;
	uint32_t b;
	uint32_t c;

	/* Set up the initial internal state */
	a = b = c = EFX_HASH_INITIAL_VALUE + (uint32_t)length + init;

	/* Handle all but the last twelve bytes of the input */
	while (length > 12) {
		a += ((uint32_t)input[0]);
		a += ((uint32_t)input[1]) << 8;
		a += ((uint32_t)input[2]) << 16;
		a += ((uint32_t)input[3]) << 24;
		b += ((uint32_t)input[4]);
		b += ((uint32_t)input[5]) << 8;
		b += ((uint32_t)input[6]) << 16;
		b += ((uint32_t)input[7]) << 24;
		c += ((uint32_t)input[8]);
		c += ((uint32_t)input[9]) << 8;
		c += ((uint32_t)input[10]) << 16;
		c += ((uint32_t)input[11]) << 24;
		EFX_HASH_MIX(a, b, c);
		length -= 12;
		input += 12;
	}

	/* Handle the left-overs */
	switch (length) {
	case 12:
		c += ((uint32_t)input[11]) << 24;
		/* Fall-through */
	case 11:
		c += ((uint32_t)input[10]) << 16;
		/* Fall-through */
	case 10:
		c += ((uint32_t)input[9]) << 8;
		/* Fall-through */
	case 9:
		c += ((uint32_t)input[8]);
		/* Fall-through */
	case 8:
		b += ((uint32_t)input[7]) << 24;
		/* Fall-through */
	case 7:
		b += ((uint32_t)input[6]) << 16;
		/* Fall-through */
	case 6:
		b += ((uint32_t)input[5]) << 8;
		/* Fall-through */
	case 5:
		b += ((uint32_t)input[4]);
		/* Fall-through */
	case 4:
		a += ((uint32_t)input[3]) << 24;
		/* Fall-through */
	case 3:
		a += ((uint32_t)input[2]) << 16;
		/* Fall-through */
	case 2:
		a += ((uint32_t)input[1]) << 8;
		/* Fall-through */
	case 1:
		a += ((uint32_t)input[0]);
		EFX_HASH_FINALISE(a, b, c);
		break;

	case 0:
		/* Should only get here if length parameter was zero */
		break;
	}

	return (c);
}

#else

#error "Neither of EFSYS_IS_{BIG,LITTLE}_ENDIAN is set"

#endif
