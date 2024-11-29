/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_MIN_HEAP_H
#define _LINUX_MIN_HEAP_H

#include <linux/bug.h>
#include <linux/string.h>
#include <linux/types.h>

/*
 * The Min Heap API provides utilities for managing min-heaps, a binary tree
 * structure where each node's value is less than or equal to its children's
 * values, ensuring the smallest element is at the root.
 *
 * Users should avoid directly calling functions prefixed with __min_heap_*().
 * Instead, use the provided macro wrappers.
 *
 * For further details and examples, refer to Documentation/core-api/min_heap.rst.
 */

/**
 * Data structure to hold a min-heap.
 * @nr: Number of elements currently in the heap.
 * @size: Maximum number of elements that can be held in current storage.
 * @data: Pointer to the start of array holding the heap elements.
 * @preallocated: Start of the static preallocated array holding the heap elements.
 */
#define MIN_HEAP_PREALLOCATED(_type, _name, _nr)	\
struct _name {	\
	int nr;	\
	int size;	\
	_type *data;	\
	_type preallocated[_nr];	\
}

#define DEFINE_MIN_HEAP(_type, _name) MIN_HEAP_PREALLOCATED(_type, _name, 0)

typedef DEFINE_MIN_HEAP(char, min_heap_char) min_heap_char;

#define __minheap_cast(_heap)		(typeof((_heap)->data[0]) *)
#define __minheap_obj_size(_heap)	sizeof((_heap)->data[0])

/**
 * struct min_heap_callbacks - Data/functions to customise the min_heap.
 * @less: Partial order function for this heap.
 * @swp: Swap elements function.
 */
struct min_heap_callbacks {
	bool (*less)(const void *lhs, const void *rhs, void *args);
	void (*swp)(void *lhs, void *rhs, void *args);
};

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
static __always_inline
void swap_words_32(void *a, void *b, size_t n)
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
static __always_inline
void swap_words_64(void *a, void *b, size_t n)
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
static __always_inline
void swap_bytes(void *a, void *b, size_t n)
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
#define SWAP_WORDS_64 ((void (*)(void *, void *, void *))0)
#define SWAP_WORDS_32 ((void (*)(void *, void *, void *))1)
#define SWAP_BYTES    ((void (*)(void *, void *, void *))2)

/*
 * Selects the appropriate swap function based on the element size.
 */
static __always_inline
void *select_swap_func(const void *base, size_t size)
{
	if (is_aligned(base, size, 8))
		return SWAP_WORDS_64;
	else if (is_aligned(base, size, 4))
		return SWAP_WORDS_32;
	else
		return SWAP_BYTES;
}

static __always_inline
void do_swap(void *a, void *b, size_t size, void (*swap_func)(void *lhs, void *rhs, void *args),
	     void *priv)
{
	if (swap_func == SWAP_WORDS_64)
		swap_words_64(a, b, size);
	else if (swap_func == SWAP_WORDS_32)
		swap_words_32(a, b, size);
	else if (swap_func == SWAP_BYTES)
		swap_bytes(a, b, size);
	else
		swap_func(a, b, priv);
}

/**
 * parent - given the offset of the child, find the offset of the parent.
 * @i: the offset of the heap element whose parent is sought.  Non-zero.
 * @lsbit: a precomputed 1-bit mask, equal to "size & -size"
 * @size: size of each element
 *
 * In terms of array indexes, the parent of element j = @i/@size is simply
 * (j-1)/2.  But when working in byte offsets, we can't use implicit
 * truncation of integer divides.
 *
 * Fortunately, we only need one bit of the quotient, not the full divide.
 * @size has a least significant bit.  That bit will be clear if @i is
 * an even multiple of @size, and set if it's an odd multiple.
 *
 * Logically, we're doing "if (i & lsbit) i -= size;", but since the
 * branch is unpredictable, it's done with a bit of clever branch-free
 * code instead.
 */
__attribute_const__ __always_inline
static size_t parent(size_t i, unsigned int lsbit, size_t size)
{
	i -= size;
	i -= size & -(i & lsbit);
	return i / 2;
}

/* Initialize a min-heap. */
static __always_inline
void __min_heap_init_inline(min_heap_char *heap, void *data, int size)
{
	heap->nr = 0;
	heap->size = size;
	if (data)
		heap->data = data;
	else
		heap->data = heap->preallocated;
}

#define min_heap_init_inline(_heap, _data, _size)	\
	__min_heap_init_inline(container_of(&(_heap)->nr, min_heap_char, nr), _data, _size)

/* Get the minimum element from the heap. */
static __always_inline
void *__min_heap_peek_inline(struct min_heap_char *heap)
{
	return heap->nr ? heap->data : NULL;
}

#define min_heap_peek_inline(_heap)	\
	(__minheap_cast(_heap)	\
	 __min_heap_peek_inline(container_of(&(_heap)->nr, min_heap_char, nr)))

/* Check if the heap is full. */
static __always_inline
bool __min_heap_full_inline(min_heap_char *heap)
{
	return heap->nr == heap->size;
}

