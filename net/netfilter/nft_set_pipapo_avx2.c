// SPDX-License-Identifier: GPL-2.0-only

/* PIPAPO: PIle PAcket POlicies: AVX2 packet lookup routines
 *
 * Copyright (c) 2019-2020 Red Hat GmbH
 *
 * Author: Stefano Brivio <sbrivio@redhat.com>
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/netlink.h>
#include <linux/netfilter.h>
#include <linux/netfilter/nf_tables.h>
#include <net/netfilter/nf_tables_core.h>
#include <uapi/linux/netfilter/nf_tables.h>
#include <linux/bitmap.h>
#include <linux/bitops.h>

#include <linux/compiler.h>
#include <asm/fpu/api.h>

#include "nft_set_pipapo_avx2.h"
#include "nft_set_pipapo.h"

#define NFT_PIPAPO_LONGS_PER_M256	(XSAVE_YMM_SIZE / BITS_PER_LONG)

/* Load from memory into YMM register with non-temporal hint ("stream load"),
 * that is, don't fetch lines from memory into the cache. This avoids pushing
 * precious packet data out of the cache hierarchy, and is appropriate when:
 *
 * - loading buckets from lookup tables, as they are not going to be used
 *   again before packets are entirely classified
 *
 * - loading the result bitmap from the previous field, as it's never used
 *   again
 */
