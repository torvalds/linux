#ifndef _LINUX_PRIO_HEAP_H
#define _LINUX_PRIO_HEAP_H

/*
 * Simple insertion-only static-sized priority heap containing
 * pointers, based on CLR, chapter 7
 */

#include <linux/gfp.h>

/**
 * struct ptr_heap - simple static-sized priority heap
 * @ptrs - pointer to data area
 * @max - max number of elements that can be stored in @ptrs
 * @size - current number of valid elements in @ptrs (in the range 0..@size-1
 * @gt: comparison operator, which should implement "greater than"
 */
struct ptr_heap {
	void **ptrs;
	int max;
	int size;
	int (*gt)(void *, void *);
};

/**
 * heap_init - initialize an empty heap with a given memory size
 * @heap: the heap structure to be initialized
 * @size: amount of memory to use in bytes
 * @gfp_mask: mask to pass to kmalloc()
 * @gt: comparison operator, which should implement "greater than"
 */
extern int heap_init(struct ptr_heap *heap, size_t size, gfp_t gfp_mask,
		     int (*gt)(void *, void *));

/**
 * heap_free - release a heap's storage
 * @heap: the heap structure whose data should be released
 */
void heap_free(struct ptr_heap *heap);

/**
 * heap_insert - insert a value into the heap and return any overflowed value
 * @heap: the heap to be operated on
 * @p: the pointer to be inserted
 *
 * Attempts to insert the given value into the priority heap. If the
 * heap is full prior to the insertion, then the resulting heap will
 * consist of the smallest @max elements of the original heap and the
 * new element; the greatest element will be removed from the heap and
 * returned. Note that the returned element will be the new element
 * (i.e. no change to the heap) if the new element is greater than all
 * elements currently in the heap.
 */
extern void *heap_insert(struct ptr_heap *heap, void *p);



#endif /* _LINUX_PRIO_HEAP_H */
