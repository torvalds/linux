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
	bool (*less)(const void *lhs, const void *rhs);
	void (*swp)(void *lhs, void *rhs);
};

/* Initialize a min-heap. */
static __always_inline
void __min_heap_init(min_heap_char *heap, void *data, int size)
{
	heap->nr = 0;
	heap->size = size;
	if (data)
		heap->data = data;
	else
		heap->data = heap->preallocated;
}

#define min_heap_init(_heap, _data, _size)	\
	__min_heap_init((min_heap_char *)_heap, _data, _size)

/* Sift the element at pos down the heap. */
static __always_inline
void __min_heapify(min_heap_char *heap, int pos, size_t elem_size,
		const struct min_heap_callbacks *func)
{
	void *left, *right;
	void *data = heap->data;
	void *root = data + pos * elem_size;
	int i = pos, j;

	/* Find the sift-down path all the way to the leaves. */
	for (;;) {
		if (i * 2 + 2 >= heap->nr)
			break;
		left = data + (i * 2 + 1) * elem_size;
		right = data + (i * 2 + 2) * elem_size;
		i = func->less(left, right) ? i * 2 + 1 : i * 2 + 2;
	}

	/* Special case for the last leaf with no sibling. */
	if (i * 2 + 2 == heap->nr)
		i = i * 2 + 1;

	/* Backtrack to the correct location. */
	while (i != pos && func->less(root, data + i * elem_size))
		i = (i - 1) / 2;

	/* Shift the element into its correct place. */
	j = i;
	while (i != pos) {
		i = (i - 1) / 2;
		func->swp(data + i * elem_size, data + j * elem_size);
	}
}

#define min_heapify(_heap, _pos, _func)	\
	__min_heapify((min_heap_char *)_heap, _pos, __minheap_obj_size(_heap), _func)

/* Floyd's approach to heapification that is O(nr). */
static __always_inline
void __min_heapify_all(min_heap_char *heap, size_t elem_size,
		const struct min_heap_callbacks *func)
{
	int i;

	for (i = heap->nr / 2 - 1; i >= 0; i--)
		__min_heapify(heap, i, elem_size, func);
}

#define min_heapify_all(_heap, _func)	\
	__min_heapify_all((min_heap_char *)_heap, __minheap_obj_size(_heap), _func)

/* Remove minimum element from the heap, O(log2(nr)). */
static __always_inline
void __min_heap_pop(min_heap_char *heap, size_t elem_size,
		const struct min_heap_callbacks *func)
{
	void *data = heap->data;

	if (WARN_ONCE(heap->nr <= 0, "Popping an empty heap"))
		return;

	/* Place last element at the root (position 0) and then sift down. */
	heap->nr--;
	memcpy(data, data + (heap->nr * elem_size), elem_size);
	__min_heapify(heap, 0, elem_size, func);
}

#define min_heap_pop(_heap, _func)	\
	__min_heap_pop((min_heap_char *)_heap, __minheap_obj_size(_heap), _func)

/*
 * Remove the minimum element and then push the given element. The
 * implementation performs 1 sift (O(log2(nr))) and is therefore more
 * efficient than a pop followed by a push that does 2.
 */
static __always_inline
void __min_heap_pop_push(min_heap_char *heap,
		const void *element, size_t elem_size,
		const struct min_heap_callbacks *func)
{
	memcpy(heap->data, element, elem_size);
	__min_heapify(heap, 0, elem_size, func);
}

#define min_heap_pop_push(_heap, _element, _func)	\
	__min_heap_pop_push((min_heap_char *)_heap, _element, __minheap_obj_size(_heap), _func)

/* Push an element on to the heap, O(log2(nr)). */
static __always_inline
void __min_heap_push(min_heap_char *heap, const void *element, size_t elem_size,
		const struct min_heap_callbacks *func)
{
	void *data = heap->data;
	void *child, *parent;
	int pos;

	if (WARN_ONCE(heap->nr >= heap->size, "Pushing on a full heap"))
		return;

	/* Place at the end of data. */
	pos = heap->nr;
	memcpy(data + (pos * elem_size), element, elem_size);
	heap->nr++;

	/* Sift child at pos up. */
	for (; pos > 0; pos = (pos - 1) / 2) {
		child = data + (pos * elem_size);
		parent = data + ((pos - 1) / 2) * elem_size;
		if (func->less(parent, child))
			break;
		func->swp(parent, child);
	}
}

#define min_heap_push(_heap, _element, _func)	\
	__min_heap_push((min_heap_char *)_heap, _element, __minheap_obj_size(_heap), _func)

#endif /* _LINUX_MIN_HEAP_H */
