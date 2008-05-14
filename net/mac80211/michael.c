/*
 * Michael MIC implementation - optimized for TKIP MIC operations
 * Copyright 2002-2003, Instant802 Networks, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/types.h>
#include <linux/bitops.h>
#include <asm/unaligned.h>

#include "michael.h"

#define michael_block(l, r) \
do { \
	r ^= rol32(l, 17); \
	l += r; \
	r ^= ((l & 0xff00ff00) >> 8) | ((l & 0x00ff00ff) << 8); \
	l += r; \
	r ^= rol32(l, 3); \
	l += r; \
	r ^= ror32(l, 2); \
	l += r; \
} while (0)

void michael_mic(u8 *key, u8 *da, u8 *sa, u8 priority,
		 u8 *data, size_t data_len, u8 *mic)
{
	u32 l, r, val;
	size_t block, blocks, left;

	l = get_unaligned_le32(key);
	r = get_unaligned_le32(key + 4);

	/* A pseudo header (DA, SA, Priority, 0, 0, 0) is used in Michael MIC
	 * calculation, but it is _not_ transmitted */
	l ^= get_unaligned_le32(da);
	michael_block(l, r);
	l ^= get_unaligned_le16(&da[4]) | (get_unaligned_le16(sa) << 16);
	michael_block(l, r);
	l ^= get_unaligned_le32(&sa[2]);
	michael_block(l, r);
	l ^= priority;
	michael_block(l, r);

	/* Real data */
	blocks = data_len / 4;
	left = data_len % 4;

	for (block = 0; block < blocks; block++) {
		l ^= get_unaligned_le32(&data[block * 4]);
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

	put_unaligned_le32(l, mic);
	put_unaligned_le32(r, mic + 4);
}
