/*-
 * Copyright (c) 2014 The FreeBSD Foundation
 * All rights reserved.
 *
 * This software was developed by John-Mark Gurney under
 * the sponsorship of the FreeBSD Foundation and
 * Rubicon Communications, LLC (Netgate).
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1.  Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 * 2.  Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *
 *	$FreeBSD$
 *
 */

/*
 * Figure 5, 8 and 12 are copied from the Intel white paper:
 * Intel® Carry-Less Multiplication Instruction and its Usage for
 * Computing the GCM Mode
 *
 * and as such are:
 * Copyright © 2010 Intel Corporation.
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *   * Neither the name of Intel Corporation nor the
 *     names of its contributors may be used to endorse or promote products
 *     derived from this software without specific prior written permission.
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
 */

#ifdef _KERNEL
#include <crypto/aesni/aesni.h>
#include <crypto/aesni/aesni_os.h>
#else
#include <stdint.h>
#endif

#include <wmmintrin.h>
#include <emmintrin.h>
#include <smmintrin.h>

static inline int
m128icmp(__m128i a, __m128i b)
{
	__m128i cmp;

	cmp = _mm_cmpeq_epi32(a, b);

	return _mm_movemask_epi8(cmp) == 0xffff;
}

#ifdef __i386__
static inline __m128i
_mm_insert_epi64(__m128i a, int64_t b, const int ndx)
{  

	if (!ndx) {
		a = _mm_insert_epi32(a, b, 0);
		a = _mm_insert_epi32(a, b >> 32, 1);
	} else {
		a = _mm_insert_epi32(a, b, 2);
		a = _mm_insert_epi32(a, b >> 32, 3);
	}

	return a;
}
#endif

/* some code from carry-less-multiplication-instruction-in-gcm-mode-paper.pdf */

/* Figure 5. Code Sample - Performing Ghash Using Algorithms 1 and 5 (C) */
static void
gfmul(__m128i a, __m128i b, __m128i *res)
{
	__m128i tmp2, tmp3, tmp4, tmp5, tmp6, tmp7, tmp8, tmp9;

	tmp3 = _mm_clmulepi64_si128(a, b, 0x00);
	tmp4 = _mm_clmulepi64_si128(a, b, 0x10);
	tmp5 = _mm_clmulepi64_si128(a, b, 0x01);
	tmp6 = _mm_clmulepi64_si128(a, b, 0x11);

	tmp4 = _mm_xor_si128(tmp4, tmp5);
	tmp5 = _mm_slli_si128(tmp4, 8);
	tmp4 = _mm_srli_si128(tmp4, 8);
	tmp3 = _mm_xor_si128(tmp3, tmp5);
	tmp6 = _mm_xor_si128(tmp6, tmp4);

	tmp7 = _mm_srli_epi32(tmp3, 31);
	tmp8 = _mm_srli_epi32(tmp6, 31);
	tmp3 = _mm_slli_epi32(tmp3, 1);
	tmp6 = _mm_slli_epi32(tmp6, 1);

	tmp9 = _mm_srli_si128(tmp7, 12);
	tmp8 = _mm_slli_si128(tmp8, 4);
	tmp7 = _mm_slli_si128(tmp7, 4);
	tmp3 = _mm_or_si128(tmp3, tmp7);
	tmp6 = _mm_or_si128(tmp6, tmp8);
	tmp6 = _mm_or_si128(tmp6, tmp9);

	tmp7 = _mm_slli_epi32(tmp3, 31);
	tmp8 = _mm_slli_epi32(tmp3, 30);
	tmp9 = _mm_slli_epi32(tmp3, 25);

	tmp7 = _mm_xor_si128(tmp7, tmp8);
	tmp7 = _mm_xor_si128(tmp7, tmp9);
	tmp8 = _mm_srli_si128(tmp7, 4);
	tmp7 = _mm_slli_si128(tmp7, 12);
	tmp3 = _mm_xor_si128(tmp3, tmp7);

	tmp2 = _mm_srli_epi32(tmp3, 1);
	tmp4 = _mm_srli_epi32(tmp3, 2);
	tmp5 = _mm_srli_epi32(tmp3, 7);
	tmp2 = _mm_xor_si128(tmp2, tmp4);
	tmp2 = _mm_xor_si128(tmp2, tmp5);
	tmp2 = _mm_xor_si128(tmp2, tmp8);
	tmp3 = _mm_xor_si128(tmp3, tmp2);
	tmp6 = _mm_xor_si128(tmp6, tmp3);

	*res = tmp6;
}

/*
 * Figure 8. Code Sample - Performing Ghash Using an Aggregated Reduction
 * Method */
