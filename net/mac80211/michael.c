/*
 * Michael MIC implementation - optimized for TKIP MIC operations
 * Copyright 2002-2003, Instant802 Networks, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/types.h>

#include "michael.h"

static inline u32 rotr(u32 val, int bits)
{
	return (val >> bits) | (val << (32 - bits));
}


static inline u32 rotl(u32 val, int bits)
{
	return (val << bits) | (val >> (32 - bits));
}


static inline u32 xswap(u32 val)
{
	return ((val & 0xff00ff00) >> 8) | ((val & 0x00ff00ff) << 8);
}


#define michael_block(l, r) \
do { \
	r ^= rotl(l, 17); \
	l += r; \
	r ^= xswap(l); \
	l += r; \
	r ^= rotl(l, 3); \
	l += r; \
	r ^= rotr(l, 2); \
	l += r; \
} while (0)


static inline u32 michael_get32(u8 *data)
{
	return data[0] | (data[1] << 8) | (data[2] << 16) | (data[3] << 24);
}


static inline void michael_put32(u32 val, u8 *data)
{
	data[0] = val & 0xff;
	data[1] = (val >> 8) & 0xff;
	data[2] = (val >> 16) & 0xff;
	data[3] = (val >> 24) & 0xff;
}


void michael_mic(u8 *key, u8 *da, u8 *sa, u8 priority,
		 u8 *data, size_t data_len, u8 *mic)
{
	u32 l, r, val;
	size_t block, blocks, left;

	l = michael_get32(key);
	r = michael_get32(key + 4);

	/* A pseudo header (DA, SA, Priority, 0, 0, 0) is used in Michael MIC
	 * calculation, but it is _not_ transmitted */
	l ^= michael_get32(da);
	michael_block(l, r);
	l ^= da[4] | (da[5] << 8) | (sa[0] << 16) | (sa[1] << 24);
	michael_block(l, r);
	l ^= michael_get32(&sa[2]);
	michael_block(l, r);
	l ^= priority;
	michael_block(l, r);

	/* Real data */
	blocks = data_len / 4;
	left = data_len % 4;

	for (block = 0; block < blocks; block++) {
		l ^= michael_get32(&data[block * 4]);
		michael_block(l, r);
	}

	/* Partial block of 0..3 bytes and padding: 0x5a + 4..7 zeros to make
	 * total length a multiple of 4. */
	val = 0x5a;
	while (left > 0) {
		val <<= 8;
		left--;
		val |= data[blocks * 4 + left];
	}
	l ^= val;
	michael_block(l, r);
	/* last block is zero, so l ^ 0 = l */
	michael_block(l, r);

	michael_put32(l, mic);
	michael_put32(r, mic + 4);
}
