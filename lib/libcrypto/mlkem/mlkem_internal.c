/* $OpenBSD: mlkem_internal.c,v 1.3 2025/09/16 06:12:04 tb Exp $ */
/*
 * Copyright (c) 2024, Google Inc.
 * Copyright (c) 2024, 2025 Bob Beck <beck@obtuse.com>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <openssl/mlkem.h>

#include "bytestring.h"
#include "sha3_internal.h"
#include "mlkem_internal.h"
#include "constant_time.h"
#include "crypto_internal.h"

/*
 * See
 * https://csrc.nist.gov/pubs/fips/203/final
 */

static void
prf(uint8_t *out, size_t out_len, const uint8_t in[33])
{
	sha3_ctx ctx;
	shake256_init(&ctx);
	shake_update(&ctx, in, 33);
	shake_xof(&ctx);
	shake_out(&ctx, out, out_len);
}

/* Section 4.1 */
static void
hash_h(uint8_t out[32], const uint8_t *in, size_t len)
{
	sha3_ctx ctx;
	sha3_init(&ctx, 32);
	sha3_update(&ctx, in, len);
	sha3_final(out, &ctx);
}

static void
hash_g(uint8_t out[64], const uint8_t *in, size_t len)
{
	sha3_ctx ctx;
	sha3_init(&ctx, 64);
	sha3_update(&ctx, in, len);
	sha3_final(out, &ctx);
}

/* this is called 'J' in the spec */
static void
kdf(uint8_t out[MLKEM_SHARED_SECRET_LENGTH], const uint8_t failure_secret[32],
    const uint8_t *in, size_t len)
{
	sha3_ctx ctx;
	shake256_init(&ctx);
	shake_update(&ctx, failure_secret, 32);
	shake_update(&ctx, in, len);
	shake_xof(&ctx);
	shake_out(&ctx, out, MLKEM_SHARED_SECRET_LENGTH);
}

#define DEGREE 256

static const size_t kBarrettMultiplier = 5039;
static const unsigned kBarrettShift = 24;
static const uint16_t kPrime = 3329;
static const int kLog2Prime = 12;
static const uint16_t kHalfPrime = (/*kPrime=*/3329 - 1) / 2;
static const int kDU768 = 10;
static const int kDV768 = 4;
static const int kDU1024 = 11;
static const int kDV1024 = 5;

/*
 * kInverseDegree is 128^-1 mod 3329; 128 because kPrime does not have a 512th
 * root of unity.
 */
static const uint16_t kInverseDegree = 3303;

static inline size_t
encoded_vector_size(uint16_t rank)
{
	return (kLog2Prime * DEGREE / 8) * rank;
}

static inline size_t
compressed_vector_size(uint16_t rank)
{
	return ((rank == RANK768) ? kDU768 : kDU1024) * rank * DEGREE / 8;
}

typedef struct scalar {
	/* On every function entry and exit, 0 <= c < kPrime. */
	uint16_t c[DEGREE];
} scalar;

/*
 * Retrieve a const scalar from const matrix of |rank| at position [row][col]
 */
static inline const scalar *
const_m2s(const scalar *v, size_t row, size_t col, uint16_t rank)
{
	return ((scalar *)v) + row * rank + col;
}

/*
 * Retrieve a scalar from matrix of |rank| at position [row][col]
 */
static inline scalar *
m2s(scalar *v, size_t row, size_t col, uint16_t rank)
{
	return ((scalar *)v) + row * rank + col;
}

/*
 * This bit of Python will be referenced in some of the following comments:
 *
 *  p = 3329
 *
 * def bitreverse(i):
 *     ret = 0
 *     for n in range(7):
 *         bit = i & 1
 *         ret <<= 1
 *         ret |= bit
 *         i >>= 1
 *     return ret
 */

/* kNTTRoots = [pow(17, bitreverse(i), p) for i in range(128)] */
static const uint16_t kNTTRoots[128] = {
	1,    1729, 2580, 3289, 2642, 630,  1897, 848,  1062, 1919, 193,  797,
	2786, 3260, 569,  1746, 296,  2447, 1339, 1476, 3046, 56,   2240, 1333,
	1426, 2094, 535,  2882, 2393, 2879, 1974, 821,  289,  331,  3253, 1756,
	1197, 2304, 2277, 2055, 650,  1977, 2513, 632,  2865, 33,   1320, 1915,
	2319, 1435, 807,  452,  1438, 2868, 1534, 2402, 2647, 2617, 1481, 648,
	2474, 3110, 1227, 910,  17,   2761, 583,  2649, 1637, 723,  2288, 1100,
	1409, 2662, 3281, 233,  756,  2156, 3015, 3050, 1703, 1651, 2789, 1789,
	1847, 952,  1461, 2687, 939,  2308, 2437, 2388, 733,  2337, 268,  641,
	1584, 2298, 2037, 3220, 375,  2549, 2090, 1645, 1063, 319,  2773, 757,
	2099, 561,  2466, 2594, 2804, 1092, 403,  1026, 1143, 2150, 2775, 886,
	1722, 1212, 1874, 1029, 2110, 2935, 885,  2154,
};

