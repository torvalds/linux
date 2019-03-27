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
 *	$FreeBSD$
 *
 */

#include "gfmult.h"

#define REV_POLY_REDUCT	0xe1	/* 0x87 bit reversed */

/* reverse the bits of a nibble */
static const uint8_t nib_rev[] = {
	0x0, 0x8, 0x4, 0xc, 0x2, 0xa, 0x6, 0xe,
	0x1, 0x9, 0x5, 0xd, 0x3, 0xb, 0x7, 0xf,
};

/* calculate v * 2 */
static inline struct gf128
gf128_mulalpha(struct gf128 v)
{
	uint64_t mask;

	mask = !!(v.v[1] & 1);
	mask = ~(mask - 1);
	v.v[1] = (v.v[1] >> 1) | ((v.v[0] & 1) << 63);
	v.v[0] = (v.v[0] >> 1) ^ ((mask & REV_POLY_REDUCT) << 56);

	return v;
}

/*
 * Generate a table for 0-16 * h.  Store the results in the table w/ indexes
 * bit reversed, and the words striped across the values.
 */
void
gf128_genmultable(struct gf128 h, struct gf128table *t)
{
	struct gf128 tbl[16];
	int i;

	tbl[0] = MAKE_GF128(0, 0);
	tbl[1] = h;

	for (i = 2; i < 16; i += 2) {
		tbl[i] = gf128_mulalpha(tbl[i / 2]);
		tbl[i + 1] = gf128_add(tbl[i], h);
	}

	for (i = 0; i < 16; i++) {
		t->a[nib_rev[i]] = tbl[i].v[0] >> 32;
		t->b[nib_rev[i]] = tbl[i].v[0];
		t->c[nib_rev[i]] = tbl[i].v[1] >> 32;
		t->d[nib_rev[i]] = tbl[i].v[1];
	}
}

/*
 * Generate tables containing h, h^2, h^3 and h^4, starting at 0.
 */
void
gf128_genmultable4(struct gf128 h, struct gf128table4 *t)
{
	struct gf128 h2, h3, h4;

	gf128_genmultable(h, &t->tbls[0]);

	h2 = gf128_mul(h, &t->tbls[0]);

	gf128_genmultable(h2, &t->tbls[1]);

	h3 = gf128_mul(h, &t->tbls[1]);
	gf128_genmultable(h3, &t->tbls[2]);

	h4 = gf128_mul(h2, &t->tbls[1]);
	gf128_genmultable(h4, &t->tbls[3]);
}

/*
 * Read a row from the table.
 */
static inline struct gf128
readrow(struct gf128table *tbl, unsigned bits)
{
	struct gf128 r;

	bits = bits % 16;

	r.v[0] = ((uint64_t)tbl->a[bits] << 32) | tbl->b[bits];
	r.v[1] = ((uint64_t)tbl->c[bits] << 32) | tbl->d[bits];

	return r;
}

/*
 * These are the reduction values.  Since we are dealing with bit reversed
 * version, the values need to be bit reversed, AND the indexes are also
 * bit reversed to make lookups quicker.
 */
static uint16_t reduction[] = {
	0x0000, 0x1c20, 0x3840, 0x2460, 0x7080, 0x6ca0, 0x48c0, 0x54e0,
	0xe100, 0xfd20, 0xd940, 0xc560, 0x9180, 0x8da0, 0xa9c0, 0xb5e0,
};

/*
 * Calculate:
 * (x*2^4 + word[3,0]*h) *
 * 2^4 + word[7,4]*h) *
 * ...
 * 2^4 + word[63,60]*h
 */
static struct gf128
gfmultword(uint64_t word, struct gf128 x, struct gf128table *tbl)
{
	struct gf128 row;
	unsigned bits;
	unsigned redbits;
	int i;

	for (i = 0; i < 64; i += 4) {
		bits = word % 16;

		/* fetch row */
		row = readrow(tbl, bits);

		/* x * 2^4 */
		redbits = x.v[1] % 16;
		x.v[1] = (x.v[1] >> 4) | (x.v[0] % 16) << 60;
		x.v[0] >>= 4;
		x.v[0] ^= (uint64_t)reduction[redbits] << (64 - 16);

		word >>= 4;

		x = gf128_add(x, row);
	}

	return x;
}

