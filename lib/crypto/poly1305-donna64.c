// SPDX-License-Identifier: GPL-2.0 OR MIT
/*
 * Copyright (C) 2015-2019 Jason A. Donenfeld <Jason@zx2c4.com>. All Rights Reserved.
 *
 * This is based in part on Andrew Moon's poly1305-donna, which is in the
 * public domain.
 */

#include <linux/kernel.h>
#include <asm/unaligned.h>
#include <crypto/internal/poly1305.h>

void poly1305_core_setkey(struct poly1305_core_key *key,
			  const u8 raw_key[POLY1305_BLOCK_SIZE])
{
	u64 t0, t1;

	/* r &= 0xffffffc0ffffffc0ffffffc0fffffff */
	t0 = get_unaligned_le64(&raw_key[0]);
	t1 = get_unaligned_le64(&raw_key[8]);

	key->key.r64[0] = t0 & 0xffc0fffffffULL;
	key->key.r64[1] = ((t0 >> 44) | (t1 << 20)) & 0xfffffc0ffffULL;
	key->key.r64[2] = ((t1 >> 24)) & 0x00ffffffc0fULL;

	/* s = 20*r */
	key->precomputed_s.r64[0] = key->key.r64[1] * 20;
	key->precomputed_s.r64[1] = key->key.r64[2] * 20;
}
EXPORT_SYMBOL(poly1305_core_setkey);

void poly1305_core_blocks(struct poly1305_state *state,
			  const struct poly1305_core_key *key, const void *src,
			  unsigned int nblocks, u32 hibit)
{
	const u8 *input = src;
	u64 hibit64;
	u64 r0, r1, r2;
	u64 s1, s2;
	u64 h0, h1, h2;
	u64 c;
	u128 d0, d1, d2, d;

	if (!nblocks)
		return;

	hibit64 = ((u64)hibit) << 40;

	r0 = key->key.r64[0];
	r1 = key->key.r64[1];
	r2 = key->key.r64[2];

	h0 = state->h64[0];
	h1 = state->h64[1];
	h2 = state->h64[2];

	s1 = key->precomputed_s.r64[0];
	s2 = key->precomputed_s.r64[1];

	do {
		u64 t0, t1;

		/* h += m[i] */
		t0 = get_unaligned_le64(&input[0]);
		t1 = get_unaligned_le64(&input[8]);

		h0 += t0 & 0xfffffffffffULL;
		h1 += ((t0 >> 44) | (t1 << 20)) & 0xfffffffffffULL;
		h2 += (((t1 >> 24)) & 0x3ffffffffffULL) | hibit64;

		/* h *= r */
		d0 = (u128)h0 * r0;
		d = (u128)h1 * s2;
		d0 += d;
		d = (u128)h2 * s1;
		d0 += d;
		d1 = (u128)h0 * r1;
		d = (u128)h1 * r0;
		d1 += d;
		d = (u128)h2 * s2;
		d1 += d;
		d2 = (u128)h0 * r2;
		d = (u128)h1 * r1;
		d2 += d;
		d = (u128)h2 * r0;
		d2 += d;

		/* (partial) h %= p */
		c = (u64)(d0 >> 44);
		h0 = (u64)d0 & 0xfffffffffffULL;
		d1 += c;
		c = (u64)(d1 >> 44);
		h1 = (u64)d1 & 0xfffffffffffULL;
		d2 += c;
		c = (u64)(d2 >> 42);
		h2 = (u64)d2 & 0x3ffffffffffULL;
		h0 += c * 5;
		c = h0 >> 44;
		h0 = h0 & 0xfffffffffffULL;
		h1 += c;

		input += POLY1305_BLOCK_SIZE;
	} while (--nblocks);

	state->h64[0] = h0;
	state->h64[1] = h1;
	state->h64[2] = h2;
}
EXPORT_SYMBOL(poly1305_core_blocks);

void poly1305_core_emit(const struct poly1305_state *state, const u32 nonce[4],
			void *dst)
{
	u8 *mac = dst;
	u64 h0, h1, h2, c;
	u64 g0, g1, g2;
	u64 t0, t1;

	/* fully carry h */
	h0 = state->h64[0];
	h1 = state->h64[1];
	h2 = state->h64[2];

	c = h1 >> 44;
	h1 &= 0xfffffffffffULL;
	h2 += c;
	c = h2 >> 42;
	h2 &= 0x3ffffffffffULL;
	h0 += c * 5;
	c = h0 >> 44;
	h0 &= 0xfffffffffffULL;
	h1 += c;
	c = h1 >> 44;
	h1 &= 0xfffffffffffULL;
	h2 += c;
	c = h2 >> 42;
	h2 &= 0x3ffffffffffULL;
	h0 += c * 5;
	c = h0 >> 44;
	h0 &= 0xfffffffffffULL;
	h1 += c;

	/* compute h + -p */
	g0 = h0 + 5;
	c  = g0 >> 44;
	g0 &= 0xfffffffffffULL;
	g1 = h1 + c;
	c  = g1 >> 44;
	g1 &= 0xfffffffffffULL;
	g2 = h2 + c - (1ULL << 42);

	/* select h if h < p, or h + -p if h >= p */
	c = (g2 >> ((sizeof(u64) * 8) - 1)) - 1;
	g0 &= c;
	g1 &= c;
	g2 &= c;
	c  = ~c;
	h0 = (h0 & c) | g0;
	h1 = (h1 & c) | g1;
	h2 = (h2 & c) | g2;

	if (likely(nonce)) {
		/* h = (h + nonce) */
		t0 = ((u64)nonce[1] << 32) | nonce[0];
		t1 = ((u64)nonce[3] << 32) | nonce[2];

		h0 += t0 & 0xfffffffffffULL;
		c = h0 >> 44;
		h0 &= 0xfffffffffffULL;
		h1 += (((t0 >> 44) | (t1 << 20)) & 0xfffffffffffULL) + c;
		c = h1 >> 44;
		h1 &= 0xfffffffffffULL;
		h2 += (((t1 >> 24)) & 0x3ffffffffffULL) + c;
		h2 &= 0x3ffffffffffULL;
	}

	/* mac = h % (2^128) */
	h0 = h0 | (h1 << 44);
	h1 = (h1 >> 20) | (h2 << 24);

	put_unaligned_le64(h0, &mac[0]);
	put_unaligned_le64(h1, &mac[8]);
}
EXPORT_SYMBOL(poly1305_core_emit);
