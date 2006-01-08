/* find_next_bit.c: fallback find next bit implementation
 *
 * Copyright (C) 2004 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/bitops.h>
#include <linux/module.h>

int find_next_bit(const unsigned long *addr, int size, int offset)
{
	const unsigned long *base;
	const int NBITS = sizeof(*addr) * 8;
	unsigned long tmp;

	base = addr;
	if (offset) {
		int suboffset;

		addr += offset / NBITS;

		suboffset = offset % NBITS;
		if (suboffset) {
			tmp = *addr;
			tmp >>= suboffset;
			if (tmp)
				goto finish;
		}

		addr++;
	}

	while ((tmp = *addr) == 0)
		addr++;

	offset = (addr - base) * NBITS;

 finish:
	/* count the remaining bits without using __ffs() since that takes a 32-bit arg */
	while (!(tmp & 0xff)) {
		offset += 8;
		tmp >>= 8;
	}

	while (!(tmp & 1)) {
		offset++;
		tmp >>= 1;
	}

	return offset;
}

EXPORT_SYMBOL(find_next_bit);