#define min_heap_full_inline(_heap)	\
	__min_heap_full_inline(container_of(&(_heap)->nr, min_heap_char, nr))

/* Sift the element at pos down the heap. */
static __always_inline
void __min_heap_sift_down_inline(min_heap_char *heap, int pos, size_t elem_size,
				 const struct min_heap_callbacks *func, void *args)
{
	const unsigned long lsbit = elem_size & -elem_size;
	void *data = heap->data;
	void (*swp)(void *lhs, void *rhs, void *args) = func->swp;
	/* pre-scale counters for performance */
	size_t a = pos * elem_size;
	size_t b, c, d;
	size_t n = heap->nr * elem_size;

	if (!swp)
		swp = select_swap_func(data, elem_size);

	/* Find the sift-down path all the way to the leaves. */
	for (b = a; c = 2 * b + elem_size, (d = c + elem_size) < n;)
		b = func->less(data + c, data + d, args) ? c : d;

	/* Special case for the last leaf with no sibling. */
	if (d == n)
		b = c;

	/* Backtrack to the correct location. */
	while (b != a && func->less(data + a, data + b, args))
		b = parent(b, lsbit, elem_size);

	/* Shift the element into its correct place. */
	c = b;
	while (b != a) {
		b = parent(b, lsbit, elem_size);
		do_swap(data + b, data + c, elem_size, swp, args);
	}
}

#define min_heap_sift_down_inline(_heap, _pos, _func, _args)	\
	__min_heap_sift_down_inline(container_of(&(_heap)->nr, min_heap_char, nr), _pos,	\
				    __minheap_obj_size(_heap), _func, _args)

/* Sift up ith element from the heap, O(log2(nr)). */
static __always_inline
void __min_heap_sift_up_inline(min_heap_char *heap, size_t elem_size, size_t idx,
			       const struct min_heap_callbacks *func, void *args)
{
	const unsigned long lsbit = elem_size & -elem_size;
	void *data = heap->data;
	void (*swp)(void *lhs, void *rhs, void *args) = func->swp;
	/* pre-scale counters for performance */
	size_t a = idx * elem_size, b;

	if (!swp)
		swp = select_swap_func(data, elem_size);

	while (a) {
		b = parent(a, lsbit, elem_size);
		if (func->less(data + b, data + a, args))
			break;
		do_swap(data + a, data + b, elem_size, swp, args);
		a = b;
	}
}

#define min_heap_sift_up_inline(_heap, _idx, _func, _args)	\
	__min_heap_sift_up_inline(container_of(&(_heap)->nr, min_heap_char, nr),	\
				  __minheap_obj_size(_heap), _idx, _func, _args)

/* Floyd's approach to heapification that is O(nr). */
static __always_inline
void __min_heapify_all_inline(min_heap_char *heap, size_t elem_size,
			      const struct min_heap_callbacks *func, void *args)
{
	int i;

	for (i = heap->nr / 2 - 1; i >= 0; i--)
		__min_heap_sift_down_inline(heap, i, elem_size, func, args);
}

#define min_heapify_all_inline(_heap, _func, _args)	\
	__min_heapify_all_inline(container_of(&(_heap)->nr, min_heap_char, nr),	\
				 __minheap_obj_size(_heap), _func, _args)

/* Remove minimum element from the heap, O(log2(nr)). */
static __always_inline
bool __min_heap_pop_inline(min_heap_char *heap, size_t elem_size,
			   const struct min_heap_callbacks *func, void *args)
{
	void *data = heap->data;

	if (WARN_ONCE(heap->nr <= 0, "Popping an empty heap"))
		return false;

	/* Place last element at the root (position 0) and then sift down. */
	heap->nr--;
	memcpy(data, data + (heap->nr * elem_size), elem_size);
	__min_heap_sift_down_inline(heap, 0, elem_size, func, args);

	return true;
}

#define min_heap_pop_inline(_heap, _func, _args)	\
	__min_heap_pop_inline(container_of(&(_heap)->nr, min_heap_char, nr),	\
			      __minheap_obj_size(_heap), _func, _args)

/*
 * Remove the minimum element and then push the given element. The
 * implementation performs 1 sift (O(log2(nr))) and is therefore more
 * efficient than a pop followed by a push that does 2.
 */
static __always_inline
void __min_heap_pop_push_inline(min_heap_char *heap, const void *element, size_t elem_size,
				const struct min_heap_callbacks *func, void *args)
{
	memcpy(heap->data, element, elem_size);
	__min_heap_sift_down_inline(heap, 0, elem_size, func, args);
}

#define min_heap_pop_push_inline(_heap, _element, _func, _args)	\
	__min_heap_pop_push_inline(container_of(&(_heap)->nr, min_heap_char, nr), _element,	\
				   __minheap_obj_size(_heap), _func, _args)

