/*
 * Simple insertion-only static-sized priority heap containing
 * pointers, based on CLR, chapter 7
 */

#include <linux/slab.h>
#include <linux/prio_heap.h>

int heap_init(struct ptr_heap *heap, size_t size, gfp_t gfp_mask,
	      int (*gt)(void *, void *))
{
	heap->ptrs = kmalloc(size, gfp_mask);
	if (!heap->ptrs)
		return -ENOMEM;
	heap->size = 0;
	heap->max = size / sizeof(void *);
	heap->gt = gt;
	return 0;
}

void heap_free(struct ptr_heap *heap)
{
	kfree(heap->ptrs);
}

void *heap_insert(struct ptr_heap *heap, void *p)
{
	void *res;
	void **ptrs = heap->ptrs;
	int pos;

	if (heap->size < heap->max) {
		/* Heap insertion */
		int pos = heap->size++;
		while (pos > 0 && heap->gt(p, ptrs[(pos-1)/2])) {
			ptrs[pos] = ptrs[(pos-1)/2];
			pos = (pos-1)/2;
		}
		ptrs[pos] = p;
		return NULL;
	}

	/* The heap is full, so something will have to be dropped */

	/* If the new pointer is greater than the current max, drop it */
	if (heap->gt(p, ptrs[0]))
		return p;

	/* Replace the current max and heapify */
	res = ptrs[0];
	ptrs[0] = p;
	pos = 0;

	while (1) {
		int left = 2 * pos + 1;
		int right = 2 * pos + 2;
		int largest = pos;
		if (left < heap->size && heap->gt(ptrs[left], p))
			largest = left;
		if (right < heap->size && heap->gt(ptrs[right], ptrs[largest]))
			largest = right;
		if (largest == pos)
			break;
		/* Push p down the heap one level and bump one up */
		ptrs[pos] = ptrs[largest];
		ptrs[largest] = p;
		pos = largest;
	}
	return res;
}