/* kInverseNTTRoots = [pow(17, -bitreverse(i), p) for i in range(128)] */
static const uint16_t kInverseNTTRoots[128] = {
	1,    1600, 40,   749,  2481, 1432, 2699, 687,  1583, 2760, 69,   543,
	2532, 3136, 1410, 2267, 2508, 1355, 450,  936,  447,  2794, 1235, 1903,
	1996, 1089, 3273, 283,  1853, 1990, 882,  3033, 2419, 2102, 219,  855,
	2681, 1848, 712,  682,  927,  1795, 461,  1891, 2877, 2522, 1894, 1010,
	1414, 2009, 3296, 464,  2697, 816,  1352, 2679, 1274, 1052, 1025, 2132,
	1573, 76,   2998, 3040, 1175, 2444, 394,  1219, 2300, 1455, 2117, 1607,
	2443, 554,  1179, 2186, 2303, 2926, 2237, 525,  735,  863,  2768, 1230,
	2572, 556,  3010, 2266, 1684, 1239, 780,  2954, 109,  1292, 1031, 1745,
	2688, 3061, 992,  2596, 941,  892,  1021, 2390, 642,  1868, 2377, 1482,
	1540, 540,  1678, 1626, 279,  314,  1173, 2573, 3096, 48,   667,  1920,
	2229, 1041, 2606, 1692, 680,  2746, 568,  3312,
};

/* kModRoots = [pow(17, 2*bitreverse(i) + 1, p) for i in range(128)] */
static const uint16_t kModRoots[128] = {
	17,   3312, 2761, 568,  583,  2746, 2649, 680,  1637, 1692, 723,  2606,
	2288, 1041, 1100, 2229, 1409, 1920, 2662, 667,  3281, 48,   233,  3096,
	756,  2573, 2156, 1173, 3015, 314,  3050, 279,  1703, 1626, 1651, 1678,
	2789, 540,  1789, 1540, 1847, 1482, 952,  2377, 1461, 1868, 2687, 642,
	939,  2390, 2308, 1021, 2437, 892,  2388, 941,  733,  2596, 2337, 992,
	268,  3061, 641,  2688, 1584, 1745, 2298, 1031, 2037, 1292, 3220, 109,
	375,  2954, 2549, 780,  2090, 1239, 1645, 1684, 1063, 2266, 319,  3010,
	2773, 556,  757,  2572, 2099, 1230, 561,  2768, 2466, 863,  2594, 735,
	2804, 525,  1092, 2237, 403,  2926, 1026, 2303, 1143, 2186, 2150, 1179,
	2775, 554,  886,  2443, 1722, 1607, 1212, 2117, 1874, 1455, 1029, 2300,
	2110, 1219, 2935, 394,  885,  2444, 2154, 1175,
};

/* reduce_once reduces 0 <= x < 2*kPrime, mod kPrime. */
static uint16_t
reduce_once(uint16_t x)
{
	assert(x < 2 * kPrime);
	const uint16_t subtracted = x - kPrime;
	uint16_t mask = 0u - (subtracted >> 15);

	/*
	 * Although this is a constant-time select, we omit a value barrier here.
	 * Value barriers impede auto-vectorization (likely because it forces the
	 * value to transit through a general-purpose register). On AArch64, this
	 * is a difference of 2x.
	 *
	 * We usually add value barriers to selects because Clang turns
	 * consecutive selects with the same condition into a branch instead of
	 * CMOV/CSEL. This condition does not occur in ML-KEM, so omitting it
	 * seems to be safe so far  but see
	 * |scalar_centered_binomial_distribution_eta_2_with_prf|.
	 */
	return (mask & x) | (~mask & subtracted);
}

/*
 * constant time reduce x mod kPrime using Barrett reduction. x must be less
 * than kPrime + 2×kPrime².
 */
static uint16_t
reduce(uint32_t x)
{
	uint64_t product = (uint64_t)x * kBarrettMultiplier;
	uint32_t quotient = (uint32_t)(product >> kBarrettShift);
	uint32_t remainder = x - quotient * kPrime;

	assert(x < kPrime + 2u * kPrime * kPrime);
	return reduce_once(remainder);
}

static void
scalar_zero(scalar *out)
{
	memset(out, 0, sizeof(*out));
}

static void
vector_zero(scalar *out, size_t rank)
{
	memset(out, 0, sizeof(*out) * rank);
}

/*
 * In place number theoretic transform of a given scalar.
 * Note that MLKEM's kPrime 3329 does not have a 512th root of unity, so this
 * transform leaves off the last iteration of the usual FFT code, with the 128
 * relevant roots of unity being stored in |kNTTRoots|. This means the output
 * should be seen as 128 elements in GF(3329^2), with the coefficients of the
 * elements being consecutive entries in |s->c|.
 */
static void
scalar_ntt(scalar *s)
{
	int offset = DEGREE;
	int step;
	/*
	 * `int` is used here because using `size_t` throughout caused a ~5% slowdown
	 * with Clang 14 on Aarch64.
	 */
	for (step = 1; step < DEGREE / 2; step <<= 1) {
		int i, j, k = 0;

		offset >>= 1;
		for (i = 0; i < step; i++) {
			const uint32_t step_root = kNTTRoots[i + step];

			for (j = k; j < k + offset; j++) {
				uint16_t odd, even;

				odd = reduce(step_root * s->c[j + offset]);
				even = s->c[j];
				s->c[j] = reduce_once(odd + even);
				s->c[j + offset] = reduce_once(even - odd +
				    kPrime);
			}
			k += 2 * offset;
		}
	}
}

static void
vector_ntt(scalar *v, size_t rank)
{
	size_t i;

	for (i = 0; i < rank; i++) {
		scalar_ntt(&v[i]);
	}
}

