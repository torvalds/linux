/*
 * Copyright (c) 2000-2004 Silicon Graphics, Inc.  All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * Further, this software is distributed without any warranty that it is
 * free of the rightful claim of any third person regarding infringement
 * or the like.  Any license provided herein, whether implied or
 * otherwise, applies only to this software file.  Patent licenses, if
 * any, provided herein do not apply to combinations of this program with
 * other software, or any other product whatsoever.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write the Free Software Foundation, Inc., 59
 * Temple Place - Suite 330, Boston MA 02111-1307, USA.
 *
 * Contact information: Silicon Graphics, Inc., 1600 Amphitheatre Pkwy,
 * Mountain View, CA  94043, or:
 *
 * http://www.sgi.com
 *
 * For further information regarding this notice, see:
 *
 * http://oss.sgi.com/projects/GenInfo/SGIGPLNoticeExplan/
 */

/*
 * XFS bit manipulation routines, used in non-realtime code.
 */

#include "xfs.h"
#include "xfs_bit.h"
#include "xfs_log.h"
#include "xfs_trans.h"
#include "xfs_buf_item.h"


#ifndef HAVE_ARCH_HIGHBIT
/*
 * Index of high bit number in byte, -1 for none set, 0..7 otherwise.
 */
STATIC const char xfs_highbit[256] = {
       -1, 0, 1, 1, 2, 2, 2, 2,			/* 00 .. 07 */
	3, 3, 3, 3, 3, 3, 3, 3,			/* 08 .. 0f */
	4, 4, 4, 4, 4, 4, 4, 4,			/* 10 .. 17 */
	4, 4, 4, 4, 4, 4, 4, 4,			/* 18 .. 1f */
	5, 5, 5, 5, 5, 5, 5, 5,			/* 20 .. 27 */
	5, 5, 5, 5, 5, 5, 5, 5,			/* 28 .. 2f */
	5, 5, 5, 5, 5, 5, 5, 5,			/* 30 .. 37 */
	5, 5, 5, 5, 5, 5, 5, 5,			/* 38 .. 3f */
	6, 6, 6, 6, 6, 6, 6, 6,			/* 40 .. 47 */
	6, 6, 6, 6, 6, 6, 6, 6,			/* 48 .. 4f */
	6, 6, 6, 6, 6, 6, 6, 6,			/* 50 .. 57 */
	6, 6, 6, 6, 6, 6, 6, 6,			/* 58 .. 5f */
	6, 6, 6, 6, 6, 6, 6, 6,			/* 60 .. 67 */
	6, 6, 6, 6, 6, 6, 6, 6,			/* 68 .. 6f */
	6, 6, 6, 6, 6, 6, 6, 6,			/* 70 .. 77 */
	6, 6, 6, 6, 6, 6, 6, 6,			/* 78 .. 7f */
	7, 7, 7, 7, 7, 7, 7, 7,			/* 80 .. 87 */
	7, 7, 7, 7, 7, 7, 7, 7,			/* 88 .. 8f */
	7, 7, 7, 7, 7, 7, 7, 7,			/* 90 .. 97 */
	7, 7, 7, 7, 7, 7, 7, 7,			/* 98 .. 9f */
	7, 7, 7, 7, 7, 7, 7, 7,			/* a0 .. a7 */
	7, 7, 7, 7, 7, 7, 7, 7,			/* a8 .. af */
	7, 7, 7, 7, 7, 7, 7, 7,			/* b0 .. b7 */
	7, 7, 7, 7, 7, 7, 7, 7,			/* b8 .. bf */
	7, 7, 7, 7, 7, 7, 7, 7,			/* c0 .. c7 */
	7, 7, 7, 7, 7, 7, 7, 7,			/* c8 .. cf */
	7, 7, 7, 7, 7, 7, 7, 7,			/* d0 .. d7 */
	7, 7, 7, 7, 7, 7, 7, 7,			/* d8 .. df */
	7, 7, 7, 7, 7, 7, 7, 7,			/* e0 .. e7 */
	7, 7, 7, 7, 7, 7, 7, 7,			/* e8 .. ef */
	7, 7, 7, 7, 7, 7, 7, 7,			/* f0 .. f7 */
	7, 7, 7, 7, 7, 7, 7, 7,			/* f8 .. ff */
};
#endif

