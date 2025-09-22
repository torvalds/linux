/* $OpenBSD: sha512_aarch64_ce.S,v 1.1 2025/03/12 14:13:41 jsing Exp $ */
/*
 * Copyright (c) 2023,2025 Joel Sing <jsing@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * SHA-512 implementation using the ARM Cryptographic Extension (CE).
 *
 * The documentation for these is rather inadequate - each instruction is
 * described in a mechanical sense, however their combined usage does not
 * seem to be detailed anywhere.
 *
 * There are four instructions that enable hardware acceleration of SHA-512:
 *
 *  sha512h - hash update, part 1 (without a number to be inconsistent):
 *    inputs <W1:W0 + K1:K0 + g:h>, <f:g>, <d:e>
 *    output T1 for W0, T1 for W1
 *
 *  sha512h2 - hash update, part 2:
 *    inputs <T1 for W0, T1 for W1>, <c:d>, <a:b>
 *    output <T1 + T2 for W0, T1 + T2 for W1>
 *
 *  sha512su0 - message schedule update with sigma0 for two rounds:
 *    inputs <W0:W1>, <W2:W3>
 *    output W0 += sigma0(W1), W1 += sigma0(W2)
 *
 *  sha512su1 - message schedule update with sigma1 for two rounds:
 *    inputs <W0:W1>, <W14:W15>, <W9:W10>
 *    output W0 += sigma1(W14) + W9, W1 += sigma1(W15) + W10
 */

#define ctx		x0
#define in		x1
#define num		x2

#define k512_base	x3
#define k512		x4

/* Note: the lower 64 bits of v8 through v15 are callee save. */

#define hc0		v28
#define hc1		v29
#define hc2		v30
#define hc3		v31

#define hs0		v0
#define hs1		v1
#define hs2		v2
#define hs3		v3
#define hs4		v4
#define hs5		v5
#define hs6		v6
#define hs7		v7

#define w0		v10
#define w1		v11
#define w2		v12
#define w3		v13
#define w4		v14
#define w5		v15
#define w6		v16
#define w7		v17

#define k0		v20
#define k1		v21
#define k2		v22
#define k3		v23
#define k4		v24
#define k5		v25
#define k6		v26
#define k7		v27

#define tmp0		v8
#define tmp1		v9
#define tmp2		v18

/*
 * Update message schedule for m0 (W0:W1), using m1 (W2:W3), m4 (W8:W9),
 * m5 (W10:W11) and m7 (W14:W15). The sha512su0 instruction computes the sigma0
 * component of the message schedule update as m0 = sigma0(m1) + m0, while
 * sha512su1 computes the sigma1 component as m0 = sigma1(m7) + W9:W10 + m0.
 * Note that W9:W10 is split across two registers, hence this needs to be
 * constructed before it is passed to sha512su1:
 *
 *   W0 = sigma1(W14) + W9 + sigma0(W1) + W0
 */
#define sha512_message_schedule_update(m0, m1, m4, m5, m7) \
	sha512su0 m0.2d, m1.2d;			/* W0 += sigma0(W1) */	\
	ext	tmp2.16b, m4.16b, m5.16b, #8;	/* W9:W10 */		\
	sha512su1 m0.2d, m7.2d, tmp2.2d;	/* W0 += sigma1(W14) + W9 */

/*
 * Compute two SHA-512 rounds by adding W0:W1 + K0:K1, then computing T1 for two
 * rounds by swapping the double words, adding g:h and calling sha512h with this
 * value (W1:W0 = W1:W0 + K1:K0 + g:h), f:g and d:e. The new e:f value is then
 * computed by adding T1 + c:d (producing the next e:f values), before calling
 * sha512h2 with T1, c:d and a:b, computing T1 + T2 for two rounds (producing
 * the next a:b values):
 *
 *   T1 = h + Sigma1(e) + Ch(e, f, g) + Kt + Wt
 *   T2 = Sigma0(a) + Maj(a, b, c)
 * 
 *   h = g
 *   g = f
 *   f = e
 *   e = d + T1
 *   d = c
 *   c = b
 *   b = a
 *   a = T1 + T2
 *
 * The inputs are:
 * 
 *   h0 = a:b
 *   h1 = c:d
 *   h2 = e:f
 *   h3 = g:h
 *
 * Producing the following outputs:
 * 
 *   h4 = next a:b
 *   h5 = next e:f
 *
 * These values are then rotated by the caller to perform the next two rounds.
 */