/*
 * In place inverse number theoretic transform of a given scalar, with pairs of
 * entries of s->v being interpreted as elements of GF(3329^2). Just as with the
 * number theoretic transform, this leaves off the first step of the normal iFFT
 * to account for the fact that 3329 does not have a 512th root of unity, using
 * the precomputed 128 roots of unity stored in |kInverseNTTRoots|.
 */
static void
scalar_inverse_ntt(scalar *s)
{
	int i, j, k, offset, step = DEGREE / 2;

	/*
	 * `int` is used here because using `size_t` throughout caused a ~5% slowdown
	 * with Clang 14 on Aarch64.
	 */
	for (offset = 2; offset < DEGREE; offset <<= 1) {
		step >>= 1;
		k = 0;
		for (i = 0; i < step; i++) {
			uint32_t step_root = kInverseNTTRoots[i + step];
			for (j = k; j < k + offset; j++) {
				uint16_t odd, even;
				odd = s->c[j + offset];
				even = s->c[j];
				s->c[j] = reduce_once(odd + even);
				s->c[j + offset] = reduce(step_root *
				    (even - odd + kPrime));
			}
			k += 2 * offset;
		}
	}
	for (i = 0; i < DEGREE; i++) {
		s->c[i] = reduce(s->c[i] * kInverseDegree);
	}
}

static void
vector_inverse_ntt(scalar *v, size_t rank)
{
	size_t i;

	for (i = 0; i < rank; i++) {
		scalar_inverse_ntt(&v[i]);
	}
}

static void
scalar_add(scalar *lhs, const scalar *rhs)
{
	int i;

	for (i = 0; i < DEGREE; i++) {
		lhs->c[i] = reduce_once(lhs->c[i] + rhs->c[i]);
	}
}

static void
scalar_sub(scalar *lhs, const scalar *rhs)
{
	int i;

	for (i = 0; i < DEGREE; i++) {
		lhs->c[i] = reduce_once(lhs->c[i] - rhs->c[i] + kPrime);
	}
}

/*
 * Multiplying two scalars in the number theoretically transformed state.
 * Since 3329 does not have a 512th root of unity, this means we have to
 * interpret the 2*ith and (2*i+1)th entries of the scalar as elements of
 * GF(3329)[X]/(X^2 - 17^(2*bitreverse(i)+1)).
 * The value of 17^(2*bitreverse(i)+1) mod 3329 is stored in the precomputed
 * |kModRoots| table. Our Barrett transform only allows us to multiply two
 * reduced numbers together, so we need some intermediate reduction steps,
 * even if an uint64_t could hold 3 multiplied numbers.
 */
static void
scalar_mult(scalar *out, const scalar *lhs, const scalar *rhs)
{
	int i;

	for (i = 0; i < DEGREE / 2; i++) {
		uint32_t real_real = (uint32_t)lhs->c[2 * i] * rhs->c[2 * i];
		uint32_t img_img = (uint32_t)lhs->c[2 * i + 1] *
		    rhs->c[2 * i + 1];
		uint32_t real_img = (uint32_t)lhs->c[2 * i] * rhs->c[2 * i + 1];
		uint32_t img_real = (uint32_t)lhs->c[2 * i + 1] * rhs->c[2 * i];

		out->c[2 * i] =
		    reduce(real_real +
		    (uint32_t)reduce(img_img) * kModRoots[i]);
		out->c[2 * i + 1] = reduce(img_real + real_img);
	}
}

static void
vector_add(scalar *lhs, const scalar *rhs, size_t rank)
{
	size_t i;

	for (i = 0; i < rank; i++) {
		scalar_add(&lhs[i], &rhs[i]);
	}
}

static void
matrix_mult(scalar *out, const void *m, const scalar *a, size_t rank)
{
	size_t i, j;

	vector_zero(&out[0], rank);
	for (i = 0; i < rank; i++) {
		for (j = 0; j < rank; j++) {
			scalar product;

			scalar_mult(&product, const_m2s(m, i, j, rank), &a[j]);
			scalar_add(&out[i], &product);
		}
	}
}

static void
matrix_mult_transpose(scalar *out, const void *m, const scalar *a, size_t rank)
{
	int i, j;

	vector_zero(&out[0], rank);
	for (i = 0; i < rank; i++) {
		for (j = 0; j < rank; j++) {
			scalar product;

			scalar_mult(&product, const_m2s(m, j, i, rank), &a[j]);
			scalar_add(&out[i], &product);
		}
	}
}

static void
scalar_inner_product(scalar *out, const scalar *lhs,
    const scalar *rhs, size_t rank)
{
	size_t i;

	scalar_zero(out);
	for (i = 0; i < rank; i++) {
		scalar product;

		scalar_mult(&product, &lhs[i], &rhs[i]);
		scalar_add(out, &product);
	}
}

/*
 * Algorithm 6 of spec. Rejection samples a Keccak stream to get uniformly
 * distributed elements. This is used for matrix expansion and only operates on
 * public inputs.
 */
static void
scalar_from_keccak_vartime(scalar *out, sha3_ctx *keccak_ctx)
{
	int i, done = 0;

	while (done < DEGREE) {
		uint8_t block[168];

		shake_out(keccak_ctx, block, sizeof(block));
		for (i = 0; i < sizeof(block) && done < DEGREE; i += 3) {
			uint16_t d1 = block[i] + 256 * (block[i + 1] % 16);
			uint16_t d2 = block[i + 1] / 16 + 16 * block[i + 2];

			if (d1 < kPrime) {
				out->c[done++] = d1;
			}
			if (d2 < kPrime && done < DEGREE) {
				out->c[done++] = d2;
			}
		}
	}
}

