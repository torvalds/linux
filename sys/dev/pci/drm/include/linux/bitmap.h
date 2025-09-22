/*	$OpenBSD: bitmap.h,v 1.9 2025/09/09 10:11:41 jsg Exp $	*/
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

#ifndef _LINUX_BITMAP_H
#define _LINUX_BITMAP_H

#include <linux/align.h>
#include <linux/bitops.h>
#include <linux/string.h>

#define bitmap_empty(p, n)	(find_first_bit(p, n) == n)

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

static inline void
bitmap_zero(void *p, u_int n)
{
	u_int *ptr = p;
	u_int b;

	for (b = 0; b < n; b += 32)
		ptr[b >> 5] = 0;
}

static inline void
bitmap_fill(void *p, u_int n)
{
	u_int *ptr = p;
	u_int b;

	for (b = 0; b < n; b += 32)
		ptr[b >> 5] = 0xffffffff;
}

static inline void
bitmap_or(void *d, void *s1, void *s2, u_int n)
{
	u_int *dst = d;
	u_int *src1 = s1;
	u_int *src2 = s2;
	u_int b;

	for (b = 0; b < n; b += 32)
		dst[b >> 5] = src1[b >> 5] | src2[b >> 5];
}

static inline void
bitmap_andnot(void *d, void *s1, void *s2, u_int n)
{
	u_int *dst = d;
	u_int *src1 = s1;
	u_int *src2 = s2;
	u_int b;

	for (b = 0; b < n; b += 32)
		dst[b >> 5] = src1[b >> 5] & ~src2[b >> 5];
}

static inline void
bitmap_complement(void *d, void *s, u_int n)
{
	u_int *dst = d;
	u_int *src = s;
	u_int b;

	for (b = 0; b < n; b += 32)
		dst[b >> 5] = ~src[b >> 5];
}

static inline bool
bitmap_intersects(const void *s1, const void *s2, u_int n)
{
	const u_int *b1 = s1;
	const u_int *b2 = s2;
	u_int b;

	for (b = 0; b < n; b += 32)
		if (b1[b >> 5] & b2[b >> 5])
			return true;
	if ((n % 32) != 0)
		if ((b1[n >> 5] & b2[b >> 5]) & (0xffffffff >> (32 - (n % 32))))
			return true;

	return false;
}

static inline void
bitmap_copy(void *d, const void *s, u_int n)
{
	u_int *dst = d;
	const u_int *src = s;
	u_int b;

	for (b = 0; b < n; b += 32)
		dst[b >> 5] = src[b >> 5];
}

static inline void
bitmap_to_arr32(void *d, const unsigned long *src, u_int n)
{
	u_int *dst = d;
#ifdef __LP64__
	u_int b;

	for (b = 0; b < n; b += 32) {
		dst[b >> 5] = src[b >> 6] & 0xffffffff;
		b += 32;
		if (b < n)
			dst[b >> 5] = src[b >> 6] >> 32;
	}
#else
	bitmap_copy(d, src, n);
#endif
	if ((n % 32) != 0)
		dst[n >> 5] &= (0xffffffff >> (32 - (n % 32)));
}

static inline void
bitmap_from_arr32(unsigned long *dst, const void *s, u_int n)
{
#ifdef __LP64__
	const u_int *src = s;
	u_int b;

	for (b = 0; b < n; b += 32) {
		dst[b >> 6] = src[b >> 5];
		b += 32;
		if (b < n)
			dst[b >> 6] |= ((unsigned long)src[b >> 5]) << 32;
	}
	if ((n % 64) != 0)
		dst[n >> 6] &= (0xffffffffffffffffUL >> (64 - (n % 64)));
#else
	bitmap_copy(dst, s, n);
	if ((n % 32) != 0)
		dst[n >> 5] &=  (0xffffffff >> (32 - (n % 32)));
#endif
}

static inline int
bitmap_weight(const void *p, u_int n)
{
	const u_int *ptr = p;
	u_int b;
	int sum = 0;

	for (b = 0; b < n; b += 32)
		sum += hweight32(ptr[b >> 5]);
	return sum;
}

static inline int
bitmap_find_free_region(void *p, u_int n, int o)
{
	int b;

	KASSERT(o == 0);
	b = find_first_zero_bit(p, n);
	if (b == n)
		return -ENOMEM;
	__set_bit(b, p);
	return b;
}

static inline void
bitmap_release_region(void *p, u_int b, int o)
{
	KASSERT(o == 0);
	__clear_bit(b, p);
}

void *bitmap_zalloc(u_int, gfp_t);
void bitmap_free(void *);

#endif
