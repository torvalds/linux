/*-
 * Copyright (c) 2015 Mark R V Murray
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer
 *    in this position and unchanged.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef SYS_DEV_RANDOM_UINT128_H_INCLUDED
#define	SYS_DEV_RANDOM_UINT128_H_INCLUDED

#include <sys/endian.h>

/* This whole thing is a crock :-(
 *
 * Everyone knows you always need the __uint128_t types!
 */

#ifdef __SIZEOF_INT128__
#define	USE_REAL_UINT128_T
#endif

#ifdef USE_REAL_UINT128_T
typedef __uint128_t uint128_t;
#define	UINT128_ZERO 0ULL
#else
typedef struct {
	/* Ignore endianness */
	uint64_t u128t_word0;
	uint64_t u128t_word1;
} uint128_t;
static const uint128_t very_long_zero = {0UL,0UL};
#define	UINT128_ZERO very_long_zero
#endif

static __inline void
uint128_increment(uint128_t *big_uintp)
{
#ifdef USE_REAL_UINT128_T
	(*big_uintp)++;
#else
	big_uintp->u128t_word0++;
	if (big_uintp->u128t_word0 == 0UL)
		big_uintp->u128t_word1++;
#endif
}

static __inline bool
uint128_equals(uint128_t a, uint128_t b)
{
#ifdef USE_REAL_UINT128_T
	return (a == b);
#else
	return (a.u128t_word0 == b.u128t_word0 &&
	    a.u128t_word1 == b.u128t_word1);
#endif
}

static __inline int
uint128_is_zero(uint128_t big_uint)
{
	return (uint128_equals(big_uint, UINT128_ZERO));
}

static __inline uint128_t
le128dec(const void *pp)
{
	const uint8_t *p = pp;

#ifdef USE_REAL_UINT128_T
	return (((uint128_t)le64dec(p + 8) << 64) | le64dec(p));
#else
	return ((uint128_t){
	    .u128t_word0 = le64dec(p),
	    .u128t_word1 = le64dec(p + 8),
	    });
#endif
}

static __inline void
le128enc(void *pp, uint128_t u)
{
	uint8_t *p = pp;

#ifdef USE_REAL_UINT128_T
	le64enc(p, (uint64_t)(u & UINT64_MAX));
	le64enc(p + 8, (uint64_t)(u >> 64));
#else
	le64enc(p, u.u128t_word0);
	le64enc(p + 8, u.u128t_word1);
#endif
}

#endif /* SYS_DEV_RANDOM_UINT128_H_INCLUDED */