/*
 * Algorithm 7 of the spec, with eta fixed to two and the PRF call
 * included. Creates binominally distributed elements by sampling 2*|eta| bits,
 * and setting the coefficient to the count of the first bits minus the count of
 * the second bits, resulting in a centered binomial distribution. Since eta is
 * two this gives -2/2 with a probability of 1/16, -1/1 with probability 1/4,
 * and 0 with probability 3/8.
 */
static void
scalar_centered_binomial_distribution_eta_2_with_prf(scalar *out,
    const uint8_t input[33])
{
	uint8_t entropy[128];
	int i;

	CTASSERT(sizeof(entropy) == 2 * /*kEta=*/ 2 * DEGREE / 8);
	prf(entropy, sizeof(entropy), input);

	for (i = 0; i < DEGREE; i += 2) {
		uint8_t byte = entropy[i / 2];
		uint16_t mask;
		uint16_t value = (byte & 1) + ((byte >> 1) & 1);

		value -= ((byte >> 2) & 1) + ((byte >> 3) & 1);

		/*
		 * Add |kPrime| if |value| underflowed. See |reduce_once| for a
		 * discussion on why the value barrier is omitted. While this
		 * could have been written reduce_once(value + kPrime), this is
		 * one extra addition and small range of |value| tempts some
		 * versions of Clang to emit a branch.
		 */
		mask = 0u - (value >> 15);
		out->c[i] = ((value + kPrime) & mask) | (value & ~mask);

		byte >>= 4;
		value = (byte & 1) + ((byte >> 1) & 1);
		value -= ((byte >> 2) & 1) + ((byte >> 3) & 1);
		/*  See above. */
		mask = 0u - (value >> 15);
		out->c[i + 1] = ((value + kPrime) & mask) | (value & ~mask);
	}
}

/*
 * Generates a secret vector by using
 * |scalar_centered_binomial_distribution_eta_2_with_prf|, using the given seed
 * appending and incrementing |counter| for entry of the vector.
 */
static void
vector_generate_secret_eta_2(scalar *out, uint8_t *counter,
    const uint8_t seed[32], size_t rank)
{
	uint8_t input[33];
	size_t i;

	memcpy(input, seed, 32);
	for (i = 0; i < rank; i++) {
		input[32] = (*counter)++;
		scalar_centered_binomial_distribution_eta_2_with_prf(&out[i],
		    input);
	}
}

/* Expands the matrix of a seed for key generation and for encaps-CPA. */
static void
matrix_expand(void *out, const uint8_t rho[32], size_t rank)
{
	uint8_t input[34];
	size_t i, j;

	memcpy(input, rho, 32);
	for (i = 0; i < rank; i++) {
		for (j = 0; j < rank; j++) {
			sha3_ctx keccak_ctx;

			input[32] = i;
			input[33] = j;
			shake128_init(&keccak_ctx);
			shake_update(&keccak_ctx, input, sizeof(input));
			shake_xof(&keccak_ctx);
			scalar_from_keccak_vartime(m2s(out, i, j, rank),
			    &keccak_ctx);
		}
	}
}

static const uint8_t kMasks[8] = {0x01, 0x03, 0x07, 0x0f,
	0x1f, 0x3f, 0x7f, 0xff};

static void
scalar_encode(uint8_t *out, const scalar *s, int bits)
{
	uint8_t out_byte = 0;
	int i, out_byte_bits = 0;

	assert(bits <= (int)sizeof(*s->c) * 8 && bits != 1);
	for (i = 0; i < DEGREE; i++) {
		uint16_t element = s->c[i];
		int element_bits_done = 0;

		while (element_bits_done < bits) {
			int chunk_bits = bits - element_bits_done;
			int out_bits_remaining = 8 - out_byte_bits;

			if (chunk_bits >= out_bits_remaining) {
				chunk_bits = out_bits_remaining;
				out_byte |= (element &
				    kMasks[chunk_bits - 1]) << out_byte_bits;
				*out = out_byte;
				out++;
				out_byte_bits = 0;
				out_byte = 0;
			} else {
				out_byte |= (element &
				    kMasks[chunk_bits - 1]) << out_byte_bits;
				out_byte_bits += chunk_bits;
			}

			element_bits_done += chunk_bits;
			element >>= chunk_bits;
		}
	}

	if (out_byte_bits > 0) {
		*out = out_byte;
	}
}

/* scalar_encode_1 is |scalar_encode| specialised for |bits| == 1. */
static void
scalar_encode_1(uint8_t out[32], const scalar *s)
{
	int i, j;

	for (i = 0; i < DEGREE; i += 8) {
		uint8_t out_byte = 0;

		for (j = 0; j < 8; j++) {
			out_byte |= (s->c[i + j] & 1) << j;
		}
		*out = out_byte;
		out++;
	}
}

/*
 * Encodes an entire vector into 32*|RANK768|*|bits| bytes. Note that since 256
 * (DEGREE) is divisible by 8, the individual vector entries will always fill a
 * whole number of bytes, so we do not need to worry about bit packing here.
 */
static void
vector_encode(uint8_t *out, const scalar *a, int bits, size_t rank)
{
	int i;

	for (i = 0; i < rank; i++) {
		scalar_encode(out + i * bits * DEGREE / 8, &a[i], bits);
	}
}

