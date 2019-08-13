/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2019, The Linux Foundation. All rights reserved.
 */

#ifndef _ION_KERNEL_H
#define _ION_KERNEL_H

#include <linux/dma-buf.h>
#include <linux/err.h>
#include <linux/device.h>
#include <linux/dma-direction.h>
#include <linux/kref.h>
#include <linux/mm_types.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/rbtree.h>
#include <linux/sched.h>
#include <linux/shrinker.h>
#include <linux/types.h>
#include <uapi/ion.h>

/**
 * struct ion_buffer - metadata for a particular buffer
 * @node:		node in the ion_device buffers tree
 * @list:		element in list of deferred freeable buffers
 * @heap:		back pointer to the heap the buffer came from
 * @flags:		buffer specific flags
 * @private_flags:	internal buffer specific flags
 * @size:		size of the buffer
 * @priv_virt:		private data to the buffer representable as
 *			a void *
 * @lock:		protects the buffers cnt fields
 * @kmap_cnt:		number of times the buffer is mapped to the kernel
 * @vaddr:		the kernel mapping if kmap_cnt is not zero
 * @sg_table:		the sg table for the buffer
 * @attachments:	list of devices attached to this buffer
 */
struct ion_buffer {
	union {
		struct rb_node node;
		struct list_head list;
	};
	struct ion_heap *heap;
	unsigned long flags;
	unsigned long private_flags;
	size_t size;
	void *priv_virt;
	struct mutex lock;
	int kmap_cnt;
	void *vaddr;
	struct sg_table *sg_table;
	struct list_head attachments;
};

/**
 * struct ion_heap_ops - ops to operate on a given heap
 * @allocate:		allocate memory
 * @free:		free memory
 * @map_kernel		map memory to the kernel
 * @unmap_kernel	unmap memory to the kernel
 * @map_user		map memory to userspace
 *
 * allocate, phys, and map_user return 0 on success, -errno on error.
 * map_dma and map_kernel return pointer on success, ERR_PTR on
 * error. @free will be called with ION_PRIV_FLAG_SHRINKER_FREE set in
 * the buffer's private_flags when called from a shrinker. In that
 * case, the pages being free'd must be truly free'd back to the
 * system, not put in a page pool or otherwise cached.
 */
struct ion_heap_ops {
	int (*allocate)(struct ion_heap *heap,
			struct ion_buffer *buffer, unsigned long len,
			unsigned long flags);
	void (*free)(struct ion_buffer *buffer);
	void * (*map_kernel)(struct ion_heap *heap, struct ion_buffer *buffer);
	void (*unmap_kernel)(struct ion_heap *heap, struct ion_buffer *buffer);
	int (*map_user)(struct ion_heap *mapper, struct ion_buffer *buffer,
			struct vm_area_struct *vma);
	int (*shrink)(struct ion_heap *heap, gfp_t gfp_mask, int nr_to_scan);
};

/**
 * heap flags - flags between the heaps and core ion code
 */
#define ION_HEAP_FLAG_DEFER_FREE BIT(0)

/**
 * private flags - flags internal to ion
 */
/*
 * Buffer is being freed from a shrinker function. Skip any possible
 * heap-specific caching mechanism (e.g. page pools). Guarantees that
 * any buffer storage that came from the system allocator will be
 * returned to the system allocator.
 */
#define ION_PRIV_FLAG_SHRINKER_FREE BIT(0)

/**
 * struct ion_heap - represents a heap in the system
 * @node:		rb node to put the heap on the device's tree of heaps
 * @type:		type of heap
 * @ops:		ops struct as above
 * @flags:		flags
 * @id:			id of heap, also indicates priority of this heap when
 *			allocating.  These are specified by platform data and
 *			MUST be unique
 * @name:		used for debugging
 * @owner:		kernel module that implements this heap
 * @shrinker:		a shrinker for the heap
 * @free_list:		free list head if deferred free is used
 * @free_list_size	size of the deferred free list in bytes
 * @lock:		protects the free list
 * @waitqueue:		queue to wait on from deferred free thread
 * @task:		task struct of deferred free thread
 * @num_of_buffers	the number of currently allocated buffers
 * @num_of_alloc_bytes	the number of allocated bytes
 * @alloc_bytes_wm	the number of allocated bytes watermark
 *
 * Represents a pool of memory from which buffers can be made.  In some
 * systems the only heap is regular system memory allocated via vmalloc.
 * On others, some blocks might require large physically contiguous buffers
 * that are allocated from a specially reserved heap.
 */