#define sha512_round(h0, h1, h2, h3, h4, h5, w, k) \
	add	h4.2d, w.2d, k.2d;			/* W0:W1 += K0:K1 */	\
	ext	h4.16b, h4.16b, h4.16b, #8;		/* W1:W0 (swap) */	\
	add	h4.2d, h4.2d, h3.2d;			/* W1:W0 += g:h */	\
	ext	tmp0.16b, h2.16b, h3.16b, #8;		/* f:g */		\
	ext	tmp1.16b, h1.16b, h2.16b, #8;		/* d:e */		\
	sha512h	h4, tmp0, tmp1.2d;			/* T1 */		\
	add	h5.2d, h1.2d, h4.2d;			/* c:d + T1 */		\
	sha512h2 h4, h1, h0.2d;				/* T1 + T2 */

#define sha512_round_update(h0, h1, h2, h3, h4, h5, m0, m1, m2, m3, m4, k) \
	sha512_message_schedule_update(m0, m1, m2, m3, m4) \
	sha512_round(h0, h1, h2, h3, h4, h5, m0, k)

.arch	armv8-a+sha3

.text

/*
 * void sha512_block_ce(SHA512_CTX *ctx, const void *in, size_t num);
 *
 * Standard ARM ABI: x0 = ctx, x1 = in, x2 = num
 */
.globl	sha512_block_ce
sha512_block_ce:

	/* Save low 64 bits of v8 through v15 to the stack. */
	sub	sp, sp, #32
	st4	{v8.d, v9.d, v10.d, v11.d}[0], [sp]
	sub	sp, sp, #32
	st4	{v12.d, v13.d, v14.d, v15.d}[0], [sp]

	/* Address of SHA-512 constants. */
	adrp	k512_base, K512
	add	k512_base, k512_base, :lo12:K512

	/*
	 * Load current hash state from context.
	 * hc0 = a:b, hc1 = c:d, hc2 = e:f, hc3 = g:h
	 */
	ld1	{hc0.2d, hc1.2d, hc2.2d, hc3.2d}, [ctx]

