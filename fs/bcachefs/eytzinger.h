/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _EYTZINGER_H
#define _EYTZINGER_H

#include <linux/bitops.h>
#include <linux/log2.h>

#include "util.h"

/*
 * Traversal for trees in eytzinger layout - a full binary tree layed out in an
 * array
 */

/*
 * One based indexing version:
 *
 * With one based indexing each level of the tree starts at a power of two -
 * good for cacheline alignment:
 */

static inline unsigned eytzinger1_child(unsigned i, unsigned child)
{
	EBUG_ON(child > 1);

	return (i << 1) + child;
}

static inline unsigned eytzinger1_left_child(unsigned i)
{
	return eytzinger1_child(i, 0);
}

static inline unsigned eytzinger1_right_child(unsigned i)
{
	return eytzinger1_child(i, 1);
}

static inline unsigned eytzinger1_first(unsigned size)
{
	return rounddown_pow_of_two(size);
}

static inline unsigned eytzinger1_last(unsigned size)
{
	return rounddown_pow_of_two(size + 1) - 1;
}

/*
 * eytzinger1_next() and eytzinger1_prev() have the nice properties that
 *
 * eytzinger1_next(0) == eytzinger1_first())
 * eytzinger1_prev(0) == eytzinger1_last())
 *
 * eytzinger1_prev(eytzinger1_first()) == 0
 * eytzinger1_next(eytzinger1_last()) == 0
 */

static inline unsigned eytzinger1_next(unsigned i, unsigned size)
{
	EBUG_ON(i > size);

	if (eytzinger1_right_child(i) <= size) {
		i = eytzinger1_right_child(i);

		i <<= __fls(size + 1) - __fls(i);
		i >>= i > size;
	} else {
		i >>= ffz(i) + 1;
	}

	return i;
}

static inline unsigned eytzinger1_prev(unsigned i, unsigned size)
{
	EBUG_ON(i > size);

	if (eytzinger1_left_child(i) <= size) {
		i = eytzinger1_left_child(i) + 1;

		i <<= __fls(size + 1) - __fls(i);
		i -= 1;
		i >>= i > size;
	} else {
		i >>= __ffs(i) + 1;
	}

	return i;
}

static inline unsigned eytzinger1_extra(unsigned size)
{
	return (size + 1 - rounddown_pow_of_two(size)) << 1;
}

static inline unsigned __eytzinger1_to_inorder(unsigned i, unsigned size,
					      unsigned extra)
{
	unsigned b = __fls(i);
	unsigned shift = __fls(size) - b;
	int s;

	EBUG_ON(!i || i > size);

	i  ^= 1U << b;
	i <<= 1;
	i  |= 1;
	i <<= shift;

	/*
	 * sign bit trick:
	 *
	 * if (i > extra)
	 *	i -= (i - extra) >> 1;
	 */
	s = extra - i;
	i += (s >> 1) & (s >> 31);

	return i;
}

static inline unsigned __inorder_to_eytzinger1(unsigned i, unsigned size,
					       unsigned extra)
{
	unsigned shift;
	int s;

	EBUG_ON(!i || i > size);

	/*
	 * sign bit trick:
	 *
	 * if (i > extra)
	 *	i += i - extra;
	 */
	s = extra - i;
	i -= s & (s >> 31);

	shift = __ffs(i);

	i >>= shift + 1;
	i  |= 1U << (__fls(size) - shift);

	return i;
}

static inline unsigned eytzinger1_to_inorder(unsigned i, unsigned size)
{
	return __eytzinger1_to_inorder(i, size, eytzinger1_extra(size));
}

static inline unsigned inorder_to_eytzinger1(unsigned i, unsigned size)
{
	return __inorder_to_eytzinger1(i, size, eytzinger1_extra(size));
}

#define eytzinger1_for_each(_i, _size)			\
	for ((_i) = eytzinger1_first((_size));		\
	     (_i) != 0;					\
	     (_i) = eytzinger1_next((_i), (_size)))

/* Zero based indexing version: */

static inline unsigned eytzinger0_child(unsigned i, unsigned child)
{
	EBUG_ON(child > 1);

	return (i << 1) + 1 + child;
}

static inline unsigned eytzinger0_left_child(unsigned i)
{
	return eytzinger0_child(i, 0);
}

static inline unsigned eytzinger0_right_child(unsigned i)
{
	return eytzinger0_child(i, 1);
}

static inline unsigned eytzinger0_first(unsigned size)
{
	return eytzinger1_first(size) - 1;
}

static inline unsigned eytzinger0_last(unsigned size)
{
	return eytzinger1_last(size) - 1;
}

static inline unsigned eytzinger0_next(unsigned i, unsigned size)
{
	return eytzinger1_next(i + 1, size) - 1;
}

static inline unsigned eytzinger0_prev(unsigned i, unsigned size)
{
	return eytzinger1_prev(i + 1, size) - 1;
}

static inline unsigned eytzinger0_extra(unsigned size)
{
	return eytzinger1_extra(size);
}

static inline unsigned __eytzinger0_to_inorder(unsigned i, unsigned size,
					       unsigned extra)
{
	return __eytzinger1_to_inorder(i + 1, size, extra) - 1;
}

static inline unsigned __inorder_to_eytzinger0(unsigned i, unsigned size,
					       unsigned extra)
{
	return __inorder_to_eytzinger1(i + 1, size, extra) - 1;
}

static inline unsigned eytzinger0_to_inorder(unsigned i, unsigned size)
{
	return __eytzinger0_to_inorder(i, size, eytzinger0_extra(size));
}

static inline unsigned inorder_to_eytzinger0(unsigned i, unsigned size)
{
	return __inorder_to_eytzinger0(i, size, eytzinger0_extra(size));
}

#define eytzinger0_for_each(_i, _size)			\
	for ((_i) = eytzinger0_first((_size));		\
	     (_i) != -1;				\
	     (_i) = eytzinger0_next((_i), (_size)))

typedef int (*eytzinger_cmp_fn)(const void *l, const void *r, size_t size);

/* return greatest node <= @search, or -1 if not found */
static inline ssize_t eytzinger0_find_le(void *base, size_t nr, size_t size,
					 eytzinger_cmp_fn cmp, const void *search)
{
	unsigned i, n = 0;

	if (!nr)
		return -1;

	do {
		i = n;
		n = eytzinger0_child(i, cmp(search, base + i * size, size) >= 0);
	} while (n < nr);

	if (n & 1) {
		/* @i was greater than @search, return previous node: */

		if (i == eytzinger0_first(nr))
			return -1;

		return eytzinger0_prev(i, nr);
	} else {
		return i;
	}
}

#define eytzinger0_find(base, nr, size, _cmp, search)			\
({									\
	void *_base	= (base);					\
	void *_search	= (search);					\
	size_t _nr	= (nr);						\
	size_t _size	= (size);					\
	size_t _i	= 0;						\
	int _res;							\
									\
	while (_i < _nr &&						\
	       (_res = _cmp(_search, _base + _i * _size, _size)))	\
		_i = eytzinger0_child(_i, _res > 0);			\
	_i;								\
})

void eytzinger0_sort(void *, size_t, size_t,
		    int (*cmp_func)(const void *, const void *, size_t),
		    void (*swap_func)(void *, void *, size_t));

#endif /* _EYTZINGER_H */