static void
reduce4(__m128i H1, __m128i H2, __m128i H3, __m128i H4,
    __m128i X1, __m128i X2, __m128i X3, __m128i X4, __m128i *res)
{
	/*algorithm by Krzysztof Jankowski, Pierre Laurent - Intel*/
	__m128i H1_X1_lo, H1_X1_hi, H2_X2_lo, H2_X2_hi, H3_X3_lo,
	    H3_X3_hi, H4_X4_lo, H4_X4_hi, lo, hi;
	__m128i tmp0, tmp1, tmp2, tmp3;
	__m128i tmp4, tmp5, tmp6, tmp7;
	__m128i tmp8, tmp9;

	H1_X1_lo = _mm_clmulepi64_si128(H1, X1, 0x00);
	H2_X2_lo = _mm_clmulepi64_si128(H2, X2, 0x00);
	H3_X3_lo = _mm_clmulepi64_si128(H3, X3, 0x00);
	H4_X4_lo = _mm_clmulepi64_si128(H4, X4, 0x00);

	lo = _mm_xor_si128(H1_X1_lo, H2_X2_lo);
	lo = _mm_xor_si128(lo, H3_X3_lo);
	lo = _mm_xor_si128(lo, H4_X4_lo);

	H1_X1_hi = _mm_clmulepi64_si128(H1, X1, 0x11);
	H2_X2_hi = _mm_clmulepi64_si128(H2, X2, 0x11);
	H3_X3_hi = _mm_clmulepi64_si128(H3, X3, 0x11);
	H4_X4_hi = _mm_clmulepi64_si128(H4, X4, 0x11);

	hi = _mm_xor_si128(H1_X1_hi, H2_X2_hi);
	hi = _mm_xor_si128(hi, H3_X3_hi);
	hi = _mm_xor_si128(hi, H4_X4_hi);

	tmp0 = _mm_shuffle_epi32(H1, 78);
	tmp4 = _mm_shuffle_epi32(X1, 78);
	tmp0 = _mm_xor_si128(tmp0, H1);
	tmp4 = _mm_xor_si128(tmp4, X1);
	tmp1 = _mm_shuffle_epi32(H2, 78);
	tmp5 = _mm_shuffle_epi32(X2, 78);
	tmp1 = _mm_xor_si128(tmp1, H2);
	tmp5 = _mm_xor_si128(tmp5, X2);
	tmp2 = _mm_shuffle_epi32(H3, 78);
	tmp6 = _mm_shuffle_epi32(X3, 78);
	tmp2 = _mm_xor_si128(tmp2, H3);
	tmp6 = _mm_xor_si128(tmp6, X3);
	tmp3 = _mm_shuffle_epi32(H4, 78);
	tmp7 = _mm_shuffle_epi32(X4, 78);
	tmp3 = _mm_xor_si128(tmp3, H4);
	tmp7 = _mm_xor_si128(tmp7, X4);

	tmp0 = _mm_clmulepi64_si128(tmp0, tmp4, 0x00);
	tmp1 = _mm_clmulepi64_si128(tmp1, tmp5, 0x00);
	tmp2 = _mm_clmulepi64_si128(tmp2, tmp6, 0x00);
	tmp3 = _mm_clmulepi64_si128(tmp3, tmp7, 0x00);

	tmp0 = _mm_xor_si128(tmp0, lo);
	tmp0 = _mm_xor_si128(tmp0, hi);
	tmp0 = _mm_xor_si128(tmp1, tmp0);
	tmp0 = _mm_xor_si128(tmp2, tmp0);
	tmp0 = _mm_xor_si128(tmp3, tmp0);

	tmp4 = _mm_slli_si128(tmp0, 8);
	tmp0 = _mm_srli_si128(tmp0, 8);

	lo = _mm_xor_si128(tmp4, lo);
	hi = _mm_xor_si128(tmp0, hi);

	tmp3 = lo;
	tmp6 = hi;

	tmp7 = _mm_srli_epi32(tmp3, 31);
	tmp8 = _mm_srli_epi32(tmp6, 31);
	tmp3 = _mm_slli_epi32(tmp3, 1);
	tmp6 = _mm_slli_epi32(tmp6, 1);

	tmp9 = _mm_srli_si128(tmp7, 12);
	tmp8 = _mm_slli_si128(tmp8, 4);
	tmp7 = _mm_slli_si128(tmp7, 4);
	tmp3 = _mm_or_si128(tmp3, tmp7);
	tmp6 = _mm_or_si128(tmp6, tmp8);
	tmp6 = _mm_or_si128(tmp6, tmp9);

	tmp7 = _mm_slli_epi32(tmp3, 31);
	tmp8 = _mm_slli_epi32(tmp3, 30);
	tmp9 = _mm_slli_epi32(tmp3, 25);

	tmp7 = _mm_xor_si128(tmp7, tmp8);
	tmp7 = _mm_xor_si128(tmp7, tmp9);
	tmp8 = _mm_srli_si128(tmp7, 4);
	tmp7 = _mm_slli_si128(tmp7, 12);
	tmp3 = _mm_xor_si128(tmp3, tmp7);

	tmp2 = _mm_srli_epi32(tmp3, 1);
	tmp4 = _mm_srli_epi32(tmp3, 2);
	tmp5 = _mm_srli_epi32(tmp3, 7);
	tmp2 = _mm_xor_si128(tmp2, tmp4);
	tmp2 = _mm_xor_si128(tmp2, tmp5);
	tmp2 = _mm_xor_si128(tmp2, tmp8);
	tmp3 = _mm_xor_si128(tmp3, tmp2);
	tmp6 = _mm_xor_si128(tmp6, tmp3);

	*res = tmp6;
}

/*
 * Figure 12. AES-GCM: Processing Four Blocks in Parallel with Aggregated
 * Every Four Blocks
 */
/*
 * per NIST SP-800-38D, 5.2.1.1, len(p) <= 2^39-256 (in bits), or
 * 2^32-256*8*16 bytes.
 */