/*
 * Count of bits set in byte, 0..8.
 */
static const char xfs_countbit[256] = {
	0, 1, 1, 2, 1, 2, 2, 3,			/* 00 .. 07 */
	1, 2, 2, 3, 2, 3, 3, 4,			/* 08 .. 0f */
	1, 2, 2, 3, 2, 3, 3, 4,			/* 10 .. 17 */
	2, 3, 3, 4, 3, 4, 4, 5,			/* 18 .. 1f */
	1, 2, 2, 3, 2, 3, 3, 4,			/* 20 .. 27 */
	2, 3, 3, 4, 3, 4, 4, 5,			/* 28 .. 2f */
	2, 3, 3, 4, 3, 4, 4, 5,			/* 30 .. 37 */
	3, 4, 4, 5, 4, 5, 5, 6,			/* 38 .. 3f */
	1, 2, 2, 3, 2, 3, 3, 4,			/* 40 .. 47 */
	2, 3, 3, 4, 3, 4, 4, 5,			/* 48 .. 4f */
	2, 3, 3, 4, 3, 4, 4, 5,			/* 50 .. 57 */
	3, 4, 4, 5, 4, 5, 5, 6,			/* 58 .. 5f */
	2, 3, 3, 4, 3, 4, 4, 5,			/* 60 .. 67 */
	3, 4, 4, 5, 4, 5, 5, 6,			/* 68 .. 6f */
	3, 4, 4, 5, 4, 5, 5, 6,			/* 70 .. 77 */
	4, 5, 5, 6, 5, 6, 6, 7,			/* 78 .. 7f */
	1, 2, 2, 3, 2, 3, 3, 4,			/* 80 .. 87 */
	2, 3, 3, 4, 3, 4, 4, 5,			/* 88 .. 8f */
	2, 3, 3, 4, 3, 4, 4, 5,			/* 90 .. 97 */
	3, 4, 4, 5, 4, 5, 5, 6,			/* 98 .. 9f */
	2, 3, 3, 4, 3, 4, 4, 5,			/* a0 .. a7 */
	3, 4, 4, 5, 4, 5, 5, 6,			/* a8 .. af */
	3, 4, 4, 5, 4, 5, 5, 6,			/* b0 .. b7 */
	4, 5, 5, 6, 5, 6, 6, 7,			/* b8 .. bf */
	2, 3, 3, 4, 3, 4, 4, 5,			/* c0 .. c7 */
	3, 4, 4, 5, 4, 5, 5, 6,			/* c8 .. cf */
	3, 4, 4, 5, 4, 5, 5, 6,			/* d0 .. d7 */
	4, 5, 5, 6, 5, 6, 6, 7,			/* d8 .. df */
	3, 4, 4, 5, 4, 5, 5, 6,			/* e0 .. e7 */
	4, 5, 5, 6, 5, 6, 6, 7,			/* e8 .. ef */
	4, 5, 5, 6, 5, 6, 6, 7,			/* f0 .. f7 */
	5, 6, 6, 7, 6, 7, 7, 8,			/* f8 .. ff */
};

/*
 * xfs_highbit32: get high bit set out of 32-bit argument, -1 if none set.
 */
inline int
xfs_highbit32(
	__uint32_t	v)
{
#ifdef HAVE_ARCH_HIGHBIT
	return highbit32(v);
#else
	int		i;

	if (v & 0xffff0000)
		if (v & 0xff000000)
			i = 24;
		else
			i = 16;
	else if (v & 0x0000ffff)
		if (v & 0x0000ff00)
			i = 8;
		else
			i = 0;
	else
		return -1;
	return i + xfs_highbit[(v >> i) & 0xff];
#endif
}

/*
 * xfs_lowbit64: get low bit set out of 64-bit argument, -1 if none set.
 */
int
xfs_lowbit64(
	__uint64_t	v)
{
	__uint32_t	w = (__uint32_t)v;
	int		n = 0;

	if (w) {	/* lower bits */
		n = ffs(w);
	} else {	/* upper bits */
		w = (__uint32_t)(v >> 32);
		if (w && (n = ffs(w)))
			n += 32;
	}
	return n - 1;
}

