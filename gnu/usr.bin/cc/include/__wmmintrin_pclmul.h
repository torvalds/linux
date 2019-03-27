/*-
 * Copyright 2013 John-Mark Gurney
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
 *
 */

#ifndef _WMMINTRIN_PCLMUL_H_
#define _WMMINTRIN_PCLMUL_H_

#include <emmintrin.h>

/*
 * c selects which parts of a and b to multiple:
 *  0x00:	a[ 63: 0] * b[ 63: 0]
 *  0x01:	a[127:64] * b[ 63: 0]
 *  0x10:	a[ 63: 0] * b[127:64]
 *  0x11:	a[127:64] * b[127:64]
 */
#define _mm_clmulepi64_si128(a, b, c) 					\
({									\
	__m128i _a = (a);						\
	__m128i _b = (b);						\
									\
	__asm__("pclmulqdq %3, %2, %0": "=x" (_a): "0" (_a), "xm" (_b),	\
	    "i" (c));							\
									\
	_a;								\
})

#endif  /* _WMMINTRIN_PCLMUL_H_ */
