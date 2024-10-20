/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_MIN_HEAP_H
#define _LINUX_MIN_HEAP_H

#include <linux/bug.h>
#include <linux/string.h>
#include <linux/types.h>

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
	__min_heap_init_inline((min_heap_char *)_heap, _data, _size)

/* Get the minimum element from the heap. */
static __always_inline
void *__min_heap_peek_inline(struct min_heap_char *heap)
{
	return heap->nr ? heap->data : NULL;
}

#define min_heap_peek_inline(_heap)	\
	(__minheap_cast(_heap) __min_heap_peek_inline((min_heap_char *)_heap))

/* Check if the heap is full. */
static __always_inline
bool __min_heap_full_inline(min_heap_char *heap)
{
	return heap->nr == heap->size;
}

#define min_heap_full_inline(_heap)	\
	__min_heap_full_inline((min_heap_char *)_heap)

/* Sift the element at pos down the heap. */
static __always_inline
void __min_heap_sift_down_inline(min_heap_char *heap, int pos, size_t elem_size,
				 const struct min_heap_callbacks *func, void *args)
{
	const unsigned long lsbit = elem_size & -elem_size;
	void *data = heap->data;
	/* pre-scale counters for performance */
	size_t a = pos * elem_size;
	size_t b, c, d;
	size_t n = heap->nr * elem_size;

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
		func->swp(data + b, data + c, args);
	}
}

#define min_heap_sift_down_inline(_heap, _pos, _func, _args)	\
	__min_heap_sift_down_inline((min_heap_char *)_heap, _pos, __minheap_obj_size(_heap),	\
				    _func, _args)

/* Sift up ith element from the heap, O(log2(nr)). */
static __always_inline
void __min_heap_sift_up_inline(min_heap_char *heap, size_t elem_size, size_t idx,
			       const struct min_heap_callbacks *func, void *args)
{
	const unsigned long lsbit = elem_size & -elem_size;
	void *data = heap->data;
	/* pre-scale counters for performance */
	size_t a = idx * elem_size, b;

	while (a) {
		b = parent(a, lsbit, elem_size);
		if (func->less(data + b, data + a, args))
			break;
		func->swp(data + a, data + b, args);
		a = b;
	}
}

#define min_heap_sift_up_inline(_heap, _idx, _func, _args)	\
	__min_heap_sift_up_inline((min_heap_char *)_heap, __minheap_obj_size(_heap), _idx,	\
				  _func, _args)

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
	__min_heapify_all_inline((min_heap_char *)_heap, __minheap_obj_size(_heap), _func, _args)

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
	__min_heap_pop_inline((min_heap_char *)_heap, __minheap_obj_size(_heap), _func, _args)

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
	__min_heap_pop_push_inline((min_heap_char *)_heap, _element, __minheap_obj_size(_heap),	\
				   _func, _args)

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
	__min_heap_push_inline((min_heap_char *)_heap, _element, __minheap_obj_size(_heap),	\
			       _func, _args)

/* Remove ith element from the heap, O(log2(nr)). */
static __always_inline
bool __min_heap_del_inline(min_heap_char *heap, size_t elem_size, size_t idx,
			   const struct min_heap_callbacks *func, void *args)
{
	void *data = heap->data;

	if (WARN_ONCE(heap->nr <= 0, "Popping an empty heap"))
		return false;

	/* Place last element at the root (position 0) and then sift down. */
	heap->nr--;
	if (idx == heap->nr)
		return true;
	func->swp(data + (idx * elem_size), data + (heap->nr * elem_size), args);
	__min_heap_sift_up_inline(heap, elem_size, idx, func, args);
	__min_heap_sift_down_inline(heap, idx, elem_size, func, args);

	return true;
}

#define min_heap_del_inline(_heap, _idx, _func, _args)	\
	__min_heap_del_inline((min_heap_char *)_heap, __minheap_obj_size(_heap), _idx,	\
			      _func, _args)

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
	__min_heap_init((min_heap_char *)_heap, _data, _size)
#define min_heap_peek(_heap)	\
	(__minheap_cast(_heap) __min_heap_peek((min_heap_char *)_heap))
#define min_heap_full(_heap)	\
	__min_heap_full((min_heap_char *)_heap)
#define min_heap_sift_down(_heap, _pos, _func, _args)	\
	__min_heap_sift_down((min_heap_char *)_heap, _pos, __minheap_obj_size(_heap), _func, _args)
#define min_heap_sift_up(_heap, _idx, _func, _args)	\
	__min_heap_sift_up((min_heap_char *)_heap, __minheap_obj_size(_heap), _idx, _func, _args)
#define min_heapify_all(_heap, _func, _args)	\
	__min_heapify_all((min_heap_char *)_heap, __minheap_obj_size(_heap), _func, _args)
#define min_heap_pop(_heap, _func, _args)	\
	__min_heap_pop((min_heap_char *)_heap, __minheap_obj_size(_heap), _func, _args)
#define min_heap_pop_push(_heap, _element, _func, _args)	\
	__min_heap_pop_push((min_heap_char *)_heap, _element, __minheap_obj_size(_heap),	\
			    _func, _args)
#define min_heap_push(_heap, _element, _func, _args)	\
	__min_heap_push((min_heap_char *)_heap, _element, __minheap_obj_size(_heap), _func, _args)
#define min_heap_del(_heap, _idx, _func, _args)	\
	__min_heap_del((min_heap_char *)_heap, __minheap_obj_size(_heap), _idx, _func, _args)

#endif /* _LINUX_MIN_HEAP_H */