/*
 * xfs_highbit64: get high bit set out of 64-bit argument, -1 if none set.
 */
int
xfs_highbit64(
	__uint64_t	v)
{
	__uint32_t	h = (__uint32_t)(v >> 32);

	if (h)
		return xfs_highbit32(h) + 32;
	return xfs_highbit32((__uint32_t)v);
}


/*
 * Count the number of bits set in the bitmap starting with bit
 * start_bit.  Size is the size of the bitmap in words.
 *
 * Do the counting by mapping a byte value to the number of set
 * bits for that value using the xfs_countbit array, i.e.
 * xfs_countbit[0] == 0, xfs_countbit[1] == 1, xfs_countbit[2] == 1,
 * xfs_countbit[3] == 2, etc.
 */
int
xfs_count_bits(uint *map, uint size, uint start_bit)
{
	register int	bits;
	register unsigned char	*bytep;
	register unsigned char	*end_map;
	int		byte_bit;

	bits = 0;
	end_map = (char*)(map + size);
	bytep = (char*)(map + (start_bit & ~0x7));
	byte_bit = start_bit & 0x7;

	/*
	 * If the caller fell off the end of the map, return 0.
	 */
	if (bytep >= end_map) {
		return (0);
	}

	/*
	 * If start_bit is not byte aligned, then process the
	 * first byte separately.
	 */
	if (byte_bit != 0) {
		/*
		 * Shift off the bits we don't want to look at,
		 * before indexing into xfs_countbit.
		 */
		bits += xfs_countbit[(*bytep >> byte_bit)];
		bytep++;
	}

	/*
	 * Count the bits in each byte until the end of the bitmap.
	 */
	while (bytep < end_map) {
		bits += xfs_countbit[*bytep];
		bytep++;
	}

	return (bits);
}

/*
 * Count the number of contiguous bits set in the bitmap starting with bit
 * start_bit.  Size is the size of the bitmap in words.
 */
int
xfs_contig_bits(uint *map, uint	size, uint start_bit)
{
	uint * p = ((unsigned int *) map) + (start_bit >> BIT_TO_WORD_SHIFT);
	uint result = 0;
	uint tmp;

	size <<= BIT_TO_WORD_SHIFT;

	ASSERT(start_bit < size);
	size -= start_bit & ~(NBWORD - 1);
	start_bit &= (NBWORD - 1);
	if (start_bit) {
		tmp = *p++;
		/* set to one first offset bits prior to start */
		tmp |= (~0U >> (NBWORD-start_bit));
		if (tmp != ~0U)
			goto found;
		result += NBWORD;
		size -= NBWORD;
	}
	while (size) {
		if ((tmp = *p++) != ~0U)
			goto found;
		result += NBWORD;
		size -= NBWORD;
	}
	return result - start_bit;
found:
	return result + ffz(tmp) - start_bit;
}

/*
 * This takes the bit number to start looking from and
 * returns the next set bit from there.  It returns -1
 * if there are no more bits set or the start bit is
 * beyond the end of the bitmap.
 *
 * Size is the number of words, not bytes, in the bitmap.
 */
int xfs_next_bit(uint *map, uint size, uint start_bit)
{
	uint * p = ((unsigned int *) map) + (start_bit >> BIT_TO_WORD_SHIFT);
	uint result = start_bit & ~(NBWORD - 1);
	uint tmp;

	size <<= BIT_TO_WORD_SHIFT;

	if (start_bit >= size)
		return -1;
	size -= result;
	start_bit &= (NBWORD - 1);
	if (start_bit) {
		tmp = *p++;
		/* set to zero first offset bits prior to start */
		tmp &= (~0U << start_bit);
		if (tmp != 0U)
			goto found;
		result += NBWORD;
		size -= NBWORD;
	}
	while (size) {
		if ((tmp = *p++) != 0U)
			goto found;
		result += NBWORD;
		size -= NBWORD;
	}
	return -1;
found:
	return result + ffs(tmp) - 1;
}