#define NFT_PIPAPO_AVX2_LOAD(reg, loc)					\
	asm volatile("vmovntdqa %0, %%ymm" #reg : : "m" (loc))

/* Stream a single lookup table bucket into YMM register given lookup table,
 * group index, value of packet bits, bucket size.
 */
#define NFT_PIPAPO_AVX2_BUCKET_LOAD4(reg, lt, group, v, bsize)		\
	NFT_PIPAPO_AVX2_LOAD(reg,					\
			     lt[((group) * NFT_PIPAPO_BUCKETS(4) +	\
				 (v)) * (bsize)])
#define NFT_PIPAPO_AVX2_BUCKET_LOAD8(reg, lt, group, v, bsize)		\
	NFT_PIPAPO_AVX2_LOAD(reg,					\
			     lt[((group) * NFT_PIPAPO_BUCKETS(8) +	\
				 (v)) * (bsize)])

/* Bitwise AND: the staple operation of this algorithm */
#define NFT_PIPAPO_AVX2_AND(dst, a, b)					\
	asm volatile("vpand %ymm" #a ", %ymm" #b ", %ymm" #dst)

/* Jump to label if @reg is zero */
#define NFT_PIPAPO_AVX2_NOMATCH_GOTO(reg, label)			\
	asm_volatile_goto("vptest %%ymm" #reg ", %%ymm" #reg ";"	\
			  "je %l[" #label "]" : : : : label)

/* Store 256 bits from YMM register into memory. Contrary to bucket load
 * operation, we don't bypass the cache here, as stored matching results
 * are always used shortly after.
 */
#define NFT_PIPAPO_AVX2_STORE(loc, reg)					\
	asm volatile("vmovdqa %%ymm" #reg ", %0" : "=m" (loc))

/* Zero out a complete YMM register, @reg */
#define NFT_PIPAPO_AVX2_ZERO(reg)					\
	asm volatile("vpxor %ymm" #reg ", %ymm" #reg ", %ymm" #reg)

/* Current working bitmap index, toggled between field matches */
static DEFINE_PER_CPU(bool, nft_pipapo_avx2_scratch_index);

/**
 * nft_pipapo_avx2_prepare() - Prepare before main algorithm body
 *
 * This zeroes out ymm15, which is later used whenever we need to clear a
 * memory location, by storing its content into memory.
 */
static void nft_pipapo_avx2_prepare(void)
{
	NFT_PIPAPO_AVX2_ZERO(15);
}

/**
 * nft_pipapo_avx2_fill() - Fill a bitmap region with ones
 * @data:	Base memory area
 * @start:	First bit to set
 * @len:	Count of bits to fill
 *
 * This is nothing else than a version of bitmap_set(), as used e.g. by
 * pipapo_refill(), tailored for the microarchitectures using it and better
 * suited for the specific usage: it's very likely that we'll set a small number
 * of bits, not crossing a word boundary, and correct branch prediction is
 * critical here.
 *
 * This function doesn't actually use any AVX2 instruction.
 */
static void nft_pipapo_avx2_fill(unsigned long *data, int start, int len)
{
	int offset = start % BITS_PER_LONG;
	unsigned long mask;

	data += start / BITS_PER_LONG;

	if (likely(len == 1)) {
		*data |= BIT(offset);
		return;
	}

	if (likely(len < BITS_PER_LONG || offset)) {
		if (likely(len + offset <= BITS_PER_LONG)) {
			*data |= GENMASK(len - 1 + offset, offset);
			return;
		}

		*data |= ~0UL << offset;
		len -= BITS_PER_LONG - offset;
		data++;

		if (len <= BITS_PER_LONG) {
			mask = ~0UL >> (BITS_PER_LONG - len);
			*data |= mask;
			return;
		}
	}

	memset(data, 0xff, len / BITS_PER_BYTE);
	data += len / BITS_PER_LONG;

	len %= BITS_PER_LONG;
	if (len)
		*data |= ~0UL >> (BITS_PER_LONG - len);
}

/**
 * nft_pipapo_avx2_refill() - Scan bitmap, select mapping table item, set bits
 * @offset:	Start from given bitmap (equivalent to bucket) offset, in longs
 * @map:	Bitmap to be scanned for set bits
 * @dst:	Destination bitmap
 * @mt:		Mapping table containing bit set specifiers
 * @last:	Return index of first set bit, if this is the last field
 *
 * This is an alternative implementation of pipapo_refill() suitable for usage
 * with AVX2 lookup routines: we know there are four words to be scanned, at
 * a given offset inside the map, for each matching iteration.
 *
 * This function doesn't actually use any AVX2 instruction.
 *
 * Return: first set bit index if @last, index of first filled word otherwise.
 */
static int nft_pipapo_avx2_refill(int offset, unsigned long *map,
				  unsigned long *dst,
				  union nft_pipapo_map_bucket *mt, bool last)
{
	int ret = -1;

#define NFT_PIPAPO_AVX2_REFILL_ONE_WORD(x)				\
	do {								\
		while (map[(x)]) {					\
			int r = __builtin_ctzl(map[(x)]);		\
			int i = (offset + (x)) * BITS_PER_LONG + r;	\
									\
			if (last)					\
				return i;				\
									\
			nft_pipapo_avx2_fill(dst, mt[i].to, mt[i].n);	\
									\
			if (ret == -1)					\
				ret = mt[i].to;				\
									\
			map[(x)] &= ~(1UL << r);			\
		}							\
	} while (0)

	NFT_PIPAPO_AVX2_REFILL_ONE_WORD(0);
	NFT_PIPAPO_AVX2_REFILL_ONE_WORD(1);
	NFT_PIPAPO_AVX2_REFILL_ONE_WORD(2);
	NFT_PIPAPO_AVX2_REFILL_ONE_WORD(3);
#undef NFT_PIPAPO_AVX2_REFILL_ONE_WORD

	return ret;
}

/**
 * nft_pipapo_avx2_lookup_4b_2() - AVX2-based lookup for 2 four-bit groups
 * @map:	Previous match result, used as initial bitmap
 * @fill:	Destination bitmap to be filled with current match result
 * @f:		Field, containing lookup and mapping tables
 * @offset:	Ignore buckets before the given index, no bits are filled there
 * @pkt:	Packet data, pointer to input nftables register
 * @first:	If this is the first field, don't source previous result
 * @last:	Last field: stop at the first match and return bit index
 *
 * Load buckets from lookup table corresponding to the values of each 4-bit
 * group of packet bytes, and perform a bitwise intersection between them. If
 * this is the first field in the set, simply AND the buckets together
 * (equivalent to using an all-ones starting bitmap), use the provided starting
 * bitmap otherwise. Then call nft_pipapo_avx2_refill() to generate the next
 * working bitmap, @fill.
 *
 * This is used for 8-bit fields (i.e. protocol numbers).
 *
 * Out-of-order (and superscalar) execution is vital here, so it's critical to
 * avoid false data dependencies. CPU and compiler could (mostly) take care of
 * this on their own, but the operation ordering is explicitly given here with
 * a likely execution order in mind, to highlight possible stalls. That's why
 * a number of logically distinct operations (i.e. loading buckets, intersecting
 * buckets) are interleaved.
 *
 * Return: -1 on no match, rule index of match if @last, otherwise first long
 * word index to be checked next (i.e. first filled word).
 */
static int nft_pipapo_avx2_lookup_4b_2(unsigned long *map, unsigned long *fill,
				       struct nft_pipapo_field *f, int offset,
				       const u8 *pkt, bool first, bool last)
{
	int i, ret = -1, m256_size = f->bsize / NFT_PIPAPO_LONGS_PER_M256, b;
	u8 pg[2] = { pkt[0] >> 4, pkt[0] & 0xf };
	unsigned long *lt = f->lt, bsize = f->bsize;

	lt += offset * NFT_PIPAPO_LONGS_PER_M256;
	for (i = offset; i < m256_size; i++, lt += NFT_PIPAPO_LONGS_PER_M256) {
		int i_ul = i * NFT_PIPAPO_LONGS_PER_M256;

		if (first) {
			NFT_PIPAPO_AVX2_BUCKET_LOAD4(0, lt, 0, pg[0], bsize);
			NFT_PIPAPO_AVX2_BUCKET_LOAD4(1, lt, 1, pg[1], bsize);
			NFT_PIPAPO_AVX2_AND(4, 0, 1);
		} else {
			NFT_PIPAPO_AVX2_BUCKET_LOAD4(0, lt, 0, pg[0], bsize);
			NFT_PIPAPO_AVX2_LOAD(2, map[i_ul]);
			NFT_PIPAPO_AVX2_BUCKET_LOAD4(1, lt, 1, pg[1], bsize);
			NFT_PIPAPO_AVX2_NOMATCH_GOTO(2, nothing);
			NFT_PIPAPO_AVX2_AND(3, 0, 1);
			NFT_PIPAPO_AVX2_AND(4, 2, 3);
		}

		NFT_PIPAPO_AVX2_NOMATCH_GOTO(4, nomatch);
		NFT_PIPAPO_AVX2_STORE(map[i_ul], 4);

		b = nft_pipapo_avx2_refill(i_ul, &map[i_ul], fill, f->mt, last);
		if (last)
			return b;

		if (unlikely(ret == -1))
			ret = b / XSAVE_YMM_SIZE;

		continue;
nomatch:
		NFT_PIPAPO_AVX2_STORE(map[i_ul], 15);
nothing:
		;
	}

	return ret;
}

/**
 * nft_pipapo_avx2_lookup_4b_4() - AVX2-based lookup for 4 four-bit groups
 * @map:	Previous match result, used as initial bitmap
 * @fill:	Destination bitmap to be filled with current match result
 * @f:		Field, containing lookup and mapping tables
 * @offset:	Ignore buckets before the given index, no bits are filled there
 * @pkt:	Packet data, pointer to input nftables register
 * @first:	If this is the first field, don't source previous result
 * @last:	Last field: stop at the first match and return bit index
 *
 * See nft_pipapo_avx2_lookup_4b_2().
 *
 * This is used for 16-bit fields (i.e. ports).
 *
 * Return: -1 on no match, rule index of match if @last, otherwise first long
 * word index to be checked next (i.e. first filled word).
 */
static int nft_pipapo_avx2_lookup_4b_4(unsigned long *map, unsigned long *fill,
				       struct nft_pipapo_field *f, int offset,
				       const u8 *pkt, bool first, bool last)
{
	int i, ret = -1, m256_size = f->bsize / NFT_PIPAPO_LONGS_PER_M256, b;
	u8 pg[4] = { pkt[0] >> 4, pkt[0] & 0xf, pkt[1] >> 4, pkt[1] & 0xf };
	unsigned long *lt = f->lt, bsize = f->bsize;

	lt += offset * NFT_PIPAPO_LONGS_PER_M256;
	for (i = offset; i < m256_size; i++, lt += NFT_PIPAPO_LONGS_PER_M256) {
		int i_ul = i * NFT_PIPAPO_LONGS_PER_M256;

		if (first) {
			NFT_PIPAPO_AVX2_BUCKET_LOAD4(0, lt, 0, pg[0], bsize);
			NFT_PIPAPO_AVX2_BUCKET_LOAD4(1, lt, 1, pg[1], bsize);
			NFT_PIPAPO_AVX2_BUCKET_LOAD4(2, lt, 2, pg[2], bsize);
			NFT_PIPAPO_AVX2_BUCKET_LOAD4(3, lt, 3, pg[3], bsize);
			NFT_PIPAPO_AVX2_AND(4, 0, 1);
			NFT_PIPAPO_AVX2_AND(5, 2, 3);
			NFT_PIPAPO_AVX2_AND(7, 4, 5);
		} else {
			NFT_PIPAPO_AVX2_BUCKET_LOAD4(0, lt, 0, pg[0], bsize);

			NFT_PIPAPO_AVX2_LOAD(1, map[i_ul]);

			NFT_PIPAPO_AVX2_BUCKET_LOAD4(2, lt, 1, pg[1], bsize);
			NFT_PIPAPO_AVX2_BUCKET_LOAD4(3, lt, 2, pg[2], bsize);
			NFT_PIPAPO_AVX2_BUCKET_LOAD4(4, lt, 3, pg[3], bsize);
			NFT_PIPAPO_AVX2_AND(5, 0, 1);

			NFT_PIPAPO_AVX2_NOMATCH_GOTO(1, nothing);

			NFT_PIPAPO_AVX2_AND(6, 2, 3);
			NFT_PIPAPO_AVX2_AND(7, 4, 5);
			/* Stall */
			NFT_PIPAPO_AVX2_AND(7, 6, 7);
		}

		/* Stall */
		NFT_PIPAPO_AVX2_NOMATCH_GOTO(7, nomatch);
		NFT_PIPAPO_AVX2_STORE(map[i_ul], 7);

		b = nft_pipapo_avx2_refill(i_ul, &map[i_ul], fill, f->mt, last);
		if (last)
			return b;

		if (unlikely(ret == -1))
			ret = b / XSAVE_YMM_SIZE;

		continue;
nomatch:
		NFT_PIPAPO_AVX2_STORE(map[i_ul], 15);
nothing:
		;
	}

	return ret;
}

/**
 * nft_pipapo_avx2_lookup_4b_8() - AVX2-based lookup for 8 four-bit groups
 * @map:	Previous match result, used as initial bitmap
 * @fill:	Destination bitmap to be filled with current match result
 * @f:		Field, containing lookup and mapping tables
 * @offset:	Ignore buckets before the given index, no bits are filled there
 * @pkt:	Packet data, pointer to input nftables register
 * @first:	If this is the first field, don't source previous result
 * @last:	Last field: stop at the first match and return bit index
 *
 * See nft_pipapo_avx2_lookup_4b_2().
 *
 * This is used for 32-bit fields (i.e. IPv4 addresses).
 *
 * Return: -1 on no match, rule index of match if @last, otherwise first long
 * word index to be checked next (i.e. first filled word).
 */
static int nft_pipapo_avx2_lookup_4b_8(unsigned long *map, unsigned long *fill,
				       struct nft_pipapo_field *f, int offset,
				       const u8 *pkt, bool first, bool last)
{
	u8 pg[8] = {  pkt[0] >> 4,  pkt[0] & 0xf,  pkt[1] >> 4,  pkt[1] & 0xf,
		      pkt[2] >> 4,  pkt[2] & 0xf,  pkt[3] >> 4,  pkt[3] & 0xf,
		   };
	int i, ret = -1, m256_size = f->bsize / NFT_PIPAPO_LONGS_PER_M256, b;
	unsigned long *lt = f->lt, bsize = f->bsize;

	lt += offset * NFT_PIPAPO_LONGS_PER_M256;
	for (i = offset; i < m256_size; i++, lt += NFT_PIPAPO_LONGS_PER_M256) {
		int i_ul = i * NFT_PIPAPO_LONGS_PER_M256;

		if (first) {
			NFT_PIPAPO_AVX2_BUCKET_LOAD4(0,  lt, 0, pg[0], bsize);
			NFT_PIPAPO_AVX2_BUCKET_LOAD4(1,  lt, 1, pg[1], bsize);
			NFT_PIPAPO_AVX2_BUCKET_LOAD4(2,  lt, 2, pg[2], bsize);
			NFT_PIPAPO_AVX2_BUCKET_LOAD4(3,  lt, 3, pg[3], bsize);
			NFT_PIPAPO_AVX2_BUCKET_LOAD4(4,  lt, 4, pg[4], bsize);
			NFT_PIPAPO_AVX2_AND(5,   0,  1);
			NFT_PIPAPO_AVX2_BUCKET_LOAD4(6,  lt, 5, pg[5], bsize);
			NFT_PIPAPO_AVX2_BUCKET_LOAD4(7,  lt, 6, pg[6], bsize);
			NFT_PIPAPO_AVX2_AND(8,   2,  3);
			NFT_PIPAPO_AVX2_AND(9,   4,  5);
			NFT_PIPAPO_AVX2_BUCKET_LOAD4(10, lt, 7, pg[7], bsize);
			NFT_PIPAPO_AVX2_AND(11,  6,  7);
			NFT_PIPAPO_AVX2_AND(12,  8,  9);
			NFT_PIPAPO_AVX2_AND(13, 10, 11);

			/* Stall */
			NFT_PIPAPO_AVX2_AND(1,  12, 13);
		} else {
			NFT_PIPAPO_AVX2_BUCKET_LOAD4(0,  lt, 0, pg[0], bsize);
			NFT_PIPAPO_AVX2_LOAD(1, map[i_ul]);
			NFT_PIPAPO_AVX2_BUCKET_LOAD4(2,  lt, 1, pg[1], bsize);
			NFT_PIPAPO_AVX2_BUCKET_LOAD4(3,  lt, 2, pg[2], bsize);
			NFT_PIPAPO_AVX2_BUCKET_LOAD4(4,  lt, 3, pg[3], bsize);

			NFT_PIPAPO_AVX2_NOMATCH_GOTO(1, nothing);

			NFT_PIPAPO_AVX2_AND(5,   0,  1);
			NFT_PIPAPO_AVX2_BUCKET_LOAD4(6,  lt, 4, pg[4], bsize);
			NFT_PIPAPO_AVX2_BUCKET_LOAD4(7,  lt, 5, pg[5], bsize);
			NFT_PIPAPO_AVX2_AND(8,   2,  3);
			NFT_PIPAPO_AVX2_BUCKET_LOAD4(9,  lt, 6, pg[6], bsize);
			NFT_PIPAPO_AVX2_AND(10,  4,  5);
			NFT_PIPAPO_AVX2_BUCKET_LOAD4(11, lt, 7, pg[7], bsize);
			NFT_PIPAPO_AVX2_AND(12,  6,  7);
			NFT_PIPAPO_AVX2_AND(13,  8,  9);
			NFT_PIPAPO_AVX2_AND(14, 10, 11);

			/* Stall */
			NFT_PIPAPO_AVX2_AND(1,  12, 13);
			NFT_PIPAPO_AVX2_AND(1,   1, 14);
		}

		NFT_PIPAPO_AVX2_NOMATCH_GOTO(1, nomatch);
		NFT_PIPAPO_AVX2_STORE(map[i_ul], 1);

		b = nft_pipapo_avx2_refill(i_ul, &map[i_ul], fill, f->mt, last);
		if (last)
			return b;

		if (unlikely(ret == -1))
			ret = b / XSAVE_YMM_SIZE;

		continue;

nomatch:
		NFT_PIPAPO_AVX2_STORE(map[i_ul], 15);
nothing:
		;
	}

	return ret;
}

/**
 * nft_pipapo_avx2_lookup_4b_12() - AVX2-based lookup for 12 four-bit groups
 * @map:	Previous match result, used as initial bitmap
 * @fill:	Destination bitmap to be filled with current match result
 * @f:		Field, containing lookup and mapping tables
 * @offset:	Ignore buckets before the given index, no bits are filled there
 * @pkt:	Packet data, pointer to input nftables register
 * @first:	If this is the first field, don't source previous result
 * @last:	Last field: stop at the first match and return bit index
 *
 * See nft_pipapo_avx2_lookup_4b_2().
 *
 * This is used for 48-bit fields (i.e. MAC addresses/EUI-48).
 *
 * Return: -1 on no match, rule index of match if @last, otherwise first long
 * word index to be checked next (i.e. first filled word).
 */
static int nft_pipapo_avx2_lookup_4b_12(unsigned long *map, unsigned long *fill,
				        struct nft_pipapo_field *f, int offset,
				        const u8 *pkt, bool first, bool last)
{
	u8 pg[12] = {  pkt[0] >> 4,  pkt[0] & 0xf,  pkt[1] >> 4,  pkt[1] & 0xf,
		       pkt[2] >> 4,  pkt[2] & 0xf,  pkt[3] >> 4,  pkt[3] & 0xf,
		       pkt[4] >> 4,  pkt[4] & 0xf,  pkt[5] >> 4,  pkt[5] & 0xf,
		    };
	int i, ret = -1, m256_size = f->bsize / NFT_PIPAPO_LONGS_PER_M256, b;
	unsigned long *lt = f->lt, bsize = f->bsize;

	lt += offset * NFT_PIPAPO_LONGS_PER_M256;
	for (i = offset; i < m256_size; i++, lt += NFT_PIPAPO_LONGS_PER_M256) {
		int i_ul = i * NFT_PIPAPO_LONGS_PER_M256;

		if (!first)
			NFT_PIPAPO_AVX2_LOAD(0, map[i_ul]);

		NFT_PIPAPO_AVX2_BUCKET_LOAD4(1,  lt,  0,  pg[0], bsize);
		NFT_PIPAPO_AVX2_BUCKET_LOAD4(2,  lt,  1,  pg[1], bsize);
		NFT_PIPAPO_AVX2_BUCKET_LOAD4(3,  lt,  2,  pg[2], bsize);

		if (!first) {
			NFT_PIPAPO_AVX2_NOMATCH_GOTO(0, nothing);
			NFT_PIPAPO_AVX2_AND(1, 1, 0);
		}

		NFT_PIPAPO_AVX2_BUCKET_LOAD4(4,  lt,  3,  pg[3], bsize);
		NFT_PIPAPO_AVX2_BUCKET_LOAD4(5,  lt,  4,  pg[4], bsize);
		NFT_PIPAPO_AVX2_AND(6,   2,  3);
		NFT_PIPAPO_AVX2_BUCKET_LOAD4(7,  lt,  5,  pg[5], bsize);
		NFT_PIPAPO_AVX2_BUCKET_LOAD4(8,  lt,  6,  pg[6], bsize);
		NFT_PIPAPO_AVX2_AND(9,   1,  4);
		NFT_PIPAPO_AVX2_BUCKET_LOAD4(10, lt,  7,  pg[7], bsize);
		NFT_PIPAPO_AVX2_AND(11,  5,  6);
		NFT_PIPAPO_AVX2_BUCKET_LOAD4(12, lt,  8,  pg[8], bsize);
		NFT_PIPAPO_AVX2_AND(13,  7,  8);
		NFT_PIPAPO_AVX2_BUCKET_LOAD4(14, lt,  9,  pg[9], bsize);

		NFT_PIPAPO_AVX2_AND(0,   9, 10);
		NFT_PIPAPO_AVX2_BUCKET_LOAD4(1,  lt, 10,  pg[10], bsize);
		NFT_PIPAPO_AVX2_AND(2,  11, 12);
		NFT_PIPAPO_AVX2_BUCKET_LOAD4(3,  lt, 11,  pg[11], bsize);
		NFT_PIPAPO_AVX2_AND(4,  13, 14);
		NFT_PIPAPO_AVX2_AND(5,   0,  1);

		NFT_PIPAPO_AVX2_AND(6,   2,  3);

		/* Stalls */
		NFT_PIPAPO_AVX2_AND(7,   4,  5);
		NFT_PIPAPO_AVX2_AND(8,   6,  7);

		NFT_PIPAPO_AVX2_NOMATCH_GOTO(8, nomatch);
		NFT_PIPAPO_AVX2_STORE(map[i_ul], 8);

		b = nft_pipapo_avx2_refill(i_ul, &map[i_ul], fill, f->mt, last);
		if (last)
			return b;

		if (unlikely(ret == -1))
			ret = b / XSAVE_YMM_SIZE;

		continue;
nomatch:
		NFT_PIPAPO_AVX2_STORE(map[i_ul], 15);
nothing:
		;
	}

	return ret;
}

/**
 * nft_pipapo_avx2_lookup_4b_32() - AVX2-based lookup for 32 four-bit groups
 * @map:	Previous match result, used as initial bitmap
 * @fill:	Destination bitmap to be filled with current match result
 * @f:		Field, containing lookup and mapping tables
 * @offset:	Ignore buckets before the given index, no bits are filled there
 * @pkt:	Packet data, pointer to input nftables register
 * @first:	If this is the first field, don't source previous result
 * @last:	Last field: stop at the first match and return bit index
 *
 * See nft_pipapo_avx2_lookup_4b_2().
 *
 * This is used for 128-bit fields (i.e. IPv6 addresses).
 *
 * Return: -1 on no match, rule index of match if @last, otherwise first long
 * word index to be checked next (i.e. first filled word).
 */
static int nft_pipapo_avx2_lookup_4b_32(unsigned long *map, unsigned long *fill,
					struct nft_pipapo_field *f, int offset,
					const u8 *pkt, bool first, bool last)
{
	u8 pg[32] = {  pkt[0] >> 4,  pkt[0] & 0xf,  pkt[1] >> 4,  pkt[1] & 0xf,
		       pkt[2] >> 4,  pkt[2] & 0xf,  pkt[3] >> 4,  pkt[3] & 0xf,
		       pkt[4] >> 4,  pkt[4] & 0xf,  pkt[5] >> 4,  pkt[5] & 0xf,
		       pkt[6] >> 4,  pkt[6] & 0xf,  pkt[7] >> 4,  pkt[7] & 0xf,
		       pkt[8] >> 4,  pkt[8] & 0xf,  pkt[9] >> 4,  pkt[9] & 0xf,
		      pkt[10] >> 4, pkt[10] & 0xf, pkt[11] >> 4, pkt[11] & 0xf,
		      pkt[12] >> 4, pkt[12] & 0xf, pkt[13] >> 4, pkt[13] & 0xf,
		      pkt[14] >> 4, pkt[14] & 0xf, pkt[15] >> 4, pkt[15] & 0xf,
		    };
	int i, ret = -1, m256_size = f->bsize / NFT_PIPAPO_LONGS_PER_M256, b;
	unsigned long *lt = f->lt, bsize = f->bsize;

	lt += offset * NFT_PIPAPO_LONGS_PER_M256;
	for (i = offset; i < m256_size; i++, lt += NFT_PIPAPO_LONGS_PER_M256) {
		int i_ul = i * NFT_PIPAPO_LONGS_PER_M256;

		if (!first)
			NFT_PIPAPO_AVX2_LOAD(0, map[i_ul]);

		NFT_PIPAPO_AVX2_BUCKET_LOAD4(1,  lt,  0,  pg[0], bsize);
		NFT_PIPAPO_AVX2_BUCKET_LOAD4(2,  lt,  1,  pg[1], bsize);
		NFT_PIPAPO_AVX2_BUCKET_LOAD4(3,  lt,  2,  pg[2], bsize);
		NFT_PIPAPO_AVX2_BUCKET_LOAD4(4,  lt,  3,  pg[3], bsize);
		if (!first) {
			NFT_PIPAPO_AVX2_NOMATCH_GOTO(0, nothing);
			NFT_PIPAPO_AVX2_AND(1, 1, 0);
		}

		NFT_PIPAPO_AVX2_AND(5,   2,  3);
		NFT_PIPAPO_AVX2_BUCKET_LOAD4(6,  lt,  4,  pg[4], bsize);
		NFT_PIPAPO_AVX2_BUCKET_LOAD4(7,  lt,  5,  pg[5], bsize);
		NFT_PIPAPO_AVX2_AND(8,   1,  4);
		NFT_PIPAPO_AVX2_BUCKET_LOAD4(9,  lt,  6,  pg[6], bsize);
		NFT_PIPAPO_AVX2_AND(10,  5,  6);
		NFT_PIPAPO_AVX2_BUCKET_LOAD4(11, lt,  7,  pg[7], bsize);
		NFT_PIPAPO_AVX2_AND(12,  7,  8);
		NFT_PIPAPO_AVX2_BUCKET_LOAD4(13, lt,  8,  pg[8], bsize);
		NFT_PIPAPO_AVX2_AND(14,  9, 10);

		NFT_PIPAPO_AVX2_BUCKET_LOAD4(0,  lt,  9,  pg[9], bsize);
		NFT_PIPAPO_AVX2_AND(1,  11, 12);
		NFT_PIPAPO_AVX2_BUCKET_LOAD4(2,  lt, 10, pg[10], bsize);
		NFT_PIPAPO_AVX2_BUCKET_LOAD4(3,  lt, 11, pg[11], bsize);
		NFT_PIPAPO_AVX2_AND(4,  13, 14);
		NFT_PIPAPO_AVX2_BUCKET_LOAD4(5,  lt, 12, pg[12], bsize);
		NFT_PIPAPO_AVX2_BUCKET_LOAD4(6,  lt, 13, pg[13], bsize);
		NFT_PIPAPO_AVX2_AND(7,   0,  1);
		NFT_PIPAPO_AVX2_BUCKET_LOAD4(8,  lt, 14, pg[14], bsize);
		NFT_PIPAPO_AVX2_AND(9,   2,  3);
		NFT_PIPAPO_AVX2_BUCKET_LOAD4(10, lt, 15, pg[15], bsize);
		NFT_PIPAPO_AVX2_AND(11,  4,  5);
		NFT_PIPAPO_AVX2_BUCKET_LOAD4(12, lt, 16, pg[16], bsize);
		NFT_PIPAPO_AVX2_AND(13,  6,  7);
		NFT_PIPAPO_AVX2_BUCKET_LOAD4(14, lt, 17, pg[17], bsize);

		NFT_PIPAPO_AVX2_AND(0,   8,  9);
		NFT_PIPAPO_AVX2_BUCKET_LOAD4(1,  lt, 18, pg[18], bsize);
		NFT_PIPAPO_AVX2_AND(2,  10, 11);
		NFT_PIPAPO_AVX2_BUCKET_LOAD4(3,  lt, 19, pg[19], bsize);
		NFT_PIPAPO_AVX2_AND(4,  12, 13);
		NFT_PIPAPO_AVX2_BUCKET_LOAD4(5,  lt, 20, pg[20], bsize);
		NFT_PIPAPO_AVX2_AND(6,  14,  0);
		NFT_PIPAPO_AVX2_AND(7,   1,  2);
		NFT_PIPAPO_AVX2_BUCKET_LOAD4(8,  lt, 21, pg[21], bsize);
		NFT_PIPAPO_AVX2_AND(9,   3,  4);
		NFT_PIPAPO_AVX2_BUCKET_LOAD4(10, lt, 22, pg[22], bsize);
		NFT_PIPAPO_AVX2_AND(11,  5,  6);
		NFT_PIPAPO_AVX2_BUCKET_LOAD4(12, lt, 23, pg[23], bsize);
		NFT_PIPAPO_AVX2_AND(13,  7,  8);

		NFT_PIPAPO_AVX2_BUCKET_LOAD4(14, lt, 24, pg[24], bsize);
		NFT_PIPAPO_AVX2_BUCKET_LOAD4(0,  lt, 25, pg[25], bsize);
		NFT_PIPAPO_AVX2_AND(1,   9, 10);
		NFT_PIPAPO_AVX2_AND(2,  11, 12);
		NFT_PIPAPO_AVX2_BUCKET_LOAD4(3,  lt, 26, pg[26], bsize);
		NFT_PIPAPO_AVX2_AND(4,  13, 14);
		NFT_PIPAPO_AVX2_BUCKET_LOAD4(5,  lt, 27, pg[27], bsize);
		NFT_PIPAPO_AVX2_AND(6,   0,  1);
		NFT_PIPAPO_AVX2_BUCKET_LOAD4(7,  lt, 28, pg[28], bsize);
		NFT_PIPAPO_AVX2_BUCKET_LOAD4(8,  lt, 29, pg[29], bsize);
		NFT_PIPAPO_AVX2_AND(9,   2,  3);
		NFT_PIPAPO_AVX2_BUCKET_LOAD4(10, lt, 30, pg[30], bsize);
		NFT_PIPAPO_AVX2_AND(11,  4,  5);
		NFT_PIPAPO_AVX2_BUCKET_LOAD4(12, lt, 31, pg[31], bsize);

		NFT_PIPAPO_AVX2_AND(0,   6,  7);
		NFT_PIPAPO_AVX2_AND(1,   8,  9);
		NFT_PIPAPO_AVX2_AND(2,  10, 11);
		NFT_PIPAPO_AVX2_AND(3,  12,  0);

		/* Stalls */
		NFT_PIPAPO_AVX2_AND(4,   1,  2);
		NFT_PIPAPO_AVX2_AND(5,   3,  4);

		NFT_PIPAPO_AVX2_NOMATCH_GOTO(5, nomatch);
		NFT_PIPAPO_AVX2_STORE(map[i_ul], 5);

		b = nft_pipapo_avx2_refill(i_ul, &map[i_ul], fill, f->mt, last);
		if (last)
			return b;

		if (unlikely(ret == -1))
			ret = b / XSAVE_YMM_SIZE;

		continue;
nomatch:
		NFT_PIPAPO_AVX2_STORE(map[i_ul], 15);
nothing:
		;
	}

	return ret;
}

/**
 * nft_pipapo_avx2_lookup_8b_1() - AVX2-based lookup for one eight-bit group
 * @map:	Previous match result, used as initial bitmap
 * @fill:	Destination bitmap to be filled with current match result
 * @f:		Field, containing lookup and mapping tables
 * @offset:	Ignore buckets before the given index, no bits are filled there
 * @pkt:	Packet data, pointer to input nftables register
 * @first:	If this is the first field, don't source previous result
 * @last:	Last field: stop at the first match and return bit index
 *
 * See nft_pipapo_avx2_lookup_4b_2().
 *
 * This is used for 8-bit fields (i.e. protocol numbers).
 *
 * Return: -1 on no match, rule index of match if @last, otherwise first long
 * word index to be checked next (i.e. first filled word).
 */
static int nft_pipapo_avx2_lookup_8b_1(unsigned long *map, unsigned long *fill,
				       struct nft_pipapo_field *f, int offset,
				       const u8 *pkt, bool first, bool last)
{
	int i, ret = -1, m256_size = f->bsize / NFT_PIPAPO_LONGS_PER_M256, b;
	unsigned long *lt = f->lt, bsize = f->bsize;

	lt += offset * NFT_PIPAPO_LONGS_PER_M256;
	for (i = offset; i < m256_size; i++, lt += NFT_PIPAPO_LONGS_PER_M256) {
		int i_ul = i * NFT_PIPAPO_LONGS_PER_M256;

		if (first) {
			NFT_PIPAPO_AVX2_BUCKET_LOAD8(2, lt, 0, pkt[0], bsize);
		} else {
			NFT_PIPAPO_AVX2_BUCKET_LOAD8(0, lt, 0, pkt[0], bsize);
			NFT_PIPAPO_AVX2_LOAD(1, map[i_ul]);
			NFT_PIPAPO_AVX2_AND(2, 0, 1);
			NFT_PIPAPO_AVX2_NOMATCH_GOTO(1, nothing);
		}

		NFT_PIPAPO_AVX2_NOMATCH_GOTO(2, nomatch);
		NFT_PIPAPO_AVX2_STORE(map[i_ul], 2);

		b = nft_pipapo_avx2_refill(i_ul, &map[i_ul], fill, f->mt, last);
		if (last)
			return b;

		if (unlikely(ret == -1))
			ret = b / XSAVE_YMM_SIZE;

		continue;
nomatch:
		NFT_PIPAPO_AVX2_STORE(map[i_ul], 15);
nothing:
		;
	}

	return ret;
}

/**
 * nft_pipapo_avx2_lookup_8b_2() - AVX2-based lookup for 2 eight-bit groups
 * @map:	Previous match result, used as initial bitmap
 * @fill:	Destination bitmap to be filled with current match result
 * @f:		Field, containing lookup and mapping tables
 * @offset:	Ignore buckets before the given index, no bits are filled there
 * @pkt:	Packet data, pointer to input nftables register
 * @first:	If this is the first field, don't source previous result
 * @last:	Last field: stop at the first match and return bit index
 *
 * See nft_pipapo_avx2_lookup_4b_2().
 *
 * This is used for 16-bit fields (i.e. ports).
 *
 * Return: -1 on no match, rule index of match if @last, otherwise first long
 * word index to be checked next (i.e. first filled word).
 */
static int nft_pipapo_avx2_lookup_8b_2(unsigned long *map, unsigned long *fill,
				       struct nft_pipapo_field *f, int offset,
				       const u8 *pkt, bool first, bool last)
{
	int i, ret = -1, m256_size = f->bsize / NFT_PIPAPO_LONGS_PER_M256, b;
	unsigned long *lt = f->lt, bsize = f->bsize;

	lt += offset * NFT_PIPAPO_LONGS_PER_M256;
	for (i = offset; i < m256_size; i++, lt += NFT_PIPAPO_LONGS_PER_M256) {
		int i_ul = i * NFT_PIPAPO_LONGS_PER_M256;

		if (first) {
			NFT_PIPAPO_AVX2_BUCKET_LOAD8(0, lt, 0, pkt[0], bsize);
			NFT_PIPAPO_AVX2_BUCKET_LOAD8(1, lt, 1, pkt[1], bsize);
			NFT_PIPAPO_AVX2_AND(4, 0, 1);
		} else {
			NFT_PIPAPO_AVX2_LOAD(0, map[i_ul]);
			NFT_PIPAPO_AVX2_BUCKET_LOAD8(1, lt, 0, pkt[0], bsize);
			NFT_PIPAPO_AVX2_BUCKET_LOAD8(2, lt, 1, pkt[1], bsize);

			/* Stall */
			NFT_PIPAPO_AVX2_AND(3, 0, 1);
			NFT_PIPAPO_AVX2_NOMATCH_GOTO(0, nothing);
			NFT_PIPAPO_AVX2_AND(4, 3, 2);
		}

		/* Stall */
		NFT_PIPAPO_AVX2_NOMATCH_GOTO(4, nomatch);
		NFT_PIPAPO_AVX2_STORE(map[i_ul], 4);

		b = nft_pipapo_avx2_refill(i_ul, &map[i_ul], fill, f->mt, last);
		if (last)
			return b;

		if (unlikely(ret == -1))
			ret = b / XSAVE_YMM_SIZE;

		continue;
nomatch:
		NFT_PIPAPO_AVX2_STORE(map[i_ul], 15);
nothing:
		;
	}

	return ret;
}

/**
 * nft_pipapo_avx2_lookup_8b_4() - AVX2-based lookup for 4 eight-bit groups
 * @map:	Previous match result, used as initial bitmap
 * @fill:	Destination bitmap to be filled with current match result
 * @f:		Field, containing lookup and mapping tables
 * @offset:	Ignore buckets before the given index, no bits are filled there
 * @pkt:	Packet data, pointer to input nftables register
 * @first:	If this is the first field, don't source previous result
 * @last:	Last field: stop at the first match and return bit index
 *
 * See nft_pipapo_avx2_lookup_4b_2().
 *
 * This is used for 32-bit fields (i.e. IPv4 addresses).
 *
 * Return: -1 on no match, rule index of match if @last, otherwise first long
 * word index to be checked next (i.e. first filled word).
 */
static int nft_pipapo_avx2_lookup_8b_4(unsigned long *map, unsigned long *fill,
				       struct nft_pipapo_field *f, int offset,
				       const u8 *pkt, bool first, bool last)
{
	int i, ret = -1, m256_size = f->bsize / NFT_PIPAPO_LONGS_PER_M256, b;
	unsigned long *lt = f->lt, bsize = f->bsize;

	lt += offset * NFT_PIPAPO_LONGS_PER_M256;
	for (i = offset; i < m256_size; i++, lt += NFT_PIPAPO_LONGS_PER_M256) {
		int i_ul = i * NFT_PIPAPO_LONGS_PER_M256;

		if (first) {
			NFT_PIPAPO_AVX2_BUCKET_LOAD8(0,  lt, 0, pkt[0], bsize);
			NFT_PIPAPO_AVX2_BUCKET_LOAD8(1,  lt, 1, pkt[1], bsize);
			NFT_PIPAPO_AVX2_BUCKET_LOAD8(2,  lt, 2, pkt[2], bsize);
			NFT_PIPAPO_AVX2_BUCKET_LOAD8(3,  lt, 3, pkt[3], bsize);

			/* Stall */
			NFT_PIPAPO_AVX2_AND(4, 0, 1);
			NFT_PIPAPO_AVX2_AND(5, 2, 3);
			NFT_PIPAPO_AVX2_AND(0, 4, 5);
		} else {
			NFT_PIPAPO_AVX2_BUCKET_LOAD8(0,  lt, 0, pkt[0], bsize);
			NFT_PIPAPO_AVX2_LOAD(1, map[i_ul]);
			NFT_PIPAPO_AVX2_BUCKET_LOAD8(2,  lt, 1, pkt[1], bsize);
			NFT_PIPAPO_AVX2_BUCKET_LOAD8(3,  lt, 2, pkt[2], bsize);
			NFT_PIPAPO_AVX2_BUCKET_LOAD8(4,  lt, 3, pkt[3], bsize);

			NFT_PIPAPO_AVX2_AND(5, 0, 1);
			NFT_PIPAPO_AVX2_NOMATCH_GOTO(1, nothing);
			NFT_PIPAPO_AVX2_AND(6, 2, 3);

			/* Stall */
			NFT_PIPAPO_AVX2_AND(7, 4, 5);
			NFT_PIPAPO_AVX2_AND(0, 6, 7);
		}

		NFT_PIPAPO_AVX2_NOMATCH_GOTO(0, nomatch);
		NFT_PIPAPO_AVX2_STORE(map[i_ul], 0);

		b = nft_pipapo_avx2_refill(i_ul, &map[i_ul], fill, f->mt, last);
		if (last)
			return b;

		if (unlikely(ret == -1))
			ret = b / XSAVE_YMM_SIZE;

		continue;

nomatch:
		NFT_PIPAPO_AVX2_STORE(map[i_ul], 15);
nothing:
		;
	}

	return ret;
}

/**
 * nft_pipapo_avx2_lookup_8b_6() - AVX2-based lookup for 6 eight-bit groups
 * @map:	Previous match result, used as initial bitmap
 * @fill:	Destination bitmap to be filled with current match result
 * @f:		Field, containing lookup and mapping tables
 * @offset:	Ignore buckets before the given index, no bits are filled there
 * @pkt:	Packet data, pointer to input nftables register
 * @first:	If this is the first field, don't source previous result
 * @last:	Last field: stop at the first match and return bit index
 *
 * See nft_pipapo_avx2_lookup_4b_2().
 *
 * This is used for 48-bit fields (i.e. MAC addresses/EUI-48).
 *
 * Return: -1 on no match, rule index of match if @last, otherwise first long
 * word index to be checked next (i.e. first filled word).
 */
static int nft_pipapo_avx2_lookup_8b_6(unsigned long *map, unsigned long *fill,
				       struct nft_pipapo_field *f, int offset,
				       const u8 *pkt, bool first, bool last)
{
	int i, ret = -1, m256_size = f->bsize / NFT_PIPAPO_LONGS_PER_M256, b;
	unsigned long *lt = f->lt, bsize = f->bsize;

	lt += offset * NFT_PIPAPO_LONGS_PER_M256;
	for (i = offset; i < m256_size; i++, lt += NFT_PIPAPO_LONGS_PER_M256) {
		int i_ul = i * NFT_PIPAPO_LONGS_PER_M256;

		if (first) {
			NFT_PIPAPO_AVX2_BUCKET_LOAD8(0,  lt, 0, pkt[0], bsize);
			NFT_PIPAPO_AVX2_BUCKET_LOAD8(1,  lt, 1, pkt[1], bsize);
			NFT_PIPAPO_AVX2_BUCKET_LOAD8(2,  lt, 2, pkt[2], bsize);
			NFT_PIPAPO_AVX2_BUCKET_LOAD8(3,  lt, 3, pkt[3], bsize);
			NFT_PIPAPO_AVX2_BUCKET_LOAD8(4,  lt, 4, pkt[4], bsize);

			NFT_PIPAPO_AVX2_AND(5, 0, 1);
			NFT_PIPAPO_AVX2_BUCKET_LOAD8(6,  lt, 5, pkt[5], bsize);
			NFT_PIPAPO_AVX2_AND(7, 2, 3);

			/* Stall */
			NFT_PIPAPO_AVX2_AND(0, 4, 5);
			NFT_PIPAPO_AVX2_AND(1, 6, 7);
			NFT_PIPAPO_AVX2_AND(4, 0, 1);
		} else {
			NFT_PIPAPO_AVX2_BUCKET_LOAD8(0,  lt, 0, pkt[0], bsize);
			NFT_PIPAPO_AVX2_LOAD(1, map[i_ul]);
			NFT_PIPAPO_AVX2_BUCKET_LOAD8(2,  lt, 1, pkt[1], bsize);
			NFT_PIPAPO_AVX2_BUCKET_LOAD8(3,  lt, 2, pkt[2], bsize);
			NFT_PIPAPO_AVX2_BUCKET_LOAD8(4,  lt, 3, pkt[3], bsize);

			NFT_PIPAPO_AVX2_AND(5, 0, 1);
			NFT_PIPAPO_AVX2_NOMATCH_GOTO(1, nothing);

			NFT_PIPAPO_AVX2_AND(6, 2, 3);
			NFT_PIPAPO_AVX2_BUCKET_LOAD8(7,  lt, 4, pkt[4], bsize);
			NFT_PIPAPO_AVX2_AND(0, 4, 5);
			NFT_PIPAPO_AVX2_BUCKET_LOAD8(1,  lt, 5, pkt[5], bsize);
			NFT_PIPAPO_AVX2_AND(2, 6, 7);

			/* Stall */
			NFT_PIPAPO_AVX2_AND(3, 0, 1);
			NFT_PIPAPO_AVX2_AND(4, 2, 3);
		}

		NFT_PIPAPO_AVX2_NOMATCH_GOTO(4, nomatch);
		NFT_PIPAPO_AVX2_STORE(map[i_ul], 4);

		b = nft_pipapo_avx2_refill(i_ul, &map[i_ul], fill, f->mt, last);
		if (last)
			return b;

		if (unlikely(ret == -1))
			ret = b / XSAVE_YMM_SIZE;

		continue;

nomatch:
		NFT_PIPAPO_AVX2_STORE(map[i_ul], 15);
nothing:
		;
	}

	return ret;
}

/**
 * nft_pipapo_avx2_lookup_8b_16() - AVX2-based lookup for 16 eight-bit groups
 * @map:	Previous match result, used as initial bitmap
 * @fill:	Destination bitmap to be filled with current match result
 * @f:		Field, containing lookup and mapping tables
 * @offset:	Ignore buckets before the given index, no bits are filled there
 * @pkt:	Packet data, pointer to input nftables register
 * @first:	If this is the first field, don't source previous result
 * @last:	Last field: stop at the first match and return bit index
 *
 * See nft_pipapo_avx2_lookup_4b_2().
 *
 * This is used for 128-bit fields (i.e. IPv6 addresses).
 *
 * Return: -1 on no match, rule index of match if @last, otherwise first long
 * word index to be checked next (i.e. first filled word).
 */
static int nft_pipapo_avx2_lookup_8b_16(unsigned long *map, unsigned long *fill,
					struct nft_pipapo_field *f, int offset,
					const u8 *pkt, bool first, bool last)
{
	int i, ret = -1, m256_size = f->bsize / NFT_PIPAPO_LONGS_PER_M256, b;
	unsigned long *lt = f->lt, bsize = f->bsize;

	lt += offset * NFT_PIPAPO_LONGS_PER_M256;
	for (i = offset; i < m256_size; i++, lt += NFT_PIPAPO_LONGS_PER_M256) {
		int i_ul = i * NFT_PIPAPO_LONGS_PER_M256;

		if (!first)
			NFT_PIPAPO_AVX2_LOAD(0, map[i_ul]);

		NFT_PIPAPO_AVX2_BUCKET_LOAD8(1, lt,  0,  pkt[0], bsize);
		NFT_PIPAPO_AVX2_BUCKET_LOAD8(2, lt,  1,  pkt[1], bsize);
		NFT_PIPAPO_AVX2_BUCKET_LOAD8(3, lt,  2,  pkt[2], bsize);
		if (!first) {
			NFT_PIPAPO_AVX2_NOMATCH_GOTO(0, nothing);
			NFT_PIPAPO_AVX2_AND(1, 1, 0);
		}
		NFT_PIPAPO_AVX2_BUCKET_LOAD8(4, lt,  3,  pkt[3], bsize);

		NFT_PIPAPO_AVX2_BUCKET_LOAD8(5, lt,  4,  pkt[4], bsize);
		NFT_PIPAPO_AVX2_AND(6, 1, 2);
		NFT_PIPAPO_AVX2_BUCKET_LOAD8(7, lt,  5,  pkt[5], bsize);
		NFT_PIPAPO_AVX2_AND(0, 3, 4);
		NFT_PIPAPO_AVX2_BUCKET_LOAD8(1, lt,  6,  pkt[6], bsize);

		NFT_PIPAPO_AVX2_BUCKET_LOAD8(2, lt,  7,  pkt[7], bsize);
		NFT_PIPAPO_AVX2_AND(3, 5, 6);
		NFT_PIPAPO_AVX2_AND(4, 0, 1);
		NFT_PIPAPO_AVX2_BUCKET_LOAD8(5, lt,  8,  pkt[8], bsize);

		NFT_PIPAPO_AVX2_AND(6, 2, 3);
		NFT_PIPAPO_AVX2_BUCKET_LOAD8(7, lt,  9,  pkt[9], bsize);
		NFT_PIPAPO_AVX2_AND(0, 4, 5);
		NFT_PIPAPO_AVX2_BUCKET_LOAD8(1, lt, 10, pkt[10], bsize);
		NFT_PIPAPO_AVX2_AND(2, 6, 7);
		NFT_PIPAPO_AVX2_BUCKET_LOAD8(3, lt, 11, pkt[11], bsize);
		NFT_PIPAPO_AVX2_AND(4, 0, 1);
		NFT_PIPAPO_AVX2_BUCKET_LOAD8(5, lt, 12, pkt[12], bsize);
		NFT_PIPAPO_AVX2_AND(6, 2, 3);
		NFT_PIPAPO_AVX2_BUCKET_LOAD8(7, lt, 13, pkt[13], bsize);
		NFT_PIPAPO_AVX2_AND(0, 4, 5);
		NFT_PIPAPO_AVX2_BUCKET_LOAD8(1, lt, 14, pkt[14], bsize);
		NFT_PIPAPO_AVX2_AND(2, 6, 7);
		NFT_PIPAPO_AVX2_BUCKET_LOAD8(3, lt, 15, pkt[15], bsize);
		NFT_PIPAPO_AVX2_AND(4, 0, 1);

		/* Stall */
		NFT_PIPAPO_AVX2_AND(5, 2, 3);
		NFT_PIPAPO_AVX2_AND(6, 4, 5);

		NFT_PIPAPO_AVX2_NOMATCH_GOTO(6, nomatch);
		NFT_PIPAPO_AVX2_STORE(map[i_ul], 6);

		b = nft_pipapo_avx2_refill(i_ul, &map[i_ul], fill, f->mt, last);
		if (last)
			return b;

		if (unlikely(ret == -1))
			ret = b / XSAVE_YMM_SIZE;

		continue;

nomatch:
		NFT_PIPAPO_AVX2_STORE(map[i_ul], 15);
nothing:
		;
	}

	return ret;
}

/**
 * nft_pipapo_avx2_lookup_slow() - Fallback function for uncommon field sizes
 * @map:	Previous match result, used as initial bitmap
 * @fill:	Destination bitmap to be filled with current match result
 * @f:		Field, containing lookup and mapping tables
 * @offset:	Ignore buckets before the given index, no bits are filled there
 * @pkt:	Packet data, pointer to input nftables register
 * @first:	If this is the first field, don't source previous result
 * @last:	Last field: stop at the first match and return bit index
 *
 * This function should never be called, but is provided for the case the field
 * size doesn't match any of the known data types. Matching rate is
 * substantially lower than AVX2 routines.
 *
 * Return: -1 on no match, rule index of match if @last, otherwise first long
 * word index to be checked next (i.e. first filled word).
 */
static int nft_pipapo_avx2_lookup_slow(unsigned long *map, unsigned long *fill,
					struct nft_pipapo_field *f, int offset,
					const u8 *pkt, bool first, bool last)
{
	unsigned long bsize = f->bsize;
	int i, ret = -1, b;

	if (first)
		memset(map, 0xff, bsize * sizeof(*map));

	for (i = offset; i < bsize; i++) {
		if (f->bb == 8)
			pipapo_and_field_buckets_8bit(f, map, pkt);
		else
			pipapo_and_field_buckets_4bit(f, map, pkt);
		NFT_PIPAPO_GROUP_BITS_ARE_8_OR_4;

		b = pipapo_refill(map, bsize, f->rules, fill, f->mt, last);

		if (last)
			return b;

		if (ret == -1)
			ret = b / XSAVE_YMM_SIZE;
	}

	return ret;
}

/**
 * nft_pipapo_avx2_estimate() - Set size, space and lookup complexity
 * @desc:	Set description, element count and field description used
 * @features:	Flags: NFT_SET_INTERVAL needs to be there
 * @est:	Storage for estimation data
 *
 * Return: true if set is compatible and AVX2 available, false otherwise.
 */
bool nft_pipapo_avx2_estimate(const struct nft_set_desc *desc, u32 features,
			      struct nft_set_estimate *est)
{
	if (!(features & NFT_SET_INTERVAL) ||
	    desc->field_count < NFT_PIPAPO_MIN_FIELDS)
		return false;

	if (!boot_cpu_has(X86_FEATURE_AVX2) || !boot_cpu_has(X86_FEATURE_AVX))
		return false;

	est->size = pipapo_estimate_size(desc);
	if (!est->size)
		return false;

	est->lookup = NFT_SET_CLASS_O_LOG_N;

	est->space = NFT_SET_CLASS_O_N;

	return true;
}

/**
 * nft_pipapo_avx2_lookup() - Lookup function for AVX2 implementation
 * @net:	Network namespace
 * @set:	nftables API set representation
 * @key:	nftables API element representation containing key data
 * @ext:	nftables API extension pointer, filled with matching reference
 *
 * For more details, see DOC: Theory of Operation in nft_set_pipapo.c.
 *
 * This implementation exploits the repetitive characteristic of the algorithm
 * to provide a fast, vectorised version using the AVX2 SIMD instruction set.
 *
 * Return: true on match, false otherwise.
 */
bool nft_pipapo_avx2_lookup(const struct net *net, const struct nft_set *set,
			    const u32 *key, const struct nft_set_ext **ext)
{
	struct nft_pipapo *priv = nft_set_priv(set);
	unsigned long *res, *fill, *scratch;
	u8 genmask = nft_genmask_cur(net);
	const u8 *rp = (const u8 *)key;
	struct nft_pipapo_match *m;
	struct nft_pipapo_field *f;
	bool map_index;
	int i, ret = 0;

	if (unlikely(!irq_fpu_usable()))
		return nft_pipapo_lookup(net, set, key, ext);

	m = rcu_dereference(priv->match);

	/* This also protects access to all data related to scratch maps.
	 *
	 * Note that we don't need a valid MXCSR state for any of the
	 * operations we use here, so pass 0 as mask and spare a LDMXCSR
	 * instruction.
	 */
	kernel_fpu_begin_mask(0);

	scratch = *raw_cpu_ptr(m->scratch_aligned);
	if (unlikely(!scratch)) {
		kernel_fpu_end();
		return false;
	}
	map_index = raw_cpu_read(nft_pipapo_avx2_scratch_index);

	res  = scratch + (map_index ? m->bsize_max : 0);
	fill = scratch + (map_index ? 0 : m->bsize_max);

	/* Starting map doesn't need to be set for this implementation */

	nft_pipapo_avx2_prepare();

next_match:
	nft_pipapo_for_each_field(f, i, m) {
		bool last = i == m->field_count - 1, first = !i;

#define NFT_SET_PIPAPO_AVX2_LOOKUP(b, n)				\
		(ret = nft_pipapo_avx2_lookup_##b##b_##n(res, fill, f,	\
							 ret, rp,	\
							 first, last))

		if (likely(f->bb == 8)) {
			if (f->groups == 1) {
				NFT_SET_PIPAPO_AVX2_LOOKUP(8, 1);
			} else if (f->groups == 2) {
				NFT_SET_PIPAPO_AVX2_LOOKUP(8, 2);
			} else if (f->groups == 4) {
				NFT_SET_PIPAPO_AVX2_LOOKUP(8, 4);
			} else if (f->groups == 6) {
				NFT_SET_PIPAPO_AVX2_LOOKUP(8, 6);
			} else if (f->groups == 16) {
				NFT_SET_PIPAPO_AVX2_LOOKUP(8, 16);
			} else {
				ret = nft_pipapo_avx2_lookup_slow(res, fill, f,
								  ret, rp,
								  first, last);
			}
		} else {
			if (f->groups == 2) {
				NFT_SET_PIPAPO_AVX2_LOOKUP(4, 2);
			} else if (f->groups == 4) {
				NFT_SET_PIPAPO_AVX2_LOOKUP(4, 4);
			} else if (f->groups == 8) {
				NFT_SET_PIPAPO_AVX2_LOOKUP(4, 8);
			} else if (f->groups == 12) {
				NFT_SET_PIPAPO_AVX2_LOOKUP(4, 12);
			} else if (f->groups == 32) {
				NFT_SET_PIPAPO_AVX2_LOOKUP(4, 32);
			} else {
				ret = nft_pipapo_avx2_lookup_slow(res, fill, f,
								  ret, rp,
								  first, last);
			}
		}
		NFT_PIPAPO_GROUP_BITS_ARE_8_OR_4;

#undef NFT_SET_PIPAPO_AVX2_LOOKUP

		if (ret < 0)
			goto out;

		if (last) {
			*ext = &f->mt[ret].e->ext;
			if (unlikely(nft_set_elem_expired(*ext) ||
				     !nft_set_elem_active(*ext, genmask))) {
				ret = 0;
				goto next_match;
			}

			goto out;
		}

		swap(res, fill);
		rp += NFT_PIPAPO_GROUPS_PADDED_SIZE(f);
	}

out:
	if (i % 2)
		raw_cpu_write(nft_pipapo_avx2_scratch_index, !map_index);
	kernel_fpu_end();

	return ret >= 0;
}