block_loop:
	mov	k512, k512_base

	/* Copy current hash state. */
	mov	hs0.2d, hc0.2d
	mov	hs1.2d, hc1.2d
	mov	hs2.2d, hc2.2d
	mov	hs3.2d, hc3.2d

	/* Load and byte swap message schedule. */
	ld1	{w0.16b, w1.16b, w2.16b, w3.16b}, [in], #64
	rev64	w0.16b, w0.16b
	rev64	w1.16b, w1.16b
	rev64	w2.16b, w2.16b
	rev64	w3.16b, w3.16b

	ld1	{w4.2d, w5.2d, w6.2d, w7.2d}, [in], #64
	rev64	w4.16b, w4.16b
	rev64	w5.16b, w5.16b
	rev64	w6.16b, w6.16b
	rev64	w7.16b, w7.16b

	/* Rounds 0 through 15 (two rounds at a time). */
	ld1	{k0.2d, k1.2d, k2.2d, k3.2d}, [k512], #64
	ld1	{k4.2d, k5.2d, k6.2d, k7.2d}, [k512], #64

	sha512_round(hs0, hs1, hs2, hs3, hs4, hs5, w0, k0)
	sha512_round(hs4, hs0, hs5, hs2, hs6, hs7, w1, k1)
	sha512_round(hs6, hs4, hs7, hs5, hs1, hs3, w2, k2)
	sha512_round(hs1, hs6, hs3, hs7, hs0, hs2, w3, k3)
	sha512_round(hs0, hs1, hs2, hs3, hs4, hs5, w4, k4)
	sha512_round(hs4, hs0, hs5, hs2, hs6, hs7, w5, k5)
	sha512_round(hs6, hs4, hs7, hs5, hs1, hs3, w6, k6)
	sha512_round(hs1, hs6, hs3, hs7, hs0, hs2, w7, k7)

	/* Rounds 16 through 31 (two rounds at a time). */
	ld1	{k0.2d, k1.2d, k2.2d, k3.2d}, [k512], #64
	ld1	{k4.2d, k5.2d, k6.2d, k7.2d}, [k512], #64

	sha512_round_update(hs0, hs1, hs2, hs3, hs4, hs5, w0, w1, w4, w5, w7, k0)
	sha512_round_update(hs4, hs0, hs5, hs2, hs6, hs7, w1, w2, w5, w6, w0, k1)
	sha512_round_update(hs6, hs4, hs7, hs5, hs1, hs3, w2, w3, w6, w7, w1, k2)
	sha512_round_update(hs1, hs6, hs3, hs7, hs0, hs2, w3, w4, w7, w0, w2, k3)
	sha512_round_update(hs0, hs1, hs2, hs3, hs4, hs5, w4, w5, w0, w1, w3, k4)
	sha512_round_update(hs4, hs0, hs5, hs2, hs6, hs7, w5, w6, w1, w2, w4, k5)
	sha512_round_update(hs6, hs4, hs7, hs5, hs1, hs3, w6, w7, w2, w3, w5, k6)
	sha512_round_update(hs1, hs6, hs3, hs7, hs0, hs2, w7, w0, w3, w4, w6, k7)

	/* Rounds 32 through 47 (two rounds at a time). */
	ld1	{k0.2d, k1.2d, k2.2d, k3.2d}, [k512], #64
	ld1	{k4.2d, k5.2d, k6.2d, k7.2d}, [k512], #64

	sha512_round_update(hs0, hs1, hs2, hs3, hs4, hs5, w0, w1, w4, w5, w7, k0)
	sha512_round_update(hs4, hs0, hs5, hs2, hs6, hs7, w1, w2, w5, w6, w0, k1)
	sha512_round_update(hs6, hs4, hs7, hs5, hs1, hs3, w2, w3, w6, w7, w1, k2)
	sha512_round_update(hs1, hs6, hs3, hs7, hs0, hs2, w3, w4, w7, w0, w2, k3)
	sha512_round_update(hs0, hs1, hs2, hs3, hs4, hs5, w4, w5, w0, w1, w3, k4)
	sha512_round_update(hs4, hs0, hs5, hs2, hs6, hs7, w5, w6, w1, w2, w4, k5)
	sha512_round_update(hs6, hs4, hs7, hs5, hs1, hs3, w6, w7, w2, w3, w5, k6)
	sha512_round_update(hs1, hs6, hs3, hs7, hs0, hs2, w7, w0, w3, w4, w6, k7)

	/* Rounds 48 through 63 (two rounds at a time). */
	ld1	{k0.2d, k1.2d, k2.2d, k3.2d}, [k512], #64
	ld1	{k4.2d, k5.2d, k6.2d, k7.2d}, [k512], #64

	sha512_round_update(hs0, hs1, hs2, hs3, hs4, hs5, w0, w1, w4, w5, w7, k0)
	sha512_round_update(hs4, hs0, hs5, hs2, hs6, hs7, w1, w2, w5, w6, w0, k1)
	sha512_round_update(hs6, hs4, hs7, hs5, hs1, hs3, w2, w3, w6, w7, w1, k2)
	sha512_round_update(hs1, hs6, hs3, hs7, hs0, hs2, w3, w4, w7, w0, w2, k3)
	sha512_round_update(hs0, hs1, hs2, hs3, hs4, hs5, w4, w5, w0, w1, w3, k4)
	sha512_round_update(hs4, hs0, hs5, hs2, hs6, hs7, w5, w6, w1, w2, w4, k5)
	sha512_round_update(hs6, hs4, hs7, hs5, hs1, hs3, w6, w7, w2, w3, w5, k6)
	sha512_round_update(hs1, hs6, hs3, hs7, hs0, hs2, w7, w0, w3, w4, w6, k7)

	/* Rounds 64 through 79 (two rounds at a time). */
	ld1	{k0.2d, k1.2d, k2.2d, k3.2d}, [k512], #64
	ld1	{k4.2d, k5.2d, k6.2d, k7.2d}, [k512], #64

	sha512_round_update(hs0, hs1, hs2, hs3, hs4, hs5, w0, w1, w4, w5, w7, k0)
	sha512_round_update(hs4, hs0, hs5, hs2, hs6, hs7, w1, w2, w5, w6, w0, k1)
	sha512_round_update(hs6, hs4, hs7, hs5, hs1, hs3, w2, w3, w6, w7, w1, k2)
	sha512_round_update(hs1, hs6, hs3, hs7, hs0, hs2, w3, w4, w7, w0, w2, k3)
	sha512_round_update(hs0, hs1, hs2, hs3, hs4, hs5, w4, w5, w0, w1, w3, k4)
	sha512_round_update(hs4, hs0, hs5, hs2, hs6, hs7, w5, w6, w1, w2, w4, k5)
	sha512_round_update(hs6, hs4, hs7, hs5, hs1, hs3, w6, w7, w2, w3, w5, k6)
	sha512_round_update(hs1, hs6, hs3, hs7, hs0, hs2, w7, w0, w3, w4, w6, k7)

	/* Add intermediate state to hash state. */
	add	hc0.2d, hc0.2d, hs0.2d
	add	hc1.2d, hc1.2d, hs1.2d
	add	hc2.2d, hc2.2d, hs2.2d
	add	hc3.2d, hc3.2d, hs3.2d

	sub	num, num, #1
	cbnz	num, block_loop

	/* Store hash state to context. */
	st1	{hc0.2d, hc1.2d, hc2.2d, hc3.2d}, [ctx]

	/* Restore low 64 bits of v8 through v15 from the stack. */
	ld4	{v12.d, v13.d, v14.d, v15.d}[0], [sp], #32
	ld4	{v8.d, v9.d, v10.d, v11.d}[0], [sp], #32

	ret