/* Encodes an entire vector as above, but adding it to a CBB */
static int
vector_encode_cbb(CBB *cbb, const scalar *a, int bits, size_t rank)
{
	uint8_t *encoded_vector;

	if (!CBB_add_space(cbb, &encoded_vector, encoded_vector_size(rank)))
		return 0;
	vector_encode(encoded_vector, a, bits, rank);

	return 1;
}

/*
 * scalar_decode parses |DEGREE * bits| bits from |in| into |DEGREE| values in
 * |out|. It returns one on success and zero if any parsed value is >=
 * |kPrime|.
 */
static int
scalar_decode(scalar *out, const uint8_t *in, int bits)
{
	uint8_t in_byte = 0;
	int i, in_byte_bits_left = 0;

	assert(bits <= (int)sizeof(*out->c) * 8 && bits != 1);

	for (i = 0; i < DEGREE; i++) {
		uint16_t element = 0;
		int element_bits_done = 0;

		while (element_bits_done < bits) {
			int chunk_bits = bits - element_bits_done;

			if (in_byte_bits_left == 0) {
				in_byte = *in;
				in++;
				in_byte_bits_left = 8;
			}

			if (chunk_bits > in_byte_bits_left) {
				chunk_bits = in_byte_bits_left;
			}

			element |= (in_byte & kMasks[chunk_bits - 1]) <<
			    element_bits_done;
			in_byte_bits_left -= chunk_bits;
			in_byte >>= chunk_bits;

			element_bits_done += chunk_bits;
		}

		if (element >= kPrime) {
			return 0;
		}
		out->c[i] = element;
	}

	return 1;
}

/* scalar_decode_1 is |scalar_decode| specialised for |bits| == 1. */
static void
scalar_decode_1(scalar *out, const uint8_t in[32])
{
	int i, j;

	for (i = 0; i < DEGREE; i += 8) {
		uint8_t in_byte = *in;

		in++;
		for (j = 0; j < 8; j++) {
			out->c[i + j] = in_byte & 1;
			in_byte >>= 1;
		}
	}
}

/*
 * Decodes 32*|RANK768|*|bits| bytes from |in| into |out|. It returns one on
 * success or zero if any parsed value is >= |kPrime|.
 */
static int
vector_decode(scalar *out, const uint8_t *in, int bits, size_t rank)
{
	size_t i;

	for (i = 0; i < rank; i++) {
		if (!scalar_decode(&out[i], in + i * bits * DEGREE / 8,
		    bits)) {
			return 0;
		}
	}
	return 1;
}

/*
 * Compresses (lossily) an input |x| mod 3329 into |bits| many bits by grouping
 * numbers close to each other together. The formula used is
 * round(2^|bits|/kPrime*x) mod 2^|bits|.
 * Uses Barrett reduction to achieve constant time. Since we need both the
 * remainder (for rounding) and the quotient (as the result), we cannot use
 * |reduce| here, but need to do the Barrett reduction directly.
 */
static uint16_t
compress(uint16_t x, int bits)
{
	uint32_t shifted = (uint32_t)x << bits;
	uint64_t product = (uint64_t)shifted * kBarrettMultiplier;
	uint32_t quotient = (uint32_t)(product >> kBarrettShift);
	uint32_t remainder = shifted - quotient * kPrime;

	/*
	 * Adjust the quotient to round correctly:
	 * 0 <= remainder <= kHalfPrime round to 0
	 * kHalfPrime < remainder <= kPrime + kHalfPrime round to 1
	 * kPrime + kHalfPrime < remainder < 2 * kPrime round to 2
	 */
	assert(remainder < 2u * kPrime);
	quotient += 1 & constant_time_lt(kHalfPrime, remainder);
	quotient += 1 & constant_time_lt(kPrime + kHalfPrime, remainder);
	return quotient & ((1 << bits) - 1);
}

/*
 * Decompresses |x| by using an equi-distant representative. The formula is
 * round(kPrime/2^|bits|*x). Note that 2^|bits| being the divisor allows us to
 * implement this logic using only bit operations.
 */
static uint16_t
decompress(uint16_t x, int bits)
{
	uint32_t product = (uint32_t)x * kPrime;
	uint32_t power = 1 << bits;
	/* This is |product| % power, since |power| is a power of 2. */
	uint32_t remainder = product & (power - 1);
	/* This is |product| / power, since |power| is a power of 2. */
	uint32_t lower = product >> bits;

	/*
	 * The rounding logic works since the first half of numbers mod |power| have a
	 * 0 as first bit, and the second half has a 1 as first bit, since |power| is
	 * a power of 2. As a 12 bit number, |remainder| is always positive, so we
	 * will shift in 0s for a right shift.
	 */
	return lower + (remainder >> (bits - 1));
}

static void
scalar_compress(scalar *s, int bits)
{
	int i;

	for (i = 0; i < DEGREE; i++) {
		s->c[i] = compress(s->c[i], bits);
	}
}

static void
scalar_decompress(scalar *s, int bits)
{
	int i;

	for (i = 0; i < DEGREE; i++) {
		s->c[i] = decompress(s->c[i], bits);
	}
}

static void
vector_compress(scalar *v, int bits, size_t rank)
{
	size_t i;

	for (i = 0; i < rank; i++) {
		scalar_compress(&v[i], bits);
	}
}

static void
vector_decompress(scalar *v, int bits, size_t rank)
{
	int i;

	for (i = 0; i < rank; i++) {
		scalar_decompress(&v[i], bits);
	}
}

struct public_key {
	scalar *t;
	uint8_t *rho;
	uint8_t *public_key_hash;
	scalar *m;
};

