// SPDX-License-Identifier: GPL-2.0

#include "eytzinger.h"

/**
 * is_aligned - is this pointer & size okay for word-wide copying?
 * @base: pointer to data
 * @size: size of each element
 * @align: required alignment (typically 4 or 8)
 *
 * Returns true if elements can be copied using word loads and stores.
 * The size must be a multiple of the alignment, and the base address must
 * be if we do not have CONFIG_HAVE_EFFICIENT_UNALIGNED_ACCESS.
 *
 * For some reason, gcc doesn't know to optimize "if (a & mask || b & mask)"
 * to "if ((a | b) & mask)", so we do that by hand.
 */
__attribute_const__ __always_inline
static bool is_aligned(const void *base, size_t size, unsigned char align)
{
	unsigned char lsbits = (unsigned char)size;

	(void)base;
#ifndef CONFIG_HAVE_EFFICIENT_UNALIGNED_ACCESS
	lsbits |= (unsigned char)(uintptr_t)base;
#endif
	return (lsbits & (align - 1)) == 0;
}

/**
 * swap_words_32 - swap two elements in 32-bit chunks
 * @a: pointer to the first element to swap
 * @b: pointer to the second element to swap
 * @n: element size (must be a multiple of 4)
 *
 * Exchange the two objects in memory.  This exploits base+index addressing,
 * which basically all CPUs have, to minimize loop overhead computations.
 *
 * For some reason, on x86 gcc 7.3.0 adds a redundant test of n at the
 * bottom of the loop, even though the zero flag is still valid from the
 * subtract (since the intervening mov instructions don't alter the flags).
 * Gcc 8.1.0 doesn't have that problem.
 */
static void swap_words_32(void *a, void *b, size_t n)
{
	do {
		u32 t = *(u32 *)(a + (n -= 4));
		*(u32 *)(a + n) = *(u32 *)(b + n);
		*(u32 *)(b + n) = t;
	} while (n);
}

/**
 * swap_words_64 - swap two elements in 64-bit chunks
 * @a: pointer to the first element to swap
 * @b: pointer to the second element to swap
 * @n: element size (must be a multiple of 8)
 *
 * Exchange the two objects in memory.  This exploits base+index
 * addressing, which basically all CPUs have, to minimize loop overhead
 * computations.
 *
 * We'd like to use 64-bit loads if possible.  If they're not, emulating
 * one requires base+index+4 addressing which x86 has but most other
 * processors do not.  If CONFIG_64BIT, we definitely have 64-bit loads,
 * but it's possible to have 64-bit loads without 64-bit pointers (e.g.
 * x32 ABI).  Are there any cases the kernel needs to worry about?
 */
static void swap_words_64(void *a, void *b, size_t n)
{
	do {
#ifdef CONFIG_64BIT
		u64 t = *(u64 *)(a + (n -= 8));
		*(u64 *)(a + n) = *(u64 *)(b + n);
		*(u64 *)(b + n) = t;
#else
		/* Use two 32-bit transfers to avoid base+index+4 addressing */
		u32 t = *(u32 *)(a + (n -= 4));
		*(u32 *)(a + n) = *(u32 *)(b + n);
		*(u32 *)(b + n) = t;

		t = *(u32 *)(a + (n -= 4));
		*(u32 *)(a + n) = *(u32 *)(b + n);
		*(u32 *)(b + n) = t;
#endif
	} while (n);
}

/**
 * swap_bytes - swap two elements a byte at a time
 * @a: pointer to the first element to swap
 * @b: pointer to the second element to swap
 * @n: element size
 *
 * This is the fallback if alignment doesn't allow using larger chunks.
 */
static void swap_bytes(void *a, void *b, size_t n)
{
	do {
		char t = ((char *)a)[--n];
		((char *)a)[n] = ((char *)b)[n];
		((char *)b)[n] = t;
	} while (n);
}

/*
 * The values are arbitrary as long as they can't be confused with
 * a pointer, but small integers make for the smallest compare
 * instructions.
 */
