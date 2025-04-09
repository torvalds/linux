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
	swap_func_t swap_func;
};

/*
 * The function pointer is last to make tail calls most efficient if the
 * compiler decides not to inline this function.
 */
static void do_swap(void *a, void *b, size_t size, swap_r_func_t swap_func, const void *priv)
{
	if (swap_func == SWAP_WRAPPER) {
		((const struct wrapper *)priv)->swap_func(a, b, (int)size);
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

static inline int eytzinger1_do_cmp(void *base1, size_t n, size_t size,
			 cmp_r_func_t cmp_func, const void *priv,
			 size_t l, size_t r)
{
	return do_cmp(base1 + inorder_to_eytzinger1(l, n) * size,
		      base1 + inorder_to_eytzinger1(r, n) * size,
		      cmp_func, priv);
}

static inline void eytzinger1_do_swap(void *base1, size_t n, size_t size,
			   swap_r_func_t swap_func, const void *priv,
			   size_t l, size_t r)
{
	do_swap(base1 + inorder_to_eytzinger1(l, n) * size,
		base1 + inorder_to_eytzinger1(r, n) * size,
		size, swap_func, priv);
}

static void eytzinger1_sort_r(void *base1, size_t n, size_t size,
			      cmp_r_func_t cmp_func,
			      swap_r_func_t swap_func,
			      const void *priv)
{
	unsigned i, j, k;

	/* called from 'sort' without swap function, let's pick the default */
	if (swap_func == SWAP_WRAPPER && !((struct wrapper *)priv)->swap_func)
		swap_func = NULL;

	if (!swap_func) {
		if (is_aligned(base1, size, 8))
			swap_func = SWAP_WORDS_64;
		else if (is_aligned(base1, size, 4))
			swap_func = SWAP_WORDS_32;
		else
			swap_func = SWAP_BYTES;
	}

	/* heapify */
	for (i = n / 2; i >= 1; --i) {
		/* Find the sift-down path all the way to the leaves. */
		for (j = i; k = j * 2, k < n;)
			j = eytzinger1_do_cmp(base1, n, size, cmp_func, priv, k, k + 1) > 0 ? k : k + 1;

		/* Special case for the last leaf with no sibling. */
		if (j * 2 == n)
			j *= 2;

		/* Backtrack to the correct location. */
		while (j != i && eytzinger1_do_cmp(base1, n, size, cmp_func, priv, i, j) >= 0)
			j /= 2;

		/* Shift the element into its correct place. */
		for (k = j; j != i;) {
			j /= 2;
			eytzinger1_do_swap(base1, n, size, swap_func, priv, j, k);
		}
	}

	/* sort */
	for (i = n; i > 1; --i) {
		eytzinger1_do_swap(base1, n, size, swap_func, priv, 1, i);

		/* Find the sift-down path all the way to the leaves. */
		for (j = 1; k = j * 2, k + 1 < i;)
			j = eytzinger1_do_cmp(base1, n, size, cmp_func, priv, k, k + 1) > 0 ? k : k + 1;

		/* Special case for the last leaf with no sibling. */
		if (j * 2 + 1 == i)
			j *= 2;

		/* Backtrack to the correct location. */
		while (j >= 1 && eytzinger1_do_cmp(base1, n, size, cmp_func, priv, 1, j) >= 0)
			j /= 2;

		/* Shift the element into its correct place. */
		for (k = j; j > 1;) {
			j /= 2;
			eytzinger1_do_swap(base1, n, size, swap_func, priv, j, k);
		}
	}
}

void eytzinger0_sort_r(void *base, size_t n, size_t size,
		       cmp_r_func_t cmp_func,
		       swap_r_func_t swap_func,
		       const void *priv)
{
	void *base1 = base - size;

	return eytzinger1_sort_r(base1, n, size, cmp_func, swap_func, priv);
}

void eytzinger0_sort(void *base, size_t n, size_t size,
		     cmp_func_t cmp_func,
		     swap_func_t swap_func)
{
	struct wrapper w = {
		.cmp  = cmp_func,
		.swap_func = swap_func,
	};

	return eytzinger0_sort_r(base, n, size, _CMP_WRAPPER, SWAP_WRAPPER, &w);
}

#if 0
#include <linux/slab.h>
#include <linux/random.h>
#include <linux/ktime.h>

static u64 cmp_count;

static int mycmp(const void *a, const void *b)
{
	u32 _a = *(u32 *)a;
	u32 _b = *(u32 *)b;

	cmp_count++;
	if (_a < _b)
		return -1;
	else if (_a > _b)
		return 1;
	else
		return 0;
}

static int test(void)
{
	size_t N, i;
	ktime_t start, end;
	s64 delta;
	u32 *arr;

	for (N = 10000; N <= 100000; N += 10000) {
		arr = kmalloc_array(N, sizeof(u32), GFP_KERNEL);
		cmp_count = 0;

		for (i = 0; i < N; i++)
			arr[i] = get_random_u32();

		start = ktime_get();
		eytzinger0_sort(arr, N, sizeof(u32), mycmp, NULL);
		end = ktime_get();

		delta = ktime_us_delta(end, start);
		printk(KERN_INFO "time: %lld\n", delta);
		printk(KERN_INFO "comparisons: %lld\n", cmp_count);

		u32 prev = 0;

		eytzinger0_for_each(i, N) {
			if (prev > arr[i])
				goto err;
			prev = arr[i];
		}

		kfree(arr);
	}
	return 0;

err:
	kfree(arr);
	return -1;
}
#endif
