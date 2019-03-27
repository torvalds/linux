/*-
 * Copyright 2013 John-Mark Gurney <jmg@FreeBSD.org>
 * All rights reserved.
 *
 * Copyright 2015 Netflix, Inc.
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

#ifndef _AESENCDEC_H_
#define _AESENCDEC_H_

#include <crypto/aesni/aesni_os.h>

#include <wmmintrin.h>

static inline void
aesni_enc8(int rounds, const __m128i *keysched, __m128i a,
    __m128i b, __m128i c, __m128i d, __m128i e, __m128i f, __m128i g,
    __m128i h, __m128i out[8])
{
	int i;

	a ^= keysched[0];
	b ^= keysched[0];
	c ^= keysched[0];
	d ^= keysched[0];
	e ^= keysched[0];
	f ^= keysched[0];
	g ^= keysched[0];
	h ^= keysched[0];

	for (i = 0; i < rounds; i++) {
		a = _mm_aesenc_si128(a, keysched[i + 1]);
		b = _mm_aesenc_si128(b, keysched[i + 1]);
		c = _mm_aesenc_si128(c, keysched[i + 1]);
		d = _mm_aesenc_si128(d, keysched[i + 1]);
		e = _mm_aesenc_si128(e, keysched[i + 1]);
		f = _mm_aesenc_si128(f, keysched[i + 1]);
		g = _mm_aesenc_si128(g, keysched[i + 1]);
		h = _mm_aesenc_si128(h, keysched[i + 1]);
	}

	out[0] = _mm_aesenclast_si128(a, keysched[i + 1]);
	out[1] = _mm_aesenclast_si128(b, keysched[i + 1]);
	out[2] = _mm_aesenclast_si128(c, keysched[i + 1]);
	out[3] = _mm_aesenclast_si128(d, keysched[i + 1]);
	out[4] = _mm_aesenclast_si128(e, keysched[i + 1]);
	out[5] = _mm_aesenclast_si128(f, keysched[i + 1]);
	out[6] = _mm_aesenclast_si128(g, keysched[i + 1]);
	out[7] = _mm_aesenclast_si128(h, keysched[i + 1]);
}

static inline void
aesni_dec8(int rounds, const __m128i *keysched, __m128i a,
    __m128i b, __m128i c, __m128i d, __m128i e, __m128i f, __m128i g,
    __m128i h, __m128i out[8])
{
	int i;

	a ^= keysched[0];
	b ^= keysched[0];
	c ^= keysched[0];
	d ^= keysched[0];
	e ^= keysched[0];
	f ^= keysched[0];
	g ^= keysched[0];
	h ^= keysched[0];

	for (i = 0; i < rounds; i++) {
		a = _mm_aesdec_si128(a, keysched[i + 1]);
		b = _mm_aesdec_si128(b, keysched[i + 1]);
		c = _mm_aesdec_si128(c, keysched[i + 1]);
		d = _mm_aesdec_si128(d, keysched[i + 1]);
		e = _mm_aesdec_si128(e, keysched[i + 1]);
		f = _mm_aesdec_si128(f, keysched[i + 1]);
		g = _mm_aesdec_si128(g, keysched[i + 1]);
		h = _mm_aesdec_si128(h, keysched[i + 1]);
	}

	out[0] = _mm_aesdeclast_si128(a, keysched[i + 1]);
	out[1] = _mm_aesdeclast_si128(b, keysched[i + 1]);
	out[2] = _mm_aesdeclast_si128(c, keysched[i + 1]);
	out[3] = _mm_aesdeclast_si128(d, keysched[i + 1]);
	out[4] = _mm_aesdeclast_si128(e, keysched[i + 1]);
	out[5] = _mm_aesdeclast_si128(f, keysched[i + 1]);
	out[6] = _mm_aesdeclast_si128(g, keysched[i + 1]);
	out[7] = _mm_aesdeclast_si128(h, keysched[i + 1]);
}

/* rounds is passed in as rounds - 1 */
static inline __m128i
aesni_enc(int rounds, const __m128i *keysched, const __m128i from)
{
	__m128i tmp;
	int i;

	tmp = from ^ keysched[0];
	for (i = 1; i < rounds; i += 2) {
		tmp = _mm_aesenc_si128(tmp, keysched[i]);
		tmp = _mm_aesenc_si128(tmp, keysched[i + 1]);
	}

	tmp = _mm_aesenc_si128(tmp, keysched[rounds]);
	return _mm_aesenclast_si128(tmp, keysched[rounds + 1]);
}

static inline __m128i
aesni_dec(int rounds, const __m128i *keysched, const __m128i from)
{
	__m128i tmp;
	int i;

	tmp = from ^ keysched[0];

	for (i = 1; i < rounds; i += 2) {
		tmp = _mm_aesdec_si128(tmp, keysched[i]);
		tmp = _mm_aesdec_si128(tmp, keysched[i + 1]);
	}

	tmp = _mm_aesdec_si128(tmp, keysched[rounds]);
	return _mm_aesdeclast_si128(tmp, keysched[rounds + 1]);
}

#endif /* _AESENCDEC_H_ */