static void
public_key_from_external(const MLKEM_public_key *external,
    struct public_key *pub)
{
	size_t vector_size = external->rank * sizeof(scalar);
	uint8_t *bytes = external->key_768->bytes;
	size_t offset = 0;

	if (external->rank == RANK1024)
		bytes = external->key_1024->bytes;

	pub->t = (struct scalar *)bytes + offset;
	offset += vector_size;
	pub->rho = bytes + offset;
	offset += 32;
	pub->public_key_hash = bytes + offset;
	offset += 32;
	pub->m = (void *)(bytes + offset);
	offset += vector_size * external->rank;
}

struct private_key {
	struct public_key pub;
	scalar *s;
	uint8_t *fo_failure_secret;
};

static void
private_key_from_external(const MLKEM_private_key *external,
    struct private_key *priv)
{
	size_t vector_size = external->rank * sizeof(scalar);
	size_t offset = 0;
	uint8_t *bytes = external->key_768->bytes;

	if (external->rank == RANK1024)
		bytes = external->key_1024->bytes;

	priv->pub.t = (struct scalar *)(bytes + offset);
	offset += vector_size;
	priv->pub.rho = bytes + offset;
	offset += 32;
	priv->pub.public_key_hash = bytes + offset;
	offset += 32;
	priv->pub.m = (void *)(bytes + offset);
	offset += vector_size * external->rank;
	priv->s = (void *)(bytes + offset);
	offset += vector_size;
	priv->fo_failure_secret = bytes + offset;
	offset += 32;
}

/*
 * Calls |mlkem_generate_key_external_entropy| with random bytes from
 * |RAND_bytes|.
 */
int
mlkem_generate_key(uint8_t *out_encoded_public_key,
    uint8_t optional_out_seed[MLKEM_SEED_LENGTH],
    MLKEM_private_key *out_private_key)
{
	uint8_t entropy_buf[MLKEM_SEED_LENGTH];
	uint8_t *entropy = optional_out_seed != NULL ? optional_out_seed :
	    entropy_buf;

	arc4random_buf(entropy, MLKEM_SEED_LENGTH);
	return mlkem_generate_key_external_entropy(out_encoded_public_key,
	    out_private_key, entropy);
}

int
mlkem_private_key_from_seed(const uint8_t *seed, size_t seed_len,
    MLKEM_private_key *out_private_key)
{
	uint8_t *public_key_buf = NULL;
	size_t public_key_buf_len = out_private_key->rank == RANK768 ?
	    MLKEM768_PUBLIC_KEY_BYTES : MLKEM1024_PUBLIC_KEY_BYTES;
	int ret = 0;

	if (seed_len != MLKEM_SEED_LENGTH) {
		goto err;
	}

	if ((public_key_buf = calloc(1, public_key_buf_len)) == NULL)
		goto err;

	ret = mlkem_generate_key_external_entropy(public_key_buf,
	    out_private_key, seed);

 err:
	freezero(public_key_buf, public_key_buf_len);

	return ret;
}

static int
mlkem_marshal_public_key_internal(CBB *out, const struct public_key *pub,
    size_t rank)
{
	if (!vector_encode_cbb(out, &pub->t[0], kLog2Prime, rank))
		return 0;
	return CBB_add_bytes(out, pub->rho, 32);
}

int
mlkem_generate_key_external_entropy(uint8_t *out_encoded_public_key,
    MLKEM_private_key *out_private_key,
    const uint8_t entropy[MLKEM_SEED_LENGTH])
{
	struct private_key priv;
	uint8_t augmented_seed[33];
	uint8_t *rho, *sigma;
	uint8_t counter = 0;
	uint8_t hashed[64];
	scalar error[RANK1024];
	CBB cbb;
	int ret = 0;

	private_key_from_external(out_private_key, &priv);
	memset(&cbb, 0, sizeof(cbb));
	memcpy(augmented_seed, entropy, 32);
	augmented_seed[32] = out_private_key->rank;
	hash_g(hashed, augmented_seed, 33);
	rho = hashed;
	sigma = hashed + 32;
	memcpy(priv.pub.rho, hashed, 32);
	matrix_expand(priv.pub.m, rho, out_private_key->rank);
	vector_generate_secret_eta_2(priv.s, &counter, sigma,
	    out_private_key->rank);
	vector_ntt(priv.s, out_private_key->rank);
	vector_generate_secret_eta_2(&error[0], &counter, sigma,
	    out_private_key->rank);
	vector_ntt(&error[0], out_private_key->rank);
	matrix_mult_transpose(priv.pub.t, priv.pub.m, priv.s,
	    out_private_key->rank);
	vector_add(priv.pub.t, &error[0], out_private_key->rank);

	if (!CBB_init_fixed(&cbb, out_encoded_public_key,
	    out_private_key->rank == RANK768 ? MLKEM768_PUBLIC_KEY_BYTES :
	    MLKEM1024_PUBLIC_KEY_BYTES))
		goto err;

	if (!mlkem_marshal_public_key_internal(&cbb, &priv.pub,
	    out_private_key->rank))
		goto err;

	hash_h(priv.pub.public_key_hash, out_encoded_public_key,
	    out_private_key->rank == RANK768 ? MLKEM768_PUBLIC_KEY_BYTES :
	    MLKEM1024_PUBLIC_KEY_BYTES);
	memcpy(priv.fo_failure_secret, entropy + 32, 32);

	ret = 1;

 err:
	CBB_cleanup(&cbb);

	return ret;
}