void
AES_GCM_encrypt(const unsigned char *in, unsigned char *out,
	const unsigned char *addt, const unsigned char *ivec,
	unsigned char *tag, uint32_t nbytes, uint32_t abytes, int ibytes,
	const unsigned char *key, int nr)
{
	int i, j ,k;
	__m128i tmp1, tmp2, tmp3, tmp4;
	__m128i tmp5, tmp6, tmp7, tmp8;
	__m128i H, H2, H3, H4, Y, T;
	const __m128i *KEY = (const __m128i *)key;
	__m128i ctr1, ctr2, ctr3, ctr4;
	__m128i ctr5, ctr6, ctr7, ctr8;
	__m128i last_block = _mm_setzero_si128();
	__m128i ONE = _mm_set_epi32(0, 1, 0, 0);
	__m128i EIGHT = _mm_set_epi32(0, 8, 0, 0);
	__m128i BSWAP_EPI64 = _mm_set_epi8(8,9,10,11,12,13,14,15,0,1,2,3,4,5,6,
	    7);
	__m128i BSWAP_MASK = _mm_set_epi8(0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,
	    15);
	__m128i X = _mm_setzero_si128();

	if (ibytes == 96/8) {
		Y = _mm_loadu_si128((const __m128i *)ivec);
		Y = _mm_insert_epi32(Y, 0x1000000, 3);
		/*(Compute E[ZERO, KS] and E[Y0, KS] together*/
		tmp1 = _mm_xor_si128(X, KEY[0]);
		tmp2 = _mm_xor_si128(Y, KEY[0]);
		for (j=1; j < nr-1; j+=2) {
			tmp1 = _mm_aesenc_si128(tmp1, KEY[j]);
			tmp2 = _mm_aesenc_si128(tmp2, KEY[j]);

			tmp1 = _mm_aesenc_si128(tmp1, KEY[j+1]);
			tmp2 = _mm_aesenc_si128(tmp2, KEY[j+1]);
		}
		tmp1 = _mm_aesenc_si128(tmp1, KEY[nr-1]);
		tmp2 = _mm_aesenc_si128(tmp2, KEY[nr-1]);

		H = _mm_aesenclast_si128(tmp1, KEY[nr]);
		T = _mm_aesenclast_si128(tmp2, KEY[nr]);

		H = _mm_shuffle_epi8(H, BSWAP_MASK);
	} else {
		tmp1 = _mm_xor_si128(X, KEY[0]);
		for (j=1; j <nr; j++)
			tmp1 = _mm_aesenc_si128(tmp1, KEY[j]);
		H = _mm_aesenclast_si128(tmp1, KEY[nr]);

		H = _mm_shuffle_epi8(H, BSWAP_MASK);
		Y = _mm_setzero_si128();

		for (i=0; i < ibytes/16; i++) {
			tmp1 = _mm_loadu_si128(&((const __m128i *)ivec)[i]);
			tmp1 = _mm_shuffle_epi8(tmp1, BSWAP_MASK);
			Y = _mm_xor_si128(Y, tmp1);
			gfmul(Y, H, &Y);
		}
		if (ibytes%16) {
			for (j=0; j < ibytes%16; j++)
				((unsigned char*)&last_block)[j] = ivec[i*16+j];
			tmp1 = last_block;
			tmp1 = _mm_shuffle_epi8(tmp1, BSWAP_MASK);
			Y = _mm_xor_si128(Y, tmp1);
			gfmul(Y, H, &Y);
		}
		tmp1 = _mm_insert_epi64(tmp1, (uint64_t)ibytes*8, 0);
		tmp1 = _mm_insert_epi64(tmp1, 0, 1);

		Y = _mm_xor_si128(Y, tmp1);
		gfmul(Y, H, &Y);
		Y = _mm_shuffle_epi8(Y, BSWAP_MASK); /*Compute E(K, Y0)*/
		tmp1 = _mm_xor_si128(Y, KEY[0]);
		for (j=1; j < nr; j++)
			tmp1 = _mm_aesenc_si128(tmp1, KEY[j]);
		T = _mm_aesenclast_si128(tmp1, KEY[nr]);
	}

	gfmul(H,H,&H2);
	gfmul(H,H2,&H3);
	gfmul(H,H3,&H4);

	for (i=0; i<abytes/16/4; i++) {
		tmp1 = _mm_loadu_si128(&((const __m128i *)addt)[i*4]);
		tmp2 = _mm_loadu_si128(&((const __m128i *)addt)[i*4+1]);
		tmp3 = _mm_loadu_si128(&((const __m128i *)addt)[i*4+2]);
		tmp4 = _mm_loadu_si128(&((const __m128i *)addt)[i*4+3]);

		tmp1 = _mm_shuffle_epi8(tmp1, BSWAP_MASK);
		tmp2 = _mm_shuffle_epi8(tmp2, BSWAP_MASK);
		tmp3 = _mm_shuffle_epi8(tmp3, BSWAP_MASK);
		tmp4 = _mm_shuffle_epi8(tmp4, BSWAP_MASK);
		tmp1 = _mm_xor_si128(X, tmp1);

		reduce4(H, H2, H3, H4, tmp4, tmp3, tmp2, tmp1, &X);
	}
	for (i=i*4; i<abytes/16; i++) {
		tmp1 = _mm_loadu_si128(&((const __m128i *)addt)[i]);
		tmp1 = _mm_shuffle_epi8(tmp1, BSWAP_MASK);
		X = _mm_xor_si128(X,tmp1);
		gfmul(X, H, &X);
	}
	if (abytes%16) {
		last_block = _mm_setzero_si128();
		for (j=0; j<abytes%16; j++)
			((unsigned char*)&last_block)[j] = addt[i*16+j];
		tmp1 = last_block;
		tmp1 = _mm_shuffle_epi8(tmp1, BSWAP_MASK);
		X =_mm_xor_si128(X,tmp1);
		gfmul(X,H,&X);
	}

	ctr1 = _mm_shuffle_epi8(Y, BSWAP_EPI64);
	ctr1 = _mm_add_epi64(ctr1, ONE);
	ctr2 = _mm_add_epi64(ctr1, ONE);
	ctr3 = _mm_add_epi64(ctr2, ONE);
	ctr4 = _mm_add_epi64(ctr3, ONE);
	ctr5 = _mm_add_epi64(ctr4, ONE);
	ctr6 = _mm_add_epi64(ctr5, ONE);
	ctr7 = _mm_add_epi64(ctr6, ONE);
	ctr8 = _mm_add_epi64(ctr7, ONE);

	for (i=0; i<nbytes/16/8; i++) {
		tmp1 = _mm_shuffle_epi8(ctr1, BSWAP_EPI64);
		tmp2 = _mm_shuffle_epi8(ctr2, BSWAP_EPI64);
		tmp3 = _mm_shuffle_epi8(ctr3, BSWAP_EPI64);
		tmp4 = _mm_shuffle_epi8(ctr4, BSWAP_EPI64);
		tmp5 = _mm_shuffle_epi8(ctr5, BSWAP_EPI64);
		tmp6 = _mm_shuffle_epi8(ctr6, BSWAP_EPI64);
		tmp7 = _mm_shuffle_epi8(ctr7, BSWAP_EPI64);
		tmp8 = _mm_shuffle_epi8(ctr8, BSWAP_EPI64);

		ctr1 = _mm_add_epi64(ctr1, EIGHT);
		ctr2 = _mm_add_epi64(ctr2, EIGHT);
		ctr3 = _mm_add_epi64(ctr3, EIGHT);
		ctr4 = _mm_add_epi64(ctr4, EIGHT);
		ctr5 = _mm_add_epi64(ctr5, EIGHT);
		ctr6 = _mm_add_epi64(ctr6, EIGHT);
		ctr7 = _mm_add_epi64(ctr7, EIGHT);
		ctr8 = _mm_add_epi64(ctr8, EIGHT);

		tmp1 =_mm_xor_si128(tmp1, KEY[0]);
		tmp2 =_mm_xor_si128(tmp2, KEY[0]);
		tmp3 =_mm_xor_si128(tmp3, KEY[0]);
		tmp4 =_mm_xor_si128(tmp4, KEY[0]);
		tmp5 =_mm_xor_si128(tmp5, KEY[0]);
		tmp6 =_mm_xor_si128(tmp6, KEY[0]);
		tmp7 =_mm_xor_si128(tmp7, KEY[0]);
		tmp8 =_mm_xor_si128(tmp8, KEY[0]);

		for (j=1; j<nr; j++) {
			tmp1 = _mm_aesenc_si128(tmp1, KEY[j]);
			tmp2 = _mm_aesenc_si128(tmp2, KEY[j]);
			tmp3 = _mm_aesenc_si128(tmp3, KEY[j]);
			tmp4 = _mm_aesenc_si128(tmp4, KEY[j]);
			tmp5 = _mm_aesenc_si128(tmp5, KEY[j]);
			tmp6 = _mm_aesenc_si128(tmp6, KEY[j]);
			tmp7 = _mm_aesenc_si128(tmp7, KEY[j]);
			tmp8 = _mm_aesenc_si128(tmp8, KEY[j]);
		}
		tmp1 =_mm_aesenclast_si128(tmp1, KEY[nr]);
		tmp2 =_mm_aesenclast_si128(tmp2, KEY[nr]);
		tmp3 =_mm_aesenclast_si128(tmp3, KEY[nr]);
		tmp4 =_mm_aesenclast_si128(tmp4, KEY[nr]);
		tmp5 =_mm_aesenclast_si128(tmp5, KEY[nr]);
		tmp6 =_mm_aesenclast_si128(tmp6, KEY[nr]);
		tmp7 =_mm_aesenclast_si128(tmp7, KEY[nr]);
		tmp8 =_mm_aesenclast_si128(tmp8, KEY[nr]);

		tmp1 = _mm_xor_si128(tmp1,
		    _mm_loadu_si128(&((const __m128i *)in)[i*8+0]));
		tmp2 = _mm_xor_si128(tmp2,
		    _mm_loadu_si128(&((const __m128i *)in)[i*8+1]));
		tmp3 = _mm_xor_si128(tmp3,
		    _mm_loadu_si128(&((const __m128i *)in)[i*8+2]));
		tmp4 = _mm_xor_si128(tmp4,
		    _mm_loadu_si128(&((const __m128i *)in)[i*8+3]));
		tmp5 = _mm_xor_si128(tmp5,
		    _mm_loadu_si128(&((const __m128i *)in)[i*8+4]));
		tmp6 = _mm_xor_si128(tmp6,
		    _mm_loadu_si128(&((const __m128i *)in)[i*8+5]));
		tmp7 = _mm_xor_si128(tmp7,
		    _mm_loadu_si128(&((const __m128i *)in)[i*8+6]));
		tmp8 = _mm_xor_si128(tmp8,
		    _mm_loadu_si128(&((const __m128i *)in)[i*8+7]));

		_mm_storeu_si128(&((__m128i*)out)[i*8+0], tmp1);
		_mm_storeu_si128(&((__m128i*)out)[i*8+1], tmp2);
		_mm_storeu_si128(&((__m128i*)out)[i*8+2], tmp3);
		_mm_storeu_si128(&((__m128i*)out)[i*8+3], tmp4);
		_mm_storeu_si128(&((__m128i*)out)[i*8+4], tmp5);
		_mm_storeu_si128(&((__m128i*)out)[i*8+5], tmp6);
		_mm_storeu_si128(&((__m128i*)out)[i*8+6], tmp7);
		_mm_storeu_si128(&((__m128i*)out)[i*8+7], tmp8);

		tmp1 = _mm_shuffle_epi8(tmp1, BSWAP_MASK);
		tmp2 = _mm_shuffle_epi8(tmp2, BSWAP_MASK);
		tmp3 = _mm_shuffle_epi8(tmp3, BSWAP_MASK);
		tmp4 = _mm_shuffle_epi8(tmp4, BSWAP_MASK);
		tmp5 = _mm_shuffle_epi8(tmp5, BSWAP_MASK);
		tmp6 = _mm_shuffle_epi8(tmp6, BSWAP_MASK);
		tmp7 = _mm_shuffle_epi8(tmp7, BSWAP_MASK);
		tmp8 = _mm_shuffle_epi8(tmp8, BSWAP_MASK);

		tmp1 = _mm_xor_si128(X, tmp1);

		reduce4(H, H2, H3, H4, tmp4, tmp3, tmp2, tmp1, &X);

		tmp5 = _mm_xor_si128(X, tmp5);
		reduce4(H, H2, H3, H4, tmp8, tmp7, tmp6, tmp5, &X);
	}
	for (k=i*8; k<nbytes/16; k++) {
		tmp1 = _mm_shuffle_epi8(ctr1, BSWAP_EPI64);
		ctr1 = _mm_add_epi64(ctr1, ONE);
		tmp1 = _mm_xor_si128(tmp1, KEY[0]);
		for (j=1; j<nr-1; j+=2) {
			tmp1 = _mm_aesenc_si128(tmp1, KEY[j]);
			tmp1 = _mm_aesenc_si128(tmp1, KEY[j+1]);
		}
		tmp1 = _mm_aesenc_si128(tmp1, KEY[nr-1]);
		tmp1 = _mm_aesenclast_si128(tmp1, KEY[nr]);
		tmp1 = _mm_xor_si128(tmp1,
		    _mm_loadu_si128(&((const __m128i *)in)[k]));
		_mm_storeu_si128(&((__m128i*)out)[k], tmp1);
		tmp1 = _mm_shuffle_epi8(tmp1, BSWAP_MASK);
		X = _mm_xor_si128(X, tmp1);
		gfmul(X,H,&X);
	}
	//If remains one incomplete block
	if (nbytes%16) {
		tmp1 = _mm_shuffle_epi8(ctr1, BSWAP_EPI64);
		tmp1 = _mm_xor_si128(tmp1, KEY[0]);
		for (j=1; j<nr-1; j+=2) {
			tmp1 = _mm_aesenc_si128(tmp1, KEY[j]);
			tmp1 = _mm_aesenc_si128(tmp1, KEY[j+1]);
		}
		tmp1 = _mm_aesenc_si128(tmp1, KEY[nr-1]);
		tmp1 = _mm_aesenclast_si128(tmp1, KEY[nr]);
		tmp1 = _mm_xor_si128(tmp1,
		    _mm_loadu_si128(&((const __m128i *)in)[k]));
		last_block = tmp1;
		for (j=0; j<nbytes%16; j++)
			out[k*16+j] = ((unsigned char*)&last_block)[j];
		for ((void)j; j<16; j++)
			((unsigned char*)&last_block)[j] = 0;
		tmp1 = last_block;
		tmp1 = _mm_shuffle_epi8(tmp1, BSWAP_MASK);
		X = _mm_xor_si128(X, tmp1);
		gfmul(X, H, &X);
	}
	tmp1 = _mm_insert_epi64(tmp1, (uint64_t)nbytes*8, 0);
	tmp1 = _mm_insert_epi64(tmp1, (uint64_t)abytes*8, 1);

	X = _mm_xor_si128(X, tmp1);
	gfmul(X,H,&X);
	X = _mm_shuffle_epi8(X, BSWAP_MASK);
	T = _mm_xor_si128(X, T);
	_mm_storeu_si128((__m128i*)tag, T);
}

