/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1987, 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)endian.h	8.1 (Berkeley) 6/10/93
 * $FreeBSD$
 */

#ifndef _MACHINE_ENDIAN_H_
#define	_MACHINE_ENDIAN_H_

#include <sys/cdefs.h>
#include <sys/_types.h>

/*
 * Define the order of 32-bit words in 64-bit words.
 */
#ifdef __LITTLE_ENDIAN__
#define	_QUAD_HIGHWORD 1
#define	_QUAD_LOWWORD 0
#else
#define	_QUAD_HIGHWORD 0
#define	_QUAD_LOWWORD 1
#endif

/*
 * GCC defines _BIG_ENDIAN and _LITTLE_ENDIAN equal to __BIG_ENDIAN__
 * and __LITTLE_ENDIAN__ (resp).
 */
#ifdef _BIG_ENDIAN
#undef _BIG_ENDIAN
#endif
#ifdef _LITTLE_ENDIAN
#undef _LITTLE_ENDIAN
#endif

/*
 * Definitions for byte order, according to byte significance from low
 * address to high.
 */
#define	_LITTLE_ENDIAN	1234	/* LSB first: i386, vax */
#define	_BIG_ENDIAN	4321	/* MSB first: 68000, ibm, net */
#define	_PDP_ENDIAN	3412	/* LSB first in word, MSW first in long */

#ifdef __LITTLE_ENDIAN__
#define	_BYTE_ORDER	_LITTLE_ENDIAN
#else
#define	_BYTE_ORDER	_BIG_ENDIAN
#endif

/*
 * Deprecated variants that don't have enough underscores to be useful in more
 * strict namespaces.
 */
#if __BSD_VISIBLE
#define	LITTLE_ENDIAN	_LITTLE_ENDIAN
#define	BIG_ENDIAN	_BIG_ENDIAN
#define	PDP_ENDIAN	_PDP_ENDIAN
#define	BYTE_ORDER	_BYTE_ORDER
#endif

#if defined(__GNUCLIKE_BUILTIN_CONSTANT_P)
#define	__is_constant(x)	__builtin_constant_p(x)
#else
#define	__is_constant(x)	0
#endif

#define	__bswap16_const(x)	((((__uint16_t)(x) >> 8) & 0xff) |	\
	(((__uint16_t)(x) << 8) & 0xff00))
#define	__bswap32_const(x)	((((__uint32_t)(x) >> 24) & 0xff) |	\
	(((__uint32_t)(x) >> 8) & 0xff00) |				\
	(((__uint32_t)(x)<< 8) & 0xff0000) |				\
	(((__uint32_t)(x) << 24) & 0xff000000))
#define	__bswap64_const(x)	((((__uint64_t)(x) >> 56) & 0xff) |	\
	(((__uint64_t)(x) >> 40) & 0xff00) |				\
	(((__uint64_t)(x) >> 24) & 0xff0000) |				\
	(((__uint64_t)(x) >> 8) & 0xff000000) |				\
	(((__uint64_t)(x) << 8) & ((__uint64_t)0xff << 32)) |		\
	(((__uint64_t)(x) << 24) & ((__uint64_t)0xff << 40)) |		\
	(((__uint64_t)(x) << 40) & ((__uint64_t)0xff << 48)) |		\
	(((__uint64_t)(x) << 56) & ((__uint64_t)0xff << 56)))

static __inline __uint16_t
__bswap16_var(__uint16_t _x)
{

	return ((_x >> 8) | ((_x << 8) & 0xff00));
}

static __inline __uint32_t
__bswap32_var(__uint32_t _x)
{

	return ((_x >> 24) | ((_x >> 8) & 0xff00) | ((_x << 8) & 0xff0000) |
	    ((_x << 24) & 0xff000000));
}

static __inline __uint64_t
__bswap64_var(__uint64_t _x)
{

	return ((_x >> 56) | ((_x >> 40) & 0xff00) | ((_x >> 24) & 0xff0000) |
	    ((_x >> 8) & 0xff000000) | ((_x << 8) & ((__uint64_t)0xff << 32)) |
	    ((_x << 24) & ((__uint64_t)0xff << 40)) |
	    ((_x << 40) & ((__uint64_t)0xff << 48)) | ((_x << 56)));
}

#define	__bswap16(x)	((__uint16_t)(__is_constant(x) ? __bswap16_const(x) : \
	__bswap16_var(x)))
#define	__bswap32(x)	(__is_constant(x) ? __bswap32_const(x) : \
	__bswap32_var(x))
#define	__bswap64(x)	(__is_constant(x) ? __bswap64_const(x) : \
	__bswap64_var(x))

#ifdef __LITTLE_ENDIAN__
#define	__htonl(x)	(__bswap32((__uint32_t)(x)))
#define	__htons(x)	(__bswap16((__uint16_t)(x)))
#define	__ntohl(x)	(__bswap32((__uint32_t)(x)))
#define	__ntohs(x)	(__bswap16((__uint16_t)(x)))
#else
#define	__htonl(x)	((__uint32_t)(x))
#define	__htons(x)	((__uint16_t)(x))
#define	__ntohl(x)	((__uint32_t)(x))
#define	__ntohs(x)	((__uint16_t)(x))
#endif

#endif /* !_MACHINE_ENDIAN_H_ */