/*
 * SHA-512 constants - see FIPS 180-4 section 4.2.3.
 */
.rodata
.align	4
.type	K512,@object
K512:
.quad	0x428a2f98d728ae22, 0x7137449123ef65cd, 0xb5c0fbcfec4d3b2f, 0xe9b5dba58189dbbc
.quad	0x3956c25bf348b538, 0x59f111f1b605d019, 0x923f82a4af194f9b, 0xab1c5ed5da6d8118
.quad	0xd807aa98a3030242, 0x12835b0145706fbe, 0x243185be4ee4b28c, 0x550c7dc3d5ffb4e2
.quad	0x72be5d74f27b896f, 0x80deb1fe3b1696b1, 0x9bdc06a725c71235, 0xc19bf174cf692694
.quad	0xe49b69c19ef14ad2, 0xefbe4786384f25e3, 0x0fc19dc68b8cd5b5, 0x240ca1cc77ac9c65
.quad	0x2de92c6f592b0275, 0x4a7484aa6ea6e483, 0x5cb0a9dcbd41fbd4, 0x76f988da831153b5
.quad	0x983e5152ee66dfab, 0xa831c66d2db43210, 0xb00327c898fb213f, 0xbf597fc7beef0ee4
.quad	0xc6e00bf33da88fc2, 0xd5a79147930aa725, 0x06ca6351e003826f, 0x142929670a0e6e70
.quad	0x27b70a8546d22ffc, 0x2e1b21385c26c926, 0x4d2c6dfc5ac42aed, 0x53380d139d95b3df
.quad	0x650a73548baf63de, 0x766a0abb3c77b2a8, 0x81c2c92e47edaee6, 0x92722c851482353b
.quad	0xa2bfe8a14cf10364, 0xa81a664bbc423001, 0xc24b8b70d0f89791, 0xc76c51a30654be30
.quad	0xd192e819d6ef5218, 0xd69906245565a910, 0xf40e35855771202a, 0x106aa07032bbd1b8
.quad	0x19a4c116b8d2d0c8, 0x1e376c085141ab53, 0x2748774cdf8eeb99, 0x34b0bcb5e19b48a8
.quad	0x391c0cb3c5c95a63, 0x4ed8aa4ae3418acb, 0x5b9cca4f7763e373, 0x682e6ff3d6b2b8a3
.quad	0x748f82ee5defb2fc, 0x78a5636f43172f60, 0x84c87814a1f0ab72, 0x8cc702081a6439ec
.quad	0x90befffa23631e28, 0xa4506cebde82bde9, 0xbef9a3f7b2c67915, 0xc67178f2e372532b
.quad	0xca273eceea26619c, 0xd186b8c721c0c207, 0xeada7dd6cde0eb1e, 0xf57d4f7fee6ed178
.quad	0x06f067aa72176fba, 0x0a637dc5a2c898a6, 0x113f9804bef90dae, 0x1b710b35131c471b
.quad	0x28db77f523047d84, 0x32caab7b40c72493, 0x3c9ebe0a15c9bebc, 0x431d67c49c100d4c
.quad	0x4cc5d4becb3e42b6, 0x597f299cfc657e2a, 0x5fcb6fab3ad6faec, 0x6c44198c4a475817
.size	K512,.-K512
