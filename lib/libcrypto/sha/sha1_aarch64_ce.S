/* $OpenBSD: sha1_aarch64_ce.S,v 1.1 2025/06/28 12:51:08 jsing Exp $ */
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
 * SHA-1 implementation using the ARM Cryptographic Extension (CE).
 *
 * There are six instructions for hardware acceleration of SHA-1 - the
 * documentation for these instructions is woefully inadequate:
 *
 *  sha1c:   hash update (choose)
 *  sha1h:   fixed rotate
 *  sha1m:   hash update (majority)
 *  sha1p:   hash update (parity)
 *  sha1su0: message schedule update with sigma0 for four rounds
 *  sha1su1: message schedule update with sigma1 for four rounds
 */

#define ctx		x0
#define in		x1
#define num		x2

/* Note: the lower 64 bits of v8 through v15 are callee saved. */

#define hc0		v16
#define hc1		v17
#define hc1s		s17

#define hs0		v18
#define hs1		v19
#define hs1s		s19

#define w0		v20
#define w1		v21
#define w2		v22
#define w3		v23

#define k0		v24
#define k1		v25
#define k2		v26
#define k3		v27

#define tmp0		v28
#define tmp1		s29

#define tmp2		w11

/*
 * Update message schedule for m0 (W0:W1:W2:W3), using m1 (W4:W5:W6:W7),
 * m2 (W8:W9:W10:11) and m3 (W12:W13:W14:W15). The sha1su0 instruction computes
 * W0 = W8 ^ W2 ^ W0, while sha1su1 computes rol(W0 ^ W13, 1).
 */
#define sha1_message_schedule_update(m0, m1, m2, m3) \
	sha1su0 m0.4s, m1.4s, m2.4s;					\
	sha1su1 m0.4s, m3.4s;

/*
 * Compute four SHA-1 rounds by adding W0:W1:W2:W3 + K0:K1:K2:K3, then
 * computing the remainder of each round (including the shuffle) via
 * sha1{c,p,m}/sha1h.
 */

#define sha1_round1(h0, h1, w, k) \
	add	tmp0.4s, w.4s, k.4s;		/* Tt = Wt + Kt */	\
	mov	tmp1, h0.s[0];						\
	sha1c	h0, h1, tmp0.4s;					\
	sha1h	h1, tmp1;

#define sha1_round2(h0, h1, w, k) \
	add	tmp0.4s, w.4s, k.4s;		/* Tt = Wt + Kt */	\
	mov	tmp1, h0.s[0];						\
	sha1p	h0, h1, tmp0.4s;					\
	sha1h	h1, tmp1;

#define sha1_round3(h0, h1, w, k) \
	add	tmp0.4s, w.4s, k.4s;		/* Tt = Wt + Kt */	\
	mov	tmp1, h0.s[0];						\
	sha1m	h0, h1, tmp0.4s;					\
	sha1h	h1, tmp1;

#define sha1_round4(h0, h1, w, k) \
	add	tmp0.4s, w.4s, k.4s;		/* Tt = Wt + Kt */	\
	mov	tmp1, h0.s[0];						\
	sha1p	h0, h1, tmp0.4s;					\
	sha1h	h1, tmp1;

.arch	armv8-a+sha2

.text

/*
 * void sha1_block_ce(SHA256_CTX *ctx, const void *in, size_t num);
 *
 * Standard ARM ABI: x0 = ctx, x1 = in, x2 = num
 */
