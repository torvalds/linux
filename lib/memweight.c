// SPDX-License-Identifier: GPL-2.0
#include <linux/export.h>
#include <linux/bug.h>
#include <linux/bitmap.h>

/**
 * memweight - count the total number of bits set in memory area
 * @ptr: pointer to the start of the area
 * @bytes: the size of the area
 */
size_t memweight(const void *ptr, size_t bytes)
{
	size_t ret = 0;
	size_t longs;
	const unsigned char *bitmap = ptr;

	for (; bytes > 0 && ((unsigned long)bitmap) % sizeof(long);
			bytes--, bitmap++)
		ret += hweight8(*bitmap);

	longs = bytes / sizeof(long);
	if (longs) {
		BUG_ON(longs >= INT_MAX / BITS_PER_LONG);
		ret += bitmap_weight((unsigned long *)bitmap,
				longs * BITS_PER_LONG);
		bytes -= longs * sizeof(long);
		bitmap += longs * sizeof(long);
	}
	/*
	 * The reason that this last loop is distinct from the preceding
	 * bitmap_weight() call is to compute 1-bits in the last region smaller
	 * than sizeof(long) properly on big-endian systems.
	 */
	for (; bytes > 0; bytes--, bitmap++)
		ret += hweight8(*bitmap);

	return ret;
}
EXPORT_SYMBOL(memweight);