struct ion_heap {
	struct plist_node node;
	enum ion_heap_type type;
	struct ion_heap_ops *ops;
	unsigned long flags;
	unsigned int id;
	const char *name;
	struct module *owner;

	/* deferred free support */
	struct shrinker shrinker;
	struct list_head free_list;
	size_t free_list_size;
	spinlock_t free_lock;
	wait_queue_head_t waitqueue;
	struct task_struct *task;

	/* heap statistics */
	u64 num_of_buffers;
	u64 num_of_alloc_bytes;
	u64 alloc_bytes_wm;

	/* protect heap statistics */
	spinlock_t stat_lock;

	/* heap's debugfs root */
	struct dentry *debugfs_dir;
};

#define ion_device_add_heap(heap) __ion_device_add_heap(heap, THIS_MODULE)

#ifdef CONFIG_ION

/**
 * __ion_device_add_heap - adds a heap to the ion device
 *
 * @heap:               the heap to add
 *
 * Returns 0 on success, negative error otherwise.
 */
int __ion_device_add_heap(struct ion_heap *heap, struct module *owner);

/**
 * ion_device_remove_heap - removes a heap from ion device
 *
 * @heap:		pointer to the heap to be removed
 */
void ion_device_remove_heap(struct ion_heap *heap);

/**
 * ion_heap_init_shrinker
 * @heap:		the heap
 *
 * If a heap sets the ION_HEAP_FLAG_DEFER_FREE flag or defines the shrink op
 * this function will be called to setup a shrinker to shrink the freelists
 * and call the heap's shrink op.
 */
int ion_heap_init_shrinker(struct ion_heap *heap);

/**
 * ion_heap_init_deferred_free -- initialize deferred free functionality
 * @heap:		the heap
 *
 * If a heap sets the ION_HEAP_FLAG_DEFER_FREE flag this function will
 * be called to setup deferred frees. Calls to free the buffer will
 * return immediately and the actual free will occur some time later
 */
int ion_heap_init_deferred_free(struct ion_heap *heap);

/**
 * ion_heap_freelist_add - add a buffer to the deferred free list
 * @heap:		the heap
 * @buffer:		the buffer
 *
 * Adds an item to the deferred freelist.
 */
void ion_heap_freelist_add(struct ion_heap *heap, struct ion_buffer *buffer);

/**
 * ion_heap_freelist_drain - drain the deferred free list
 * @heap:		the heap
 * @size:		amount of memory to drain in bytes
 *
 * Drains the indicated amount of memory from the deferred freelist immediately.
 * Returns the total amount freed.  The total freed may be higher depending
 * on the size of the items in the list, or lower if there is insufficient
 * total memory on the freelist.
 */
size_t ion_heap_freelist_drain(struct ion_heap *heap, size_t size);

/**
 * ion_heap_freelist_shrink - drain the deferred free
 *				list, skipping any heap-specific
 *				pooling or caching mechanisms
 *
 * @heap:		the heap
 * @size:		amount of memory to drain in bytes
 *
 * Drains the indicated amount of memory from the deferred freelist immediately.
 * Returns the total amount freed.  The total freed may be higher depending
 * on the size of the items in the list, or lower if there is insufficient
 * total memory on the freelist.
 *
 * Unlike with @ion_heap_freelist_drain, don't put any pages back into
 * page pools or otherwise cache the pages. Everything must be
 * genuinely free'd back to the system. If you're free'ing from a
 * shrinker you probably want to use this. Note that this relies on
 * the heap.ops.free callback honoring the ION_PRIV_FLAG_SHRINKER_FREE
 * flag.
 */
size_t ion_heap_freelist_shrink(struct ion_heap *heap,
				size_t size);

