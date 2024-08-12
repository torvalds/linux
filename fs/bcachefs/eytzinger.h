/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _EYTZINGER_H
#define _EYTZINGER_H

#include <linux/bitops.h>
#include <linux/log2.h>

#ifdef EYTZINGER_DEBUG
#define EYTZINGER_BUG_ON(cond)		BUG_ON(cond)
#else
#define EYTZINGER_BUG_ON(cond)
#endif

/*
 * Traversal for trees in eytzinger layout - a full binary tree layed out in an
 * array.
 *
 * Consider using an eytzinger tree any time you would otherwise be doing binary
 * search over an array. Binary search is a worst case scenario for branch
 * prediction and prefetching, but in an eytzinger tree every node's children
 * are adjacent in memory, thus we can prefetch children before knowing the
 * result of the comparison, assuming multiple nodes fit on a cacheline.
 *
 * Two variants are provided, for one based indexing and zero based indexing.
 *
 * Zero based indexing is more convenient, but one based indexing has better
 * alignment and thus better performance because each new level of the tree
 * starts at a power of two, and thus if element 0 was cacheline aligned, each
 * new level will be as well.
 */

static inline unsigned eytzinger1_child(unsigned i, unsigned child)
{
	EYTZINGER_BUG_ON(child > 1);

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
	return size ? rounddown_pow_of_two(size) : 0;
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
	EYTZINGER_BUG_ON(i > size);

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
	EYTZINGER_BUG_ON(i > size);

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
	return size
		? (size + 1 - rounddown_pow_of_two(size)) << 1
		: 0;
}

static inline unsigned __eytzinger1_to_inorder(unsigned i, unsigned size,
					      unsigned extra)
{
	unsigned b = __fls(i);
	unsigned shift = __fls(size) - b;
	int s;

	EYTZINGER_BUG_ON(!i || i > size);

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

	EYTZINGER_BUG_ON(!i || i > size);

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
	for (unsigned (_i) = eytzinger1_first((_size));	\
	     (_i) != 0;					\
	     (_i) = eytzinger1_next((_i), (_size)))

/* Zero based indexing version: */

static inline unsigned eytzinger0_child(unsigned i, unsigned child)
{
	EYTZINGER_BUG_ON(child > 1);

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
	for (unsigned (_i) = eytzinger0_first((_size));	\
	     (_i) != -1;				\
	     (_i) = eytzinger0_next((_i), (_size)))

/* return greatest node <= @search, or -1 if not found */
static inline int eytzinger0_find_le(void *base, size_t nr, size_t size,
				     cmp_func_t cmp, const void *search)
{
	unsigned i, n = 0;

	if (!nr)
		return -1;

	do {
		i = n;
		n = eytzinger0_child(i, cmp(base + i * size, search) <= 0);
	} while (n < nr);

	if (n & 1) {
		/*
		 * @i was greater than @search, return previous node:
		 *
		 * if @i was leftmost/smallest element,
		 * eytzinger0_prev(eytzinger0_first())) returns -1, as expected
		 */
		return eytzinger0_prev(i, nr);
	} else {
		return i;
	}
}

static inline int eytzinger0_find_gt(void *base, size_t nr, size_t size,
				     cmp_func_t cmp, const void *search)
{
	ssize_t idx = eytzinger0_find_le(base, nr, size, cmp, search);

	/*
	 * if eytitzinger0_find_le() returned -1 - no element was <= search - we
	 * want to return the first element; next/prev identities mean this work
	 * as expected
	 *
	 * similarly if find_le() returns last element, we should return -1;
	 * identities mean this all works out:
	 */
	return eytzinger0_next(idx, nr);
}

static inline int eytzinger0_find_ge(void *base, size_t nr, size_t size,
				     cmp_func_t cmp, const void *search)
{
	ssize_t idx = eytzinger0_find_le(base, nr, size, cmp, search);

	if (idx < nr && !cmp(base + idx * size, search))
		return idx;

	return eytzinger0_next(idx, nr);
}

#define eytzinger0_find(base, nr, size, _cmp, search)			\
({									\
	void *_base		= (base);				\
	const void *_search	= (search);				\
	size_t _nr		= (nr);					\
	size_t _size		= (size);				\
	size_t _i		= 0;					\
	int _res;							\
									\
	while (_i < _nr &&						\
	       (_res = _cmp(_search, _base + _i * _size)))		\
		_i = eytzinger0_child(_i, _res > 0);			\
	_i;								\
})

void eytzinger0_sort_r(void *, size_t, size_t,
		       cmp_r_func_t, swap_r_func_t, const void *);
void eytzinger0_sort(void *, size_t, size_t, cmp_func_t, swap_func_t);

#endif /* _EYTZINGER_H */