void
mlkem_public_from_private(const MLKEM_private_key *private_key,
    MLKEM_public_key *out_public_key)
{
	switch (private_key->rank) {
	case RANK768:
		memcpy(out_public_key->key_768->bytes,
		    private_key->key_768->bytes,
		    sizeof(out_public_key->key_768->bytes));
		break;
	case RANK1024:
		memcpy(out_public_key->key_1024->bytes,
		    private_key->key_1024->bytes,
		    sizeof(out_public_key->key_1024->bytes));
		break;
	}
}

/*
 * Encrypts a message with given randomness to the ciphertext in |out|. Without
 * applying the Fujisaki-Okamoto transform this would not result in a CCA secure
 * scheme, since lattice schemes are vulnerable to decryption failure oracles.
 */
static void
encrypt_cpa(uint8_t *out, const struct public_key *pub,
    const uint8_t message[32], const uint8_t randomness[32],
    size_t rank)
{
	scalar secret[RANK1024], error[RANK1024], u[RANK1024];
	scalar expanded_message, scalar_error;
	uint8_t counter = 0;
	uint8_t input[33];
	scalar v;
	int u_bits = kDU768;
	int v_bits = kDV768;

	if (rank == RANK1024) {
		u_bits = kDU1024;
		v_bits = kDV1024;
	}
	vector_generate_secret_eta_2(&secret[0], &counter, randomness, rank);
	vector_ntt(&secret[0], rank);
	vector_generate_secret_eta_2(&error[0], &counter, randomness, rank);
	memcpy(input, randomness, 32);
	input[32] = counter;
	scalar_centered_binomial_distribution_eta_2_with_prf(&scalar_error,
	    input);
	matrix_mult(&u[0], pub->m, &secret[0], rank);
	vector_inverse_ntt(&u[0], rank);
	vector_add(&u[0], &error[0], rank);
	scalar_inner_product(&v, &pub->t[0], &secret[0], rank);
	scalar_inverse_ntt(&v);
	scalar_add(&v, &scalar_error);
	scalar_decode_1(&expanded_message, message);
	scalar_decompress(&expanded_message, 1);
	scalar_add(&v, &expanded_message);
	vector_compress(&u[0], u_bits, rank);
	vector_encode(out, &u[0], u_bits, rank);
	scalar_compress(&v, v_bits);
	scalar_encode(out + compressed_vector_size(rank), &v, v_bits);
}

/* Calls mlkem_encap_external_entropy| with random bytes */
void
mlkem_encap(const MLKEM_public_key *public_key,
    uint8_t *out_ciphertext,
    uint8_t out_shared_secret[MLKEM_SHARED_SECRET_LENGTH])
{
	uint8_t entropy[MLKEM_ENCAP_ENTROPY];

	arc4random_buf(entropy, MLKEM_ENCAP_ENTROPY);
	mlkem_encap_external_entropy(out_ciphertext,
	    out_shared_secret, public_key, entropy);
}

/* See section 6.2 of the spec. */
void
mlkem_encap_external_entropy(uint8_t *out_ciphertext,
    uint8_t out_shared_secret[MLKEM_SHARED_SECRET_LENGTH],
    const MLKEM_public_key *public_key,
    const uint8_t entropy[MLKEM_ENCAP_ENTROPY])
{
	struct public_key pub;
	uint8_t key_and_randomness[64];
	uint8_t input[64];

	public_key_from_external(public_key, &pub);
	memcpy(input, entropy, MLKEM_ENCAP_ENTROPY);
	memcpy(input + MLKEM_ENCAP_ENTROPY, pub.public_key_hash,
	    sizeof(input) - MLKEM_ENCAP_ENTROPY);
	hash_g(key_and_randomness, input, sizeof(input));
	encrypt_cpa(out_ciphertext, &pub, entropy, key_and_randomness + 32,
	    public_key->rank);
	memcpy(out_shared_secret, key_and_randomness, 32);
}

static void
decrypt_cpa(uint8_t out[32], const struct private_key *priv,
    const uint8_t *ciphertext, size_t rank)
{
	scalar u[RANK1024];
	scalar mask, v;
	int u_bits = kDU768;
	int v_bits = kDV768;

	if (rank == RANK1024) {
		u_bits = kDU1024;
		v_bits = kDV1024;
	}
	vector_decode(&u[0], ciphertext, u_bits, rank);
	vector_decompress(&u[0], u_bits, rank);
	vector_ntt(&u[0], rank);
	scalar_decode(&v, ciphertext + compressed_vector_size(rank), v_bits);
	scalar_decompress(&v, v_bits);
	scalar_inner_product(&mask, &priv->s[0], &u[0], rank);
	scalar_inverse_ntt(&mask);
	scalar_sub(&v, &mask);
	scalar_compress(&v, 1);
	scalar_encode_1(out, &v);
}