/**
 * ion_heap_freelist_size - returns the size of the freelist in bytes
 * @heap:		the heap
 */
size_t ion_heap_freelist_size(struct ion_heap *heap);

/**
 * ion_heap_map_kernel - map the ion_buffer in kernel virtual address space.
 *
 * @heap:               the heap
 * @buffer:             buffer to be mapped
 *
 * Maps the buffer using vmap(). The function respects cache flags for the
 * buffer and creates the page table entries accordingly. Returns virtual
 * address at the beginning of the buffer or ERR_PTR.
 */
void *ion_heap_map_kernel(struct ion_heap *heap, struct ion_buffer *buffer);

/**
 * ion_heap_unmap_kernel - unmap ion_buffer
 *
 * @buffer:             buffer to be unmapped
 *
 * ION wrapper for vunmap() of the ion buffer.
 */
void ion_heap_unmap_kernel(struct ion_heap *heap, struct ion_buffer *buffer);

/**
 * ion_heap_map_user - map given ion buffer in provided vma
 *
 * @heap:               the heap this buffer belongs to
 * @buffer:             Ion buffer to be mapped
 * @vma:                vma of the process where buffer should be mapped.
 *
 * Maps the buffer using remap_pfn_range() into specific process's vma starting
 * with vma->vm_start. The vma size is expected to be >= ion buffer size.
 * If not, a partial buffer mapping may be created. Returns 0 on success.
 */
int ion_heap_map_user(struct ion_heap *heap, struct ion_buffer *buffer,
		      struct vm_area_struct *vma);

/* ion_buffer_zero - zeroes out an ion buffer respecting the ION_FLAGs.
 *
 * @buffer:		ion_buffer to zero
 *
 * Returns 0 on success, negative error otherwise.
 */
int ion_buffer_zero(struct ion_buffer *buffer);

/**
 * ion_alloc - Allocates an ion buffer of given size from given heap
 *
 * @len:               size of the buffer to be allocated.
 * @heap_id_mask:      a bitwise maks of heap ids to allocate from
 * @flags:             ION_BUFFER_XXXX flags for the new buffer.
 *
 * The function exports a dma_buf object for the new ion buffer internally
 * and returns that to the caller. So, the buffer is ready to be used by other
 * drivers immediately. Returns ERR_PTR in case of failure.
 */
struct dma_buf *ion_alloc(size_t len, unsigned int heap_id_mask,
			  unsigned int flags);

#else

static inline int __ion_device_add_heap(struct ion_heap *heap,
				      struct module *owner)
{
	return -ENODEV;
}

static inline int ion_heap_init_shrinker(struct ion_heap *heap)
{
	return -ENODEV;
}

static inline int ion_heap_init_deferred_free(struct ion_heap *heap)
{
	return -ENODEV;
}

static inline void ion_heap_freelist_add(struct ion_heap *heap,
					 struct ion_buffer *buffer) {}

static inline size_t ion_heap_freelist_drain(struct ion_heap *heap, size_t size)
{
	return -ENODEV;
}

static inline size_t ion_heap_freelist_shrink(struct ion_heap *heap,
					      size_t size)
{
	return -ENODEV;
}

static inline size_t ion_heap_freelist_size(struct ion_heap *heap)
{
	return -ENODEV;
}

static inline void *ion_heap_map_kernel(struct ion_heap *heap,
					struct ion_buffer *buffer)
{
	return ERR_PTR(-ENODEV);
}

static inline void ion_heap_unmap_kernel(struct ion_heap *heap,
					 struct ion_buffer *buffer) {}

static inline int ion_heap_map_user(struct ion_heap *heap,
				    struct ion_buffer *buffer,
				    struct vm_area_struct *vma)
{
	return -ENODEV;
}

static inline int ion_buffer_zero(struct ion_buffer *buffer)
{
	return -EINVAL;
}

static inline struct dma_buf *ion_alloc(size_t len, unsigned int heap_id_mask,
					unsigned int flags)
{
	return ERR_PTR(-ENOMEM);
}

#endif /* CONFIG_ION */
#endif /* _ION_KERNEL_H */