/* My modification of _encrypt to be _decrypt */
int
AES_GCM_decrypt(const unsigned char *in, unsigned char *out,
	const unsigned char *addt, const unsigned char *ivec,
	const unsigned char *tag, uint32_t nbytes, uint32_t abytes, int ibytes,
	const unsigned char *key, int nr)
{
	int i, j ,k;
	__m128i tmp1, tmp2, tmp3, tmp4;
	__m128i tmp5, tmp6, tmp7, tmp8;
	__m128i H, H2, H3, H4, Y, T;
	const __m128i *KEY = (const __m128i *)key;
	__m128i ctr1, ctr2, ctr3, ctr4;
	__m128i ctr5, ctr6, ctr7, ctr8;
	__m128i last_block = _mm_setzero_si128();
	__m128i ONE = _mm_set_epi32(0, 1, 0, 0);
	__m128i EIGHT = _mm_set_epi32(0, 8, 0, 0);
	__m128i BSWAP_EPI64 = _mm_set_epi8(8,9,10,11,12,13,14,15,0,1,2,3,4,5,6,
	    7);
	__m128i BSWAP_MASK = _mm_set_epi8(0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,
	    15);
	__m128i X = _mm_setzero_si128();

	if (ibytes == 96/8) {
		Y = _mm_loadu_si128((const __m128i *)ivec);
		Y = _mm_insert_epi32(Y, 0x1000000, 3);
		/*(Compute E[ZERO, KS] and E[Y0, KS] together*/
		tmp1 = _mm_xor_si128(X, KEY[0]);
		tmp2 = _mm_xor_si128(Y, KEY[0]);
		for (j=1; j < nr-1; j+=2) {
			tmp1 = _mm_aesenc_si128(tmp1, KEY[j]);
			tmp2 = _mm_aesenc_si128(tmp2, KEY[j]);

			tmp1 = _mm_aesenc_si128(tmp1, KEY[j+1]);
			tmp2 = _mm_aesenc_si128(tmp2, KEY[j+1]);
		}
		tmp1 = _mm_aesenc_si128(tmp1, KEY[nr-1]);
		tmp2 = _mm_aesenc_si128(tmp2, KEY[nr-1]);

		H = _mm_aesenclast_si128(tmp1, KEY[nr]);
		T = _mm_aesenclast_si128(tmp2, KEY[nr]);

		H = _mm_shuffle_epi8(H, BSWAP_MASK);
	} else {
		tmp1 = _mm_xor_si128(X, KEY[0]);
		for (j=1; j <nr; j++)
			tmp1 = _mm_aesenc_si128(tmp1, KEY[j]);
		H = _mm_aesenclast_si128(tmp1, KEY[nr]);

		H = _mm_shuffle_epi8(H, BSWAP_MASK);
		Y = _mm_setzero_si128();

		for (i=0; i < ibytes/16; i++) {
			tmp1 = _mm_loadu_si128(&((const __m128i *)ivec)[i]);
			tmp1 = _mm_shuffle_epi8(tmp1, BSWAP_MASK);
			Y = _mm_xor_si128(Y, tmp1);
			gfmul(Y, H, &Y);
		}
		if (ibytes%16) {
			for (j=0; j < ibytes%16; j++)
				((unsigned char*)&last_block)[j] = ivec[i*16+j];
			tmp1 = last_block;
			tmp1 = _mm_shuffle_epi8(tmp1, BSWAP_MASK);
			Y = _mm_xor_si128(Y, tmp1);
			gfmul(Y, H, &Y);
		}
		tmp1 = _mm_insert_epi64(tmp1, (uint64_t)ibytes*8, 0);
		tmp1 = _mm_insert_epi64(tmp1, 0, 1);

		Y = _mm_xor_si128(Y, tmp1);
		gfmul(Y, H, &Y);
		Y = _mm_shuffle_epi8(Y, BSWAP_MASK); /*Compute E(K, Y0)*/
		tmp1 = _mm_xor_si128(Y, KEY[0]);
		for (j=1; j < nr; j++)
			tmp1 = _mm_aesenc_si128(tmp1, KEY[j]);
		T = _mm_aesenclast_si128(tmp1, KEY[nr]);
	}

	gfmul(H,H,&H2);
	gfmul(H,H2,&H3);
	gfmul(H,H3,&H4);

	for (i=0; i<abytes/16/4; i++) {
		tmp1 = _mm_loadu_si128(&((const __m128i *)addt)[i*4]);
		tmp2 = _mm_loadu_si128(&((const __m128i *)addt)[i*4+1]);
		tmp3 = _mm_loadu_si128(&((const __m128i *)addt)[i*4+2]);
		tmp4 = _mm_loadu_si128(&((const __m128i *)addt)[i*4+3]);

		tmp1 = _mm_shuffle_epi8(tmp1, BSWAP_MASK);
		tmp2 = _mm_shuffle_epi8(tmp2, BSWAP_MASK);
		tmp3 = _mm_shuffle_epi8(tmp3, BSWAP_MASK);
		tmp4 = _mm_shuffle_epi8(tmp4, BSWAP_MASK);

		tmp1 = _mm_xor_si128(X, tmp1);

		reduce4(H, H2, H3, H4, tmp4, tmp3, tmp2, tmp1, &X);
	}
	for (i=i*4; i<abytes/16; i++) {
		tmp1 = _mm_loadu_si128(&((const __m128i *)addt)[i]);
		tmp1 = _mm_shuffle_epi8(tmp1, BSWAP_MASK);
		X = _mm_xor_si128(X,tmp1);
		gfmul(X, H, &X);
	}
	if (abytes%16) {
		last_block = _mm_setzero_si128();
		for (j=0; j<abytes%16; j++)
			((unsigned char*)&last_block)[j] = addt[i*16+j];
		tmp1 = last_block;
		tmp1 = _mm_shuffle_epi8(tmp1, BSWAP_MASK);
		X =_mm_xor_si128(X,tmp1);
		gfmul(X,H,&X);
	}

	/* This is where we validate the cipher text before decrypt */
	for (i = 0; i<nbytes/16/4; i++) {
		tmp1 = _mm_loadu_si128(&((const __m128i *)in)[i*4]);
		tmp2 = _mm_loadu_si128(&((const __m128i *)in)[i*4+1]);
		tmp3 = _mm_loadu_si128(&((const __m128i *)in)[i*4+2]);
		tmp4 = _mm_loadu_si128(&((const __m128i *)in)[i*4+3]);

		tmp1 = _mm_shuffle_epi8(tmp1, BSWAP_MASK);
		tmp2 = _mm_shuffle_epi8(tmp2, BSWAP_MASK);
		tmp3 = _mm_shuffle_epi8(tmp3, BSWAP_MASK);
		tmp4 = _mm_shuffle_epi8(tmp4, BSWAP_MASK);

		tmp1 = _mm_xor_si128(X, tmp1);

		reduce4(H, H2, H3, H4, tmp4, tmp3, tmp2, tmp1, &X);
	}
	for (i = i*4; i<nbytes/16; i++) {
		tmp1 = _mm_loadu_si128(&((const __m128i *)in)[i]);
		tmp1 = _mm_shuffle_epi8(tmp1, BSWAP_MASK);
		X = _mm_xor_si128(X, tmp1);
		gfmul(X,H,&X);
	}
	if (nbytes%16) {
		last_block = _mm_setzero_si128();
		for (j=0; j<nbytes%16; j++)
			((unsigned char*)&last_block)[j] = in[i*16+j];
		tmp1 = last_block;
		tmp1 = _mm_shuffle_epi8(tmp1, BSWAP_MASK);
		X = _mm_xor_si128(X, tmp1);
		gfmul(X, H, &X);
	}

	tmp1 = _mm_insert_epi64(tmp1, (uint64_t)nbytes*8, 0);
	tmp1 = _mm_insert_epi64(tmp1, (uint64_t)abytes*8, 1);

	X = _mm_xor_si128(X, tmp1);
	gfmul(X,H,&X);
	X = _mm_shuffle_epi8(X, BSWAP_MASK);
	T = _mm_xor_si128(X, T);

	if (!m128icmp(T, _mm_loadu_si128((const __m128i*)tag)))
		return 0; //in case the authentication failed

	ctr1 = _mm_shuffle_epi8(Y, BSWAP_EPI64);
	ctr1 = _mm_add_epi64(ctr1, ONE);
	ctr2 = _mm_add_epi64(ctr1, ONE);
	ctr3 = _mm_add_epi64(ctr2, ONE);
	ctr4 = _mm_add_epi64(ctr3, ONE);
	ctr5 = _mm_add_epi64(ctr4, ONE);
	ctr6 = _mm_add_epi64(ctr5, ONE);
	ctr7 = _mm_add_epi64(ctr6, ONE);
	ctr8 = _mm_add_epi64(ctr7, ONE);

	for (i=0; i<nbytes/16/8; i++) {
		tmp1 = _mm_shuffle_epi8(ctr1, BSWAP_EPI64);
		tmp2 = _mm_shuffle_epi8(ctr2, BSWAP_EPI64);
		tmp3 = _mm_shuffle_epi8(ctr3, BSWAP_EPI64);
		tmp4 = _mm_shuffle_epi8(ctr4, BSWAP_EPI64);
		tmp5 = _mm_shuffle_epi8(ctr5, BSWAP_EPI64);
		tmp6 = _mm_shuffle_epi8(ctr6, BSWAP_EPI64);
		tmp7 = _mm_shuffle_epi8(ctr7, BSWAP_EPI64);
		tmp8 = _mm_shuffle_epi8(ctr8, BSWAP_EPI64);

		ctr1 = _mm_add_epi64(ctr1, EIGHT);
		ctr2 = _mm_add_epi64(ctr2, EIGHT);
		ctr3 = _mm_add_epi64(ctr3, EIGHT);
		ctr4 = _mm_add_epi64(ctr4, EIGHT);
		ctr5 = _mm_add_epi64(ctr5, EIGHT);
		ctr6 = _mm_add_epi64(ctr6, EIGHT);
		ctr7 = _mm_add_epi64(ctr7, EIGHT);
		ctr8 = _mm_add_epi64(ctr8, EIGHT);

		tmp1 =_mm_xor_si128(tmp1, KEY[0]);
		tmp2 =_mm_xor_si128(tmp2, KEY[0]);
		tmp3 =_mm_xor_si128(tmp3, KEY[0]);
		tmp4 =_mm_xor_si128(tmp4, KEY[0]);
		tmp5 =_mm_xor_si128(tmp5, KEY[0]);
		tmp6 =_mm_xor_si128(tmp6, KEY[0]);
		tmp7 =_mm_xor_si128(tmp7, KEY[0]);
		tmp8 =_mm_xor_si128(tmp8, KEY[0]);

		for (j=1; j<nr; j++) {
			tmp1 = _mm_aesenc_si128(tmp1, KEY[j]);
			tmp2 = _mm_aesenc_si128(tmp2, KEY[j]);
			tmp3 = _mm_aesenc_si128(tmp3, KEY[j]);
			tmp4 = _mm_aesenc_si128(tmp4, KEY[j]);
			tmp5 = _mm_aesenc_si128(tmp5, KEY[j]);
			tmp6 = _mm_aesenc_si128(tmp6, KEY[j]);
			tmp7 = _mm_aesenc_si128(tmp7, KEY[j]);
			tmp8 = _mm_aesenc_si128(tmp8, KEY[j]);
		}
		tmp1 =_mm_aesenclast_si128(tmp1, KEY[nr]);
		tmp2 =_mm_aesenclast_si128(tmp2, KEY[nr]);
		tmp3 =_mm_aesenclast_si128(tmp3, KEY[nr]);
		tmp4 =_mm_aesenclast_si128(tmp4, KEY[nr]);
		tmp5 =_mm_aesenclast_si128(tmp5, KEY[nr]);
		tmp6 =_mm_aesenclast_si128(tmp6, KEY[nr]);
		tmp7 =_mm_aesenclast_si128(tmp7, KEY[nr]);
		tmp8 =_mm_aesenclast_si128(tmp8, KEY[nr]);

		tmp1 = _mm_xor_si128(tmp1,
		    _mm_loadu_si128(&((const __m128i *)in)[i*8+0]));
		tmp2 = _mm_xor_si128(tmp2,
		    _mm_loadu_si128(&((const __m128i *)in)[i*8+1]));
		tmp3 = _mm_xor_si128(tmp3,
		    _mm_loadu_si128(&((const __m128i *)in)[i*8+2]));
		tmp4 = _mm_xor_si128(tmp4,
		    _mm_loadu_si128(&((const __m128i *)in)[i*8+3]));
		tmp5 = _mm_xor_si128(tmp5,
		    _mm_loadu_si128(&((const __m128i *)in)[i*8+4]));
		tmp6 = _mm_xor_si128(tmp6,
		    _mm_loadu_si128(&((const __m128i *)in)[i*8+5]));
		tmp7 = _mm_xor_si128(tmp7,
		    _mm_loadu_si128(&((const __m128i *)in)[i*8+6]));
		tmp8 = _mm_xor_si128(tmp8,
		    _mm_loadu_si128(&((const __m128i *)in)[i*8+7]));

		_mm_storeu_si128(&((__m128i*)out)[i*8+0], tmp1);
		_mm_storeu_si128(&((__m128i*)out)[i*8+1], tmp2);
		_mm_storeu_si128(&((__m128i*)out)[i*8+2], tmp3);
		_mm_storeu_si128(&((__m128i*)out)[i*8+3], tmp4);
		_mm_storeu_si128(&((__m128i*)out)[i*8+4], tmp5);
		_mm_storeu_si128(&((__m128i*)out)[i*8+5], tmp6);
		_mm_storeu_si128(&((__m128i*)out)[i*8+6], tmp7);
		_mm_storeu_si128(&((__m128i*)out)[i*8+7], tmp8);

		tmp1 = _mm_shuffle_epi8(tmp1, BSWAP_MASK);
		tmp2 = _mm_shuffle_epi8(tmp2, BSWAP_MASK);
		tmp3 = _mm_shuffle_epi8(tmp3, BSWAP_MASK);
		tmp4 = _mm_shuffle_epi8(tmp4, BSWAP_MASK);
		tmp5 = _mm_shuffle_epi8(tmp5, BSWAP_MASK);
		tmp6 = _mm_shuffle_epi8(tmp6, BSWAP_MASK);
		tmp7 = _mm_shuffle_epi8(tmp7, BSWAP_MASK);
		tmp8 = _mm_shuffle_epi8(tmp8, BSWAP_MASK);
	}
	for (k=i*8; k<nbytes/16; k++) {
		tmp1 = _mm_shuffle_epi8(ctr1, BSWAP_EPI64);
		ctr1 = _mm_add_epi64(ctr1, ONE);
		tmp1 = _mm_xor_si128(tmp1, KEY[0]);
		for (j=1; j<nr-1; j+=2) {
			tmp1 = _mm_aesenc_si128(tmp1, KEY[j]);
			tmp1 = _mm_aesenc_si128(tmp1, KEY[j+1]);
		}
		tmp1 = _mm_aesenc_si128(tmp1, KEY[nr-1]);
		tmp1 = _mm_aesenclast_si128(tmp1, KEY[nr]);
		tmp1 = _mm_xor_si128(tmp1,
		    _mm_loadu_si128(&((const __m128i *)in)[k]));
		_mm_storeu_si128(&((__m128i*)out)[k], tmp1);
	}
	//If remains one incomplete block
	if (nbytes%16) {
		tmp1 = _mm_shuffle_epi8(ctr1, BSWAP_EPI64);
		tmp1 = _mm_xor_si128(tmp1, KEY[0]);
		for (j=1; j<nr-1; j+=2) {
			tmp1 = _mm_aesenc_si128(tmp1, KEY[j]);
			tmp1 = _mm_aesenc_si128(tmp1, KEY[j+1]);
		}
		tmp1 = _mm_aesenc_si128(tmp1, KEY[nr-1]);
		tmp1 = _mm_aesenclast_si128(tmp1, KEY[nr]);
		tmp1 = _mm_xor_si128(tmp1,
		    _mm_loadu_si128(&((const __m128i *)in)[k]));
		last_block = tmp1;
		for (j=0; j<nbytes%16; j++)
			out[k*16+j] = ((unsigned char*)&last_block)[j];
	}
	return 1; //when sucessfull returns 1
}