#define SWAP_WORDS_64 (swap_r_func_t)0
#define SWAP_WORDS_32 (swap_r_func_t)1
#define SWAP_BYTES    (swap_r_func_t)2
#define SWAP_WRAPPER  (swap_r_func_t)3

struct wrapper {
	cmp_func_t cmp;
	swap_func_t swap;
};

/*
 * The function pointer is last to make tail calls most efficient if the
 * compiler decides not to inline this function.
 */
static void do_swap(void *a, void *b, size_t size, swap_r_func_t swap_func, const void *priv)
{
	if (swap_func == SWAP_WRAPPER) {
		((const struct wrapper *)priv)->swap(a, b, (int)size);
		return;
	}

	if (swap_func == SWAP_WORDS_64)
		swap_words_64(a, b, size);
	else if (swap_func == SWAP_WORDS_32)
		swap_words_32(a, b, size);
	else if (swap_func == SWAP_BYTES)
		swap_bytes(a, b, size);
	else
		swap_func(a, b, (int)size, priv);
}

#define _CMP_WRAPPER ((cmp_r_func_t)0L)

static int do_cmp(const void *a, const void *b, cmp_r_func_t cmp, const void *priv)
{
	if (cmp == _CMP_WRAPPER)
		return ((const struct wrapper *)priv)->cmp(a, b);
	return cmp(a, b, priv);
}

static inline int eytzinger0_do_cmp(void *base, size_t n, size_t size,
			 cmp_r_func_t cmp_func, const void *priv,
			 size_t l, size_t r)
{
	return do_cmp(base + inorder_to_eytzinger0(l, n) * size,
		      base + inorder_to_eytzinger0(r, n) * size,
		      cmp_func, priv);
}

static inline void eytzinger0_do_swap(void *base, size_t n, size_t size,
			   swap_r_func_t swap_func, const void *priv,
			   size_t l, size_t r)
{
	do_swap(base + inorder_to_eytzinger0(l, n) * size,
		base + inorder_to_eytzinger0(r, n) * size,
		size, swap_func, priv);
}

void eytzinger0_sort_r(void *base, size_t n, size_t size,
		       cmp_r_func_t cmp_func,
		       swap_r_func_t swap_func,
		       const void *priv)
{
	int i, c, r;

	/* called from 'sort' without swap function, let's pick the default */
	if (swap_func == SWAP_WRAPPER && !((struct wrapper *)priv)->swap)
		swap_func = NULL;

	if (!swap_func) {
		if (is_aligned(base, size, 8))
			swap_func = SWAP_WORDS_64;
		else if (is_aligned(base, size, 4))
			swap_func = SWAP_WORDS_32;
		else
			swap_func = SWAP_BYTES;
	}

	/* heapify */
	for (i = n / 2 - 1; i >= 0; --i) {
		for (r = i; r * 2 + 1 < n; r = c) {
			c = r * 2 + 1;

			if (c + 1 < n &&
			    eytzinger0_do_cmp(base, n, size, cmp_func, priv, c, c + 1) < 0)
				c++;

			if (eytzinger0_do_cmp(base, n, size, cmp_func, priv, r, c) >= 0)
				break;

			eytzinger0_do_swap(base, n, size, swap_func, priv, r, c);
		}
	}

	/* sort */
	for (i = n - 1; i > 0; --i) {
		eytzinger0_do_swap(base, n, size, swap_func, priv, 0, i);

		for (r = 0; r * 2 + 1 < i; r = c) {
			c = r * 2 + 1;

			if (c + 1 < i &&
			    eytzinger0_do_cmp(base, n, size, cmp_func, priv, c, c + 1) < 0)
				c++;

			if (eytzinger0_do_cmp(base, n, size, cmp_func, priv, r, c) >= 0)
				break;

			eytzinger0_do_swap(base, n, size, swap_func, priv, r, c);
		}
	}
}

void eytzinger0_sort(void *base, size_t n, size_t size,
		     cmp_func_t cmp_func,
		     swap_func_t swap_func)
{
	struct wrapper w = {
		.cmp  = cmp_func,
		.swap = swap_func,
	};

	return eytzinger0_sort_r(base, n, size, _CMP_WRAPPER, SWAP_WRAPPER, &w);
}
