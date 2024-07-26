// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2024, SUSE LLC
 *
 * Authors: Enzo Matsumiya <ematsumiya@suse.de>
 *
 * Implementation of the LZ77 "plain" compression algorithm, as per MS-XCA spec.
 */
#include <linux/slab.h>
#include "lz77.h"

static __always_inline u32 hash3(const u8 *ptr)
{
	return lz77_hash32(lz77_read32(ptr) & 0xffffff, LZ77_HASH_LOG);
}

static u8 *write_match(u8 *dst, u8 **nib, u32 dist, u32 len)
{
	len -= 3;
	dist--;
	dist <<= 3;

	if (len < 7) {
		lz77_write16(dst, dist + len);
		return dst + 2;
	}

	dist |= 7;
	lz77_write16(dst, dist);
	dst += 2;
	len -= 7;

	if (!*nib) {
		*nib = dst;
		lz77_write8(dst, min_t(unsigned int, len, 15));
		dst++;
	} else {
		**nib |= min_t(unsigned int, len, 15) << 4;
		*nib = NULL;
	}

	if (len < 15)
		return dst;

	len -= 15;
	if (len < 255) {
		lz77_write8(dst, len);
		return dst + 1;
	}

	lz77_write8(dst, 0xff);
	dst++;

	len += 7 + 15;
	if (len <= 0xffff) {
		lz77_write16(dst, len);
		return dst + 2;
	}

	lz77_write16(dst, 0);
	dst += 2;
	lz77_write32(dst, len);

	return dst + 4;
}

static u8 *write_literals(u8 *dst, const u8 *dst_end, const u8 *src, size_t count,
			  struct lz77_flags *flags)
{
	const u8 *end = src + count;

	while (src < end) {
		size_t c = lz77_min(count, 32 - flags->count);

		if (dst + c >= dst_end)
			return ERR_PTR(-EFAULT);

		if (lz77_copy(dst, src, c))
			return ERR_PTR(-EFAULT);

		dst += c;
		src += c;
		count -= c;

		flags->val <<= c;
		flags->count += c;
		if (flags->count == 32) {
			lz77_write32(flags->pos, flags->val);
			flags->count = 0;
			flags->pos = dst;
			dst += 4;
		}
	}

	return dst;
}

static __always_inline bool is_valid_match(const u32 dist, const u32 len)
{
	return (dist >= LZ77_MATCH_MIN_DIST && dist < LZ77_MATCH_MAX_DIST) &&
	       (len >= LZ77_MATCH_MIN_LEN && len < LZ77_MATCH_MAX_LEN);
}

static __always_inline const u8 *find_match(u32 *htable, const u8 *base, const u8 *cur,
					    const u8 *end, u32 *best_len)
{
	const u8 *match;
	u32 hash;
	size_t offset;

	hash = hash3(cur);
	offset = cur - base;

	if (htable[hash] >= offset)
		return cur;

	match = base + htable[hash];
	*best_len = lz77_match(match, cur, end);
	if (is_valid_match(cur - match, *best_len))
		return match;

	return cur;
}

int lz77_compress(const u8 *src, size_t src_len, u8 *dst, size_t *dst_len)
{
	const u8 *srcp, *src_end, *anchor;
	struct lz77_flags flags = { 0 };
	u8 *dstp, *dst_end, *nib;
	u32 *htable;
	int ret;

	srcp = src;
	anchor = srcp;
	src_end = src + src_len;

	dstp = dst;
	dst_end = dst + *dst_len;
	flags.pos = dstp;
	nib = NULL;

	memset(dstp, 0, *dst_len);
	dstp += 4;

	htable = kvcalloc(LZ77_HASH_SIZE, sizeof(u32), GFP_KERNEL);
	if (!htable)
		return -ENOMEM;

	/* fill hashtable with invalid offsets */
	memset(htable, 0xff, LZ77_HASH_SIZE * sizeof(u32));

	/* from here on, any error is because @dst_len reached >= @src_len */
	ret = -EMSGSIZE;

	/* main loop */
	while (srcp < src_end) {
		u32 hash, dist, len;
		const u8 *match;

		while (srcp + 3 < src_end) {
			len = LZ77_MATCH_MIN_LEN - 1;
			match = find_match(htable, src, srcp, src_end, &len);
			hash = hash3(srcp);
			htable[hash] = srcp - src;

			if (likely(match < srcp)) {
				dist = srcp - match;
				break;
			}

			srcp++;
		}

		dstp = write_literals(dstp, dst_end, anchor, srcp - anchor, &flags);
		if (IS_ERR(dstp))
			goto err_free;

		if (srcp + 3 >= src_end)
			goto leftovers;

		dstp = write_match(dstp, &nib, dist, len);
		srcp += len;
		anchor = srcp;

		flags.val = (flags.val << 1) | 1;
		flags.count++;
		if (flags.count == 32) {
			lz77_write32(flags.pos, flags.val);
			flags.count = 0;
			flags.pos = dstp;
			dstp += 4;
		}
	}
leftovers:
	if (srcp < src_end) {
		dstp = write_literals(dstp, dst_end, srcp, src_end - srcp, &flags);
		if (IS_ERR(dstp))
			goto err_free;
	}

	flags.val <<= (32 - flags.count);
	flags.val |= (1 << (32 - flags.count)) - 1;
	lz77_write32(flags.pos, flags.val);

	*dst_len = dstp - dst;
	ret = 0;
err_free:
	kvfree(htable);

	return ret;
}