/* Push an element on to the heap, O(log2(nr)). */
static __always_inline
bool __min_heap_push_inline(min_heap_char *heap, const void *element, size_t elem_size,
			    const struct min_heap_callbacks *func, void *args)
{
	void *data = heap->data;
	int pos;

	if (WARN_ONCE(heap->nr >= heap->size, "Pushing on a full heap"))
		return false;

	/* Place at the end of data. */
	pos = heap->nr;
	memcpy(data + (pos * elem_size), element, elem_size);
	heap->nr++;

	/* Sift child at pos up. */
	__min_heap_sift_up_inline(heap, elem_size, pos, func, args);

	return true;
}

#define min_heap_push_inline(_heap, _element, _func, _args)	\
	__min_heap_push_inline(container_of(&(_heap)->nr, min_heap_char, nr), _element,	\
					    __minheap_obj_size(_heap), _func, _args)

/* Remove ith element from the heap, O(log2(nr)). */
static __always_inline
bool __min_heap_del_inline(min_heap_char *heap, size_t elem_size, size_t idx,
			   const struct min_heap_callbacks *func, void *args)
{
	void *data = heap->data;
	void (*swp)(void *lhs, void *rhs, void *args) = func->swp;

	if (WARN_ONCE(heap->nr <= 0, "Popping an empty heap"))
		return false;

	if (!swp)
		swp = select_swap_func(data, elem_size);

	/* Place last element at the root (position 0) and then sift down. */
	heap->nr--;
	if (idx == heap->nr)
		return true;
	do_swap(data + (idx * elem_size), data + (heap->nr * elem_size), elem_size, swp, args);
	__min_heap_sift_up_inline(heap, elem_size, idx, func, args);
	__min_heap_sift_down_inline(heap, idx, elem_size, func, args);

	return true;
}

#define min_heap_del_inline(_heap, _idx, _func, _args)	\
	__min_heap_del_inline(container_of(&(_heap)->nr, min_heap_char, nr),	\
			      __minheap_obj_size(_heap), _idx, _func, _args)

void __min_heap_init(min_heap_char *heap, void *data, int size);
void *__min_heap_peek(struct min_heap_char *heap);
bool __min_heap_full(min_heap_char *heap);
void __min_heap_sift_down(min_heap_char *heap, int pos, size_t elem_size,
			  const struct min_heap_callbacks *func, void *args);
void __min_heap_sift_up(min_heap_char *heap, size_t elem_size, size_t idx,
			const struct min_heap_callbacks *func, void *args);
void __min_heapify_all(min_heap_char *heap, size_t elem_size,
		       const struct min_heap_callbacks *func, void *args);
bool __min_heap_pop(min_heap_char *heap, size_t elem_size,
		    const struct min_heap_callbacks *func, void *args);
void __min_heap_pop_push(min_heap_char *heap, const void *element, size_t elem_size,
			 const struct min_heap_callbacks *func, void *args);
bool __min_heap_push(min_heap_char *heap, const void *element, size_t elem_size,
		     const struct min_heap_callbacks *func, void *args);
bool __min_heap_del(min_heap_char *heap, size_t elem_size, size_t idx,
		    const struct min_heap_callbacks *func, void *args);

#define min_heap_init(_heap, _data, _size)	\
	__min_heap_init(container_of(&(_heap)->nr, min_heap_char, nr), _data, _size)
#define min_heap_peek(_heap)	\
	(__minheap_cast(_heap) __min_heap_peek(container_of(&(_heap)->nr, min_heap_char, nr)))
#define min_heap_full(_heap)	\
	__min_heap_full(container_of(&(_heap)->nr, min_heap_char, nr))
#define min_heap_sift_down(_heap, _pos, _func, _args)	\
	__min_heap_sift_down(container_of(&(_heap)->nr, min_heap_char, nr), _pos,	\
			     __minheap_obj_size(_heap), _func, _args)
#define min_heap_sift_up(_heap, _idx, _func, _args)	\
	__min_heap_sift_up(container_of(&(_heap)->nr, min_heap_char, nr),	\
			   __minheap_obj_size(_heap), _idx, _func, _args)
#define min_heapify_all(_heap, _func, _args)	\
	__min_heapify_all(container_of(&(_heap)->nr, min_heap_char, nr),	\
			  __minheap_obj_size(_heap), _func, _args)
#define min_heap_pop(_heap, _func, _args)	\
	__min_heap_pop(container_of(&(_heap)->nr, min_heap_char, nr),	\
		       __minheap_obj_size(_heap), _func, _args)
#define min_heap_pop_push(_heap, _element, _func, _args)	\
	__min_heap_pop_push(container_of(&(_heap)->nr, min_heap_char, nr), _element,	\
			    __minheap_obj_size(_heap), _func, _args)
#define min_heap_push(_heap, _element, _func, _args)	\
	__min_heap_push(container_of(&(_heap)->nr, min_heap_char, nr), _element,	\
			__minheap_obj_size(_heap), _func, _args)
#define min_heap_del(_heap, _idx, _func, _args)	\
	__min_heap_del(container_of(&(_heap)->nr, min_heap_char, nr),	\
		       __minheap_obj_size(_heap), _idx, _func, _args)

#endif /* _LINUX_MIN_HEAP_H */