/* See section 6.3 */
int
mlkem_decap(const MLKEM_private_key *private_key, const uint8_t *ciphertext,
    size_t ciphertext_len, uint8_t out_shared_secret[MLKEM_SHARED_SECRET_LENGTH])
{
	struct private_key priv;
	size_t expected_ciphertext_length = private_key->rank == RANK768 ?
	    MLKEM768_CIPHERTEXT_BYTES : MLKEM1024_CIPHERTEXT_BYTES;
	uint8_t *expected_ciphertext = NULL;
	uint8_t key_and_randomness[64];
	uint8_t failure_key[32];
	uint8_t decrypted[64];
	uint8_t mask;
	int i;
	int ret = 0;

	if (ciphertext_len != expected_ciphertext_length) {
		arc4random_buf(out_shared_secret, MLKEM_SHARED_SECRET_LENGTH);
		goto err;
	}

	if ((expected_ciphertext = calloc(1, expected_ciphertext_length)) ==
	    NULL) {
		arc4random_buf(out_shared_secret, MLKEM_SHARED_SECRET_LENGTH);
		goto err;
	}

	private_key_from_external(private_key, &priv);
	decrypt_cpa(decrypted, &priv, ciphertext, private_key->rank);
	memcpy(decrypted + 32, priv.pub.public_key_hash,
	    sizeof(decrypted) - 32);
	hash_g(key_and_randomness, decrypted, sizeof(decrypted));
	encrypt_cpa(expected_ciphertext, &priv.pub, decrypted,
	    key_and_randomness + 32, private_key->rank);
	kdf(failure_key, priv.fo_failure_secret, ciphertext, ciphertext_len);
	mask = constant_time_eq_int_8(memcmp(ciphertext, expected_ciphertext,
	    expected_ciphertext_length), 0);
	for (i = 0; i < MLKEM_SHARED_SECRET_LENGTH; i++) {
		out_shared_secret[i] = constant_time_select_8(mask,
		    key_and_randomness[i], failure_key[i]);
	}

	ret = 1;

 err:
	freezero(expected_ciphertext, expected_ciphertext_length);

	return ret;
}

int
mlkem_marshal_public_key(const MLKEM_public_key *public_key,
    uint8_t **output, size_t *output_len)
{
	struct public_key pub;
	int ret = 0;
	CBB cbb;

	if (!CBB_init(&cbb, public_key->rank == RANK768 ?
	    MLKEM768_PUBLIC_KEY_BYTES : MLKEM1024_PUBLIC_KEY_BYTES))
		goto err;
	public_key_from_external(public_key, &pub);
	if (!mlkem_marshal_public_key_internal(&cbb, &pub, public_key->rank))
		goto err;
	if (!CBB_finish(&cbb, output, output_len))
		goto err;

	ret = 1;

 err:
	CBB_cleanup(&cbb);

	return ret;
}

/*
 * mlkem_parse_public_key_no_hash parses |in| into |pub| but doesn't calculate
 * the value of |pub->public_key_hash|.
 */
static int
mlkem_parse_public_key_no_hash(struct public_key *pub, CBS *in, size_t rank)
{
	CBS t_bytes;

	if (!CBS_get_bytes(in, &t_bytes, encoded_vector_size(rank)))
		return 0;
	if (!vector_decode(&pub->t[0], CBS_data(&t_bytes), kLog2Prime, rank))
		return 0;

	memcpy(pub->rho, CBS_data(in), 32);
	if (!CBS_skip(in, 32))
		return 0;
	matrix_expand(pub->m, pub->rho, rank);
	return 1;
}

int
mlkem_parse_public_key(const uint8_t *input, size_t input_len,
    MLKEM_public_key *public_key)
{
	struct public_key pub;
	CBS cbs;

	public_key_from_external(public_key, &pub);
	CBS_init(&cbs, input, input_len);
	if (!mlkem_parse_public_key_no_hash(&pub, &cbs, public_key->rank))
		return 0;
	if (CBS_len(&cbs) != 0)
		return 0;

	hash_h(pub.public_key_hash, input, input_len);

	return 1;
}

int
mlkem_marshal_private_key(const MLKEM_private_key *private_key,
    uint8_t **out_private_key, size_t *out_private_key_len)
{
	struct private_key priv;
	size_t key_length = private_key->rank == RANK768 ?
	    MLKEM768_PRIVATE_KEY_BYTES : MLKEM1024_PRIVATE_KEY_BYTES;
	CBB cbb;
	int ret = 0;

	private_key_from_external(private_key, &priv);
	if (!CBB_init(&cbb, key_length))
		goto err;

	if (!vector_encode_cbb(&cbb, priv.s, kLog2Prime, private_key->rank))
		goto err;
	if (!mlkem_marshal_public_key_internal(&cbb, &priv.pub,
	    private_key->rank))
		goto err;
	if (!CBB_add_bytes(&cbb, priv.pub.public_key_hash, 32))
		goto err;
	if (!CBB_add_bytes(&cbb, priv.fo_failure_secret, 32))
		goto err;

	if (!CBB_finish(&cbb, out_private_key, out_private_key_len))
		goto err;

	ret = 1;

 err:
	CBB_cleanup(&cbb);

	return ret;
}

int
mlkem_parse_private_key(const uint8_t *input, size_t input_len,
    MLKEM_private_key *out_private_key)
{
	struct private_key priv;
	CBS cbs, s_bytes;

	private_key_from_external(out_private_key, &priv);
	CBS_init(&cbs, input, input_len);

	if (!CBS_get_bytes(&cbs, &s_bytes,
	    encoded_vector_size(out_private_key->rank)))
		return 0;
	if (!vector_decode(priv.s, CBS_data(&s_bytes), kLog2Prime,
	    out_private_key->rank))
		return 0;
	if (!mlkem_parse_public_key_no_hash(&priv.pub, &cbs,
	    out_private_key->rank))
		return 0;

	memcpy(priv.pub.public_key_hash, CBS_data(&cbs), 32);
	if (!CBS_skip(&cbs, 32))
		return 0;
	memcpy(priv.fo_failure_secret, CBS_data(&cbs), 32);
	if (!CBS_skip(&cbs, 32))
		return 0;
	if (CBS_len(&cbs) != 0)
		return 0;

	return 1;
}
