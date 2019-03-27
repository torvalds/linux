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

#ifndef _WMMINTRIN_AES_H_
#define _WMMINTRIN_AES_H_

#include <emmintrin.h>

#define MAKE_AES(name) \
static __inline__ __m128i __attribute__((__always_inline__, __nodebug__)) \
_mm_## name ##_si128(__m128i __V, __m128i __R) \
{ \
  __m128i v = __V; \
 \
  __asm__ (#name " %2, %0": "=x" (v): "0" (v), "xm" (__R)); \
 \
  return v; \
}

MAKE_AES(aesimc)
MAKE_AES(aesenc)
MAKE_AES(aesenclast)
MAKE_AES(aesdec)
MAKE_AES(aesdeclast)

#undef MAKE_AES

#endif  /* _WMMINTRIN_AES_H_ */