.globl	sha1_block_ce
.type   sha1_block_ce,@function
sha1_block_ce:

	/*
	 * Load SHA-1 round constants.
	 */

	/* Round 1 - 0x5a827999 */
	movz	tmp2, #0x5a82, lsl #16
	movk	tmp2, #0x7999
	dup	k0.4s, tmp2

	/* Round 2 - 0x6ed9eba1 */
	movz	tmp2, #0x6ed9, lsl #16
	movk	tmp2, #0xeba1
	dup	k1.4s, tmp2

	/* Round 3 - 0x8f1bbcdc */
	movz	tmp2, #0x8f1b, lsl #16
	movk	tmp2, #0xbcdc
	dup	k2.4s, tmp2

	/* Round 4 - 0xca62c1d6 */
	movz	tmp2, #0xca62, lsl #16
	movk	tmp2, #0xc1d6
	dup	k3.4s, tmp2

	/* Load current hash state from context (hc0 = a:b:c:d, hc1 = e). */
	ld1	{hc0.4s}, [ctx]
	ldr	hc1s, [ctx, #(4*4)]

block_loop:
	/* Copy current hash state. */
	mov	hs0.4s, hc0.4s
	mov	hs1s, hc1.s[0]

	/* Load and byte swap message schedule. */
	ld1	{w0.16b, w1.16b, w2.16b, w3.16b}, [in], #64
	rev32	w0.16b, w0.16b
	rev32	w1.16b, w1.16b
	rev32	w2.16b, w2.16b
	rev32	w3.16b, w3.16b

	/* Rounds 0 through 15 (four rounds at a time). */
	sha1_round1(hs0, hs1s, w0, k0)
	sha1_round1(hs0, hs1s, w1, k0)
	sha1_round1(hs0, hs1s, w2, k0)
	sha1_round1(hs0, hs1s, w3, k0)

	/* Rounds 16 through 31 (four rounds at a time). */
	sha1_message_schedule_update(w0, w1, w2, w3)
	sha1_message_schedule_update(w1, w2, w3, w0)
	sha1_message_schedule_update(w2, w3, w0, w1)
	sha1_message_schedule_update(w3, w0, w1, w2)

	sha1_round1(hs0, hs1s, w0, k0)
	sha1_round2(hs0, hs1s, w1, k1)
	sha1_round2(hs0, hs1s, w2, k1)
	sha1_round2(hs0, hs1s, w3, k1)

	/* Rounds 32 through 47 (four rounds at a time). */
	sha1_message_schedule_update(w0, w1, w2, w3)
	sha1_message_schedule_update(w1, w2, w3, w0)
	sha1_message_schedule_update(w2, w3, w0, w1)
	sha1_message_schedule_update(w3, w0, w1, w2)

	sha1_round2(hs0, hs1s, w0, k1)
	sha1_round2(hs0, hs1s, w1, k1)
	sha1_round3(hs0, hs1s, w2, k2)
	sha1_round3(hs0, hs1s, w3, k2)

	/* Rounds 48 through 63 (four rounds at a time). */
	sha1_message_schedule_update(w0, w1, w2, w3)
	sha1_message_schedule_update(w1, w2, w3, w0)
	sha1_message_schedule_update(w2, w3, w0, w1)
	sha1_message_schedule_update(w3, w0, w1, w2)

	sha1_round3(hs0, hs1s, w0, k2)
	sha1_round3(hs0, hs1s, w1, k2)
	sha1_round3(hs0, hs1s, w2, k2)
	sha1_round4(hs0, hs1s, w3, k3)

	/* Rounds 64 through 79 (four rounds at a time). */
	sha1_message_schedule_update(w0, w1, w2, w3)
	sha1_message_schedule_update(w1, w2, w3, w0)
	sha1_message_schedule_update(w2, w3, w0, w1)
	sha1_message_schedule_update(w3, w0, w1, w2)

	sha1_round4(hs0, hs1s, w0, k3)
	sha1_round4(hs0, hs1s, w1, k3)
	sha1_round4(hs0, hs1s, w2, k3)
	sha1_round4(hs0, hs1s, w3, k3)

	/* Add intermediate state to hash state. */
	add	hc0.4s, hc0.4s, hs0.4s
	add	hc1.4s, hc1.4s, hs1.4s

	sub	num, num, #1
	cbnz	num, block_loop

	/* Store hash state to context. */
	st1	{hc0.4s}, [ctx]
	str	hc1s, [ctx, #(4*4)]

	ret
