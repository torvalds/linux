/*-
 * Copyright (c) 2001 David E. O'Brien
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
 * $NetBSD: endian.h,v 1.7 1999/08/21 05:53:51 simonb Exp $
 * $FreeBSD$
 */

#ifndef _MACHINE_ENDIAN_H_
#define	_MACHINE_ENDIAN_H_

#include <sys/_types.h>

/*
 * Definitions for byte order, according to byte significance from low
 * address to high.
 */
#define	_LITTLE_ENDIAN  1234    /* LSB first: i386, vax */
#define	_BIG_ENDIAN     4321    /* MSB first: 68000, ibm, net */
#define	_PDP_ENDIAN     3412    /* LSB first in word, MSW first in long */

#define	_BYTE_ORDER	_LITTLE_ENDIAN

#if __BSD_VISIBLE
#define	LITTLE_ENDIAN   _LITTLE_ENDIAN
#define	BIG_ENDIAN      _BIG_ENDIAN
#define	PDP_ENDIAN      _PDP_ENDIAN
#define	BYTE_ORDER      _BYTE_ORDER
#endif

#define	_QUAD_HIGHWORD  1
#define	_QUAD_LOWWORD 0
#define	__ntohl(x)        (__bswap32(x))
#define	__ntohs(x)        (__bswap16(x))
#define	__htonl(x)        (__bswap32(x))
#define	__htons(x)        (__bswap16(x))

static __inline __uint64_t
__bswap64(__uint64_t _x)
{
	__uint64_t ret;

	ret = (_x >> 56);
	ret |= ((_x >> 40) & 0xff00);
	ret |= ((_x >> 24) & 0xff0000);
	ret |= ((_x >>  8) & 0xff000000);
	ret |= ((_x <<  8) & ((__uint64_t)0xff << 32));
	ret |= ((_x << 24) & ((__uint64_t)0xff << 40));
	ret |= ((_x << 40) & ((__uint64_t)0xff << 48));
	ret |= (_x << 56);

	return (ret);
}

static __inline __uint32_t
__bswap32_var(__uint32_t _x)
{

	return ((_x >> 24) | ((_x >> 8) & 0xff00) | ((_x << 8) & 0xff0000) | 
	    ((_x << 24) & 0xff000000));
}

static __inline __uint16_t
__bswap16_var(__uint16_t _x)
{
	__uint32_t ret;

	ret = ((_x >> 8) | ((_x << 8) & 0xff00));

	return ((__uint16_t)ret);
}

#ifdef __OPTIMIZE__

#define	__bswap32_constant(x)	\
    ((((x) & 0xff000000U) >> 24) |	\
     (((x) & 0x00ff0000U) >>  8) |	\
     (((x) & 0x0000ff00U) <<  8) |	\
     (((x) & 0x000000ffU) << 24))

#define	__bswap16_constant(x)	\
    ((((x) & 0xff00) >> 8) |		\
     (((x) & 0x00ff) << 8))

#define	__bswap16(x)	\
    ((__uint16_t)(__builtin_constant_p(x) ?	\
     __bswap16_constant(x) :			\
     __bswap16_var(x)))

#define	__bswap32(x)	\
    ((__uint32_t)(__builtin_constant_p(x) ? 	\
     __bswap32_constant(x) :			\
     __bswap32_var(x)))

#else
#define	__bswap16(x)	__bswap16_var(x)
#define	__bswap32(x)	__bswap32_var(x)

#endif /* __OPTIMIZE__ */
#endif /* !_MACHINE_ENDIAN_H_ */
