/*	$OpenBSD: bitmap.h,v 1.1 2022/09/08 18:16:26 mglocker Exp $ */

/*
 * Copyright 2004 Eric Anholt
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * VA LINUX SYSTEMS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

static inline void
__clear_bit(u_int b, volatile void *p)
{
	volatile u_int *ptr = (volatile u_int *)p;
	ptr[b >> 5] &= ~(1 << (b & 0x1f));
}

static inline void
__set_bit(u_int b, volatile void *p)
{
	volatile u_int *ptr = (volatile u_int *)p;
	ptr[b >> 5] |= (1 << (b & 0x1f));
}

static inline int
find_next_zero_bit(volatile void *p, int max, int b)
{
	volatile u_int *ptr = (volatile u_int *)p;

	for (; b < max; b += 32) {
		if (ptr[b >> 5] != ~0) {
			for (;;) {
				if ((ptr[b >> 5] & (1 << (b & 0x1f))) == 0)
					return b;
				b++;
			}
		}
	}
	return max;
}

static inline int
find_next_bit(volatile void *p, int max, int b)
{
	volatile u_int *ptr = (volatile u_int *)p;

	for (; b < max; b+= 32) {
		if (ptr[b >> 5] != 0) {
			for (;;) {
				if (ptr[b >> 5] & (1 << (b & 0x1f)))
					return b;
				b++;
			}
		}
	}
	return max;
}

/*
 * Copyright (c) 2013, 2014, 2015 Mark Kettenis
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

static inline void
bitmap_set(void *p, int b, u_int n)
{
	u_int end = b + n;

	for (; b < end; b++)
		__set_bit(b, p);
}

static inline void
bitmap_clear(void *p, int b, u_int n)
{
	u_int end = b + n;

	for (; b < end; b++)
		__clear_bit(b, p);
}

static inline u_long
bitmap_find_next_zero_area_off(void *p, u_long size, u_long start, u_long n,
    u_long align_mask, u_long align_offset)
{
	u_long index, end, i;

	while (1) {
		index = (((find_next_zero_bit(p, size, start) +
		    align_offset) + align_mask) & ~align_mask) - align_offset;

		end = index + n;
		if (end > size)
			return end;

		i = find_next_bit(p, end, index);
		if (i >= end)
			break;
		start = i + 1;
	}

	return index;
}

static inline unsigned long
bitmap_find_next_zero_area(void *p, u_long size, u_long start, u_long n,
    u_long align_mask)
{
	return bitmap_find_next_zero_area_off(p, size, start, n, align_mask, 0);
}
