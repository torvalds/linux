// SPDX-License-Identifier: GPL-2.0

#include <linux/bitmap.h>

__rust_helper
void rust_helper_bitmap_copy_and_extend(unsigned long *to, const unsigned long *from,
		unsigned int count, unsigned int size)
{
	bitmap_copy_and_extend(to, from, count, size);
}
