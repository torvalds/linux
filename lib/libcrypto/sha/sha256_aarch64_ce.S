/* $OpenBSD: sha256_aarch64_ce.S,v 1.2 2025/03/12 12:53:33 jsing Exp $ */
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
 * SHA-256 implementation using the ARM Cryptographic Extension (CE).
 *
 * There are four instructions that enable hardware acceleration of SHA-256,
 * however the documentation for these is woefully inadequate:
 *
 *  sha256h:   hash update - part 1 (without a number to be inconsistent)
 *  sha256h2:  hash update - part 2
 *  sha256su0: message schedule update with sigma0 for four rounds
 *  sha256su1: message schedule update with sigma1 for four rounds
 */

#define ctx		x0
#define in		x1
#define num		x2

#define k256_base	x9
#define k256		x10

/* Note: the lower 64 bits of v8 through v15 are callee save. */

#define hc0		v16
#define hc1		v17

#define hs0		v18
#define hs1		v19

#define w0		v20
#define w1		v21
#define w2		v22
#define w3		v23

#define k0		v24
#define k1		v25
#define k2		v26
#define k3		v27

#define tmp0		v28
#define tmp1		v29

/*
 * Update message schedule for m0 (W0:W1:W2:W3), using m1 (W4:W5:W6:W7),
 * m2 (W8:W9:W10:11) and m3 (W12:W13:W14:W15). The sha256su0 instruction
 * computes the sigma0 component of the message schedule update as:
 *   W0:W1:W2:W3 = sigma0(W1:W2:W3:W4) + W0:W1:W2:W3
 * while sha256su1 computes the sigma1 component and adds in W9 as:
 *   W0:W1:W2:W3 = sigma1(W14:W15:W0:W1) + W9:W10:W12:W13 + W0:W1:W2:W3
 */
#define sha256_message_schedule_update(m0, m1, m2, m3) \
	sha256su0 m0.4s, m1.4s;						\
	sha256su1 m0.4s, m2.4s, m3.4s;

/*
 * Compute four SHA-256 rounds by adding W0:W1:W2:W3 + K0:K1:K2:K3, then
 * computing the remainder of each round (including the shuffle) via
 * sha256h/sha256h2.
 */
#define sha256_round(h0, h1, w, k) \
	add	tmp0.4s, w.4s, k.4s;		/* Tt = Wt + Kt */	\
	mov	tmp1.4s, h0.4s;						\
	sha256h	h0, h1, tmp0.4s;					\
	sha256h2 h1, tmp1, tmp0.4s;

#define sha256_round_update(h0, h1, m0, m1, m2, m3, k) \
	sha256_message_schedule_update(m0, m1, m2, m3)			\
	sha256_round(h0, h1, m0, k)

.arch	armv8-a+sha2

.text

/*
 * void sha256_block_ce(SHA256_CTX *ctx, const void *in, size_t num);
 *
 * Standard ARM ABI: x0 = ctx, x1 = in, x2 = num
 */
.globl	sha256_block_ce
.type   sha256_block_ce,@function
sha256_block_ce:

	/* Address of SHA-256 constants. */
	adrp	k256_base, K256
	add	k256_base, k256_base, :lo12:K256

	/*
	 * Load current hash state from context.
	 * hc0 = a:b:c:d, hc1 = e:f:g:h
	 */
	ld1	{hc0.4s, hc1.4s}, [ctx]

block_loop:
	mov	k256, k256_base

	/* Copy current hash state. */
	mov	hs0.4s, hc0.4s
	mov	hs1.4s, hc1.4s

	/* Load and byte swap message schedule. */
	ld1	{w0.16b, w1.16b, w2.16b, w3.16b}, [in], #64
	rev32	w0.16b, w0.16b
	rev32	w1.16b, w1.16b
	rev32	w2.16b, w2.16b
	rev32	w3.16b, w3.16b

	/* Rounds 0 through 15 (four rounds at a time). */
	ld1	{k0.4s, k1.4s, k2.4s, k3.4s}, [k256], #64

	sha256_round(hs0, hs1, w0, k0)
	sha256_round(hs0, hs1, w1, k1)
	sha256_round(hs0, hs1, w2, k2)
	sha256_round(hs0, hs1, w3, k3)

	/* Rounds 16 through 31 (four rounds at a time). */
	ld1	{k0.4s, k1.4s, k2.4s, k3.4s}, [k256], #64

	sha256_round_update(hs0, hs1, w0, w1, w2, w3, k0)
	sha256_round_update(hs0, hs1, w1, w2, w3, w0, k1)
	sha256_round_update(hs0, hs1, w2, w3, w0, w1, k2)
	sha256_round_update(hs0, hs1, w3, w0, w1, w2, k3)

	/* Rounds 32 through 47 (four rounds at a time). */
	ld1	{k0.4s, k1.4s, k2.4s, k3.4s}, [k256], #64

	sha256_round_update(hs0, hs1, w0, w1, w2, w3, k0)
	sha256_round_update(hs0, hs1, w1, w2, w3, w0, k1)
	sha256_round_update(hs0, hs1, w2, w3, w0, w1, k2)
	sha256_round_update(hs0, hs1, w3, w0, w1, w2, k3)

	/* Rounds 48 through 63 (four rounds at a time). */
	ld1	{k0.4s, k1.4s, k2.4s, k3.4s}, [k256], #64

	sha256_round_update(hs0, hs1, w0, w1, w2, w3, k0)
	sha256_round_update(hs0, hs1, w1, w2, w3, w0, k1)
	sha256_round_update(hs0, hs1, w2, w3, w0, w1, k2)
	sha256_round_update(hs0, hs1, w3, w0, w1, w2, k3)

	/* Add intermediate state to hash state. */
	add	hc0.4s, hc0.4s, hs0.4s
	add	hc1.4s, hc1.4s, hs1.4s

	sub	num, num, #1
	cbnz	num, block_loop

	/* Store hash state to context. */
	st1	{hc0.4s, hc1.4s}, [ctx]

	ret

/*
 * SHA-256 constants - see FIPS 180-4 section 4.2.3.
 */
.rodata
.align	4
.type	K256,@object
K256:
.long	0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5
.long	0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5
.long	0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3
.long	0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174
.long	0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc
.long	0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da
.long	0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7
.long	0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967
.long	0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13
.long	0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85
.long	0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3
.long	0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070
.long	0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5
.long	0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3
.long	0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208
.long	0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
.size	K256,.-K256
