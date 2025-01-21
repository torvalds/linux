// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2024, SUSE LLC
 *
 * Authors: Enzo Matsumiya <ematsumiya@suse.de>
 *
 * Implementation of the LZ77 "plain" compression algorithm, as per MS-XCA spec.
 */
#include <linux/slab.h>
#include <linux/sizes.h>
#include <linux/count_zeros.h>
#include <linux/unaligned.h>

#include "lz77.h"

/*
 * Compression parameters.
 */
#define LZ77_MATCH_MIN_LEN	4
#define LZ77_MATCH_MIN_DIST	1
#define LZ77_MATCH_MAX_DIST	SZ_1K
#define LZ77_HASH_LOG		15
#define LZ77_HASH_SIZE		(1 << LZ77_HASH_LOG)
#define LZ77_STEP_SIZE		sizeof(u64)

static __always_inline u8 lz77_read8(const u8 *ptr)
{
	return get_unaligned(ptr);
}

static __always_inline u64 lz77_read64(const u64 *ptr)
{
	return get_unaligned(ptr);
}

static __always_inline void lz77_write8(u8 *ptr, u8 v)
{
	put_unaligned(v, ptr);
}

static __always_inline void lz77_write16(u16 *ptr, u16 v)
{
	put_unaligned_le16(v, ptr);
}

static __always_inline void lz77_write32(u32 *ptr, u32 v)
{
	put_unaligned_le32(v, ptr);
}

static __always_inline u32 lz77_match_len(const void *wnd, const void *cur, const void *end)
{
	const void *start = cur;
	u64 diff;

	/* Safe for a do/while because otherwise we wouldn't reach here from the main loop. */
	do {
		diff = lz77_read64(cur) ^ lz77_read64(wnd);
		if (!diff) {
			cur += LZ77_STEP_SIZE;
			wnd += LZ77_STEP_SIZE;

			continue;
		}

		/* This computes the number of common bytes in @diff. */
		cur += count_trailing_zeros(diff) >> 3;

		return (cur - start);
	} while (likely(cur + LZ77_STEP_SIZE < end));

	while (cur < end && lz77_read8(cur++) == lz77_read8(wnd++))
		;

	return (cur - start);
}

static __always_inline void *lz77_write_match(void *dst, void **nib, u32 dist, u32 len)
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
		lz77_write8(dst, umin(len, 15));
		*nib = dst;
		dst++;
	} else {
		u8 *b = *nib;

		lz77_write8(b, *b | umin(len, 15) << 4);
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

noinline int lz77_compress(const void *src, u32 slen, void *dst, u32 *dlen)
{
	const void *srcp, *end;
	void *dstp, *nib, *flag_pos;
	u32 flag_count = 0;
	long flag = 0;
	u64 *htable;

	srcp = src;
	end = src + slen;
	dstp = dst;
	nib = NULL;
	flag_pos = dstp;
	dstp += 4;

	htable = kvcalloc(LZ77_HASH_SIZE, sizeof(*htable), GFP_KERNEL);
	if (!htable)
		return -ENOMEM;

	/* Main loop. */
	do {
		u32 dist, len = 0;
		const void *wnd;
		u64 hash;

		hash = ((lz77_read64(srcp) << 24) * 889523592379ULL) >> (64 - LZ77_HASH_LOG);
		wnd = src + htable[hash];
		htable[hash] = srcp - src;
		dist = srcp - wnd;

		if (dist && dist < LZ77_MATCH_MAX_DIST)
			len = lz77_match_len(wnd, srcp, end);

		if (len < LZ77_MATCH_MIN_LEN) {
			lz77_write8(dstp, lz77_read8(srcp));

			dstp++;
			srcp++;

			flag <<= 1;
			flag_count++;
			if (flag_count == 32) {
				lz77_write32(flag_pos, flag);
				flag_count = 0;
				flag_pos = dstp;
				dstp += 4;
			}

			continue;
		}

		/*
		 * Bail out if @dstp reached >= 7/8 of @slen -- already compressed badly, not worth
		 * going further.
		 */
		if (unlikely(dstp - dst >= slen - (slen >> 3))) {
			*dlen = slen;
			goto out;
		}

		dstp = lz77_write_match(dstp, &nib, dist, len);
		srcp += len;

		flag = (flag << 1) | 1;
		flag_count++;
		if (flag_count == 32) {
			lz77_write32(flag_pos, flag);
			flag_count = 0;
			flag_pos = dstp;
			dstp += 4;
		}
	} while (likely(srcp + LZ77_STEP_SIZE < end));

	while (srcp < end) {
		u32 c = umin(end - srcp, 32 - flag_count);

		memcpy(dstp, srcp, c);

		dstp += c;
		srcp += c;

		flag <<= c;
		flag_count += c;
		if (flag_count == 32) {
			lz77_write32(flag_pos, flag);
			flag_count = 0;
			flag_pos = dstp;
			dstp += 4;
		}
	}

	flag <<= (32 - flag_count);
	flag |= (1 << (32 - flag_count)) - 1;
	lz77_write32(flag_pos, flag);

	*dlen = dstp - dst;
out:
	kvfree(htable);

	if (*dlen < slen)
		return 0;

	return -EMSGSIZE;
}
