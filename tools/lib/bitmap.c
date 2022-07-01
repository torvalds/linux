// SPDX-License-Identifier: GPL-2.0-only
/*
 * From lib/bitmap.c
 * Helper functions for bitmap.h.
 */
#include <linux/bitmap.h>

int __bitmap_weight(const unsigned long *bitmap, int bits)
{
	int k, w = 0, lim = bits/BITS_PER_LONG;

	for (k = 0; k < lim; k++)
		w += hweight_long(bitmap[k]);

	if (bits % BITS_PER_LONG)
		w += hweight_long(bitmap[k] & BITMAP_LAST_WORD_MASK(bits));

	return w;
}

void __bitmap_or(unsigned long *dst, const unsigned long *bitmap1,
		 const unsigned long *bitmap2, int bits)
{
	int k;
	int nr = BITS_TO_LONGS(bits);

	for (k = 0; k < nr; k++)
		dst[k] = bitmap1[k] | bitmap2[k];
}

size_t bitmap_scnprintf(unsigned long *bitmap, unsigned int nbits,
			char *buf, size_t size)
{
	/* current bit is 'cur', most recently seen range is [rbot, rtop] */
	unsigned int cur, rbot, rtop;
	bool first = true;
	size_t ret = 0;

	rbot = cur = find_first_bit(bitmap, nbits);
	while (cur < nbits) {
		rtop = cur;
		cur = find_next_bit(bitmap, nbits, cur + 1);
		if (cur < nbits && cur <= rtop + 1)
			continue;

		if (!first)
			ret += scnprintf(buf + ret, size - ret, ",");

		first = false;

		ret += scnprintf(buf + ret, size - ret, "%d", rbot);
		if (rbot < rtop)
			ret += scnprintf(buf + ret, size - ret, "-%d", rtop);

		rbot = cur;
	}
	return ret;
}

bool __bitmap_and(unsigned long *dst, const unsigned long *bitmap1,
		 const unsigned long *bitmap2, unsigned int bits)
{
	unsigned int k;
	unsigned int lim = bits/BITS_PER_LONG;
	unsigned long result = 0;

	for (k = 0; k < lim; k++)
		result |= (dst[k] = bitmap1[k] & bitmap2[k]);
	if (bits % BITS_PER_LONG)
		result |= (dst[k] = bitmap1[k] & bitmap2[k] &
			   BITMAP_LAST_WORD_MASK(bits));
	return result != 0;
}

bool __bitmap_equal(const unsigned long *bitmap1,
		    const unsigned long *bitmap2, unsigned int bits)
{
	unsigned int k, lim = bits/BITS_PER_LONG;
	for (k = 0; k < lim; ++k)
		if (bitmap1[k] != bitmap2[k])
			return false;

	if (bits % BITS_PER_LONG)
		if ((bitmap1[k] ^ bitmap2[k]) & BITMAP_LAST_WORD_MASK(bits))
			return false;

	return true;
}

bool __bitmap_intersects(const unsigned long *bitmap1,
			 const unsigned long *bitmap2, unsigned int bits)
{
	unsigned int k, lim = bits/BITS_PER_LONG;
	for (k = 0; k < lim; ++k)
		if (bitmap1[k] & bitmap2[k])
			return true;

	if (bits % BITS_PER_LONG)
		if ((bitmap1[k] & bitmap2[k]) & BITMAP_LAST_WORD_MASK(bits))
			return true;
	return false;
}