/*
 * Calculate
 * (x*2^4 + worda[3,0]*h^4+wordb[3,0]*h^3+...+wordd[3,0]*h) *
 * ...
 * 2^4 + worda[63,60]*h^4+ ... + wordd[63,60]*h
 *
 * Passing/returning struct is .5% faster than passing in via pointer on
 * amd64.
 */
static struct gf128
gfmultword4(uint64_t worda, uint64_t wordb, uint64_t wordc, uint64_t wordd,
    struct gf128 x, struct gf128table4 *tbl)
{
	struct gf128 rowa, rowb, rowc, rowd;
	unsigned bitsa, bitsb, bitsc, bitsd;
	unsigned redbits;
	int i;

	/*
	 * XXX - nibble reverse words to save a shift? probably not as
	 * nibble reverse would take 20 ops (5 * 4) verse 16
	 */

	for (i = 0; i < 64; i += 4) {
		bitsa = worda % 16;
		bitsb = wordb % 16;
		bitsc = wordc % 16;
		bitsd = wordd % 16;

		/* fetch row */
		rowa = readrow(&tbl->tbls[3], bitsa);
		rowb = readrow(&tbl->tbls[2], bitsb);
		rowc = readrow(&tbl->tbls[1], bitsc);
		rowd = readrow(&tbl->tbls[0], bitsd);

		/* x * 2^4 */
		redbits = x.v[1] % 16;
		x.v[1] = (x.v[1] >> 4) | (x.v[0] % 16) << 60;
		x.v[0] >>= 4;
		x.v[0] ^= (uint64_t)reduction[redbits] << (64 - 16);

		worda >>= 4;
		wordb >>= 4;
		wordc >>= 4;
		wordd >>= 4;

		x = gf128_add(x, gf128_add(rowa, gf128_add(rowb,
		    gf128_add(rowc, rowd))));
	}

	return x;
}

struct gf128
gf128_mul(struct gf128 v, struct gf128table *tbl)
{
	struct gf128 ret;

	ret = MAKE_GF128(0, 0);

	ret = gfmultword(v.v[1], ret, tbl);
	ret = gfmultword(v.v[0], ret, tbl);

	return ret;
}

/*
 * Calculate a*h^4 + b*h^3 + c*h^2 + d*h, or:
 * (((a*h+b)*h+c)*h+d)*h
 */
struct gf128
gf128_mul4(struct gf128 a, struct gf128 b, struct gf128 c, struct gf128 d,
    struct gf128table4 *tbl)
{
	struct gf128 tmp;

	tmp = MAKE_GF128(0, 0);

	tmp = gfmultword4(a.v[1], b.v[1], c.v[1], d.v[1], tmp, tbl);
	tmp = gfmultword4(a.v[0], b.v[0], c.v[0], d.v[0], tmp, tbl);

	return tmp;
}

/*
 * a = data[0..15] + r
 * b = data[16..31]
 * c = data[32..47]
 * d = data[48..63]
 *
 * Calculate a*h^4 + b*h^3 + c*h^2 + d*h, or:
 * (((a*h+b)*h+c)*h+d)*h
 */
struct gf128
gf128_mul4b(struct gf128 r, const uint8_t *v, struct gf128table4 *tbl)
{
	struct gf128 a, b, c, d;
	struct gf128 tmp;

	tmp = MAKE_GF128(0, 0);

	a = gf128_add(r, gf128_read(&v[0*16]));
	b = gf128_read(&v[1*16]);
	c = gf128_read(&v[2*16]);
	d = gf128_read(&v[3*16]);

	tmp = gfmultword4(a.v[1], b.v[1], c.v[1], d.v[1], tmp, tbl);
	tmp = gfmultword4(a.v[0], b.v[0], c.v[0], d.v[0], tmp, tbl);

	return tmp;
}
