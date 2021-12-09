/*
 * Copyright 2020 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: Christian König
 */

#ifndef _TTM_RESOURCE_H_
#define _TTM_RESOURCE_H_

#include <linux/types.h>
#include <linux/mutex.h>
#include <linux/dma-buf-map.h>
#include <linux/dma-fence.h>
#include <drm/drm_print.h>
#include <drm/ttm/ttm_caching.h>
#include <drm/ttm/ttm_kmap_iter.h>

#define TTM_MAX_BO_PRIORITY	4U

struct ttm_device;
struct ttm_resource_manager;
struct ttm_resource;
struct ttm_place;
struct ttm_buffer_object;
struct dma_buf_map;
struct io_mapping;
struct sg_table;
struct scatterlist;

struct ttm_resource_manager_func {
	/**
	 * struct ttm_resource_manager_func member alloc
	 *
	 * @man: Pointer to a memory type manager.
	 * @bo: Pointer to the buffer object we're allocating space for.
	 * @place: Placement details.
	 * @res: Resulting pointer to the ttm_resource.
	 *
	 * This function should allocate space in the memory type managed
	 * by @man. Placement details if applicable are given by @place. If
	 * successful, a filled in ttm_resource object should be returned in
	 * @res. @res::start should be set to a value identifying the beginning
	 * of the range allocated, and the function should return zero.
	 * If the manager can't fulfill the request -ENOSPC should be returned.
	 * If a system error occurred, preventing the request to be fulfilled,
	 * the function should return a negative error code.
	 *
	 * This function may not be called from within atomic context and needs
	 * to take care of its own locking to protect any data structures
	 * managing the space.
	 */
	int  (*alloc)(struct ttm_resource_manager *man,
		      struct ttm_buffer_object *bo,
		      const struct ttm_place *place,
		      struct ttm_resource **res);

	/**
	 * struct ttm_resource_manager_func member free
	 *
	 * @man: Pointer to a memory type manager.
	 * @res: Pointer to a struct ttm_resource to be freed.
	 *
	 * This function frees memory type resources previously allocated.
	 * May not be called from within atomic context.
	 */
	void (*free)(struct ttm_resource_manager *man,
		     struct ttm_resource *res);

	/**
	 * struct ttm_resource_manager_func member debug
	 *
	 * @man: Pointer to a memory type manager.
	 * @printer: Prefix to be used in printout to identify the caller.
	 *
	 * This function is called to print out the state of the memory
	 * type manager to aid debugging of out-of-memory conditions.
	 * It may not be called from within atomic context.
	 */
	void (*debug)(struct ttm_resource_manager *man,
		      struct drm_printer *printer);
};

/**
 * struct ttm_resource_manager
 *
 * @use_type: The memory type is enabled.
 * @flags: TTM_MEMTYPE_XX flags identifying the traits of the memory
 * managed by this memory type.
 * @gpu_offset: If used, the GPU offset of the first managed page of
 * fixed memory or the first managed location in an aperture.
 * @size: Size of the managed region.
 * @func: structure pointer implementing the range manager. See above
 * @move_lock: lock for move fence
 * static information. bdev::driver::io_mem_free is never used.
 * @lru: The lru list for this memory type.
 * @move: The fence of the last pipelined move operation.
 *
 * This structure is used to identify and manage memory types for a device.
 */
struct ttm_resource_manager {
	/*
	 * No protection. Constant from start.
	 */
	bool use_type;
	bool use_tt;
	uint64_t size;
	const struct ttm_resource_manager_func *func;
	spinlock_t move_lock;

	/*
	 * Protected by the global->lru_lock.
	 */

	struct list_head lru[TTM_MAX_BO_PRIORITY];

	/*
	 * Protected by @move_lock.
	 */
	struct dma_fence *move;
};

/**
 * struct ttm_bus_placement
 *
 * @addr:		mapped virtual address
 * @offset:		physical addr
 * @is_iomem:		is this io memory ?
 *
 * Structure indicating the bus placement of an object.
 */
struct ttm_bus_placement {
	void			*addr;
	phys_addr_t		offset;
	bool			is_iomem;
	enum ttm_caching	caching;
};

/**
 * struct ttm_resource
 *
 * @start: Start of the allocation.
 * @num_pages: Actual size of resource in pages.
 * @mem_type: Resource type of the allocation.
 * @placement: Placement flags.
 * @bus: Placement on io bus accessible to the CPU
 *
 * Structure indicating the placement and space resources used by a
 * buffer object.
 */
struct ttm_resource {
	unsigned long start;
	unsigned long num_pages;
	uint32_t mem_type;
	uint32_t placement;
	struct ttm_bus_placement bus;
};

/**
 * struct ttm_kmap_iter_iomap - Specialization for a struct io_mapping +
 * struct sg_table backed struct ttm_resource.
 * @base: Embedded struct ttm_kmap_iter providing the usage interface.
 * @iomap: struct io_mapping representing the underlying linear io_memory.
 * @st: sg_table into @iomap, representing the memory of the struct ttm_resource.
 * @start: Offset that needs to be subtracted from @st to make
 * sg_dma_address(st->sgl) - @start == 0 for @iomap start.
 * @cache: Scatterlist traversal cache for fast lookups.
 * @cache.sg: Pointer to the currently cached scatterlist segment.
 * @cache.i: First index of @sg. PAGE_SIZE granularity.
 * @cache.end: Last index + 1 of @sg. PAGE_SIZE granularity.
 * @cache.offs: First offset into @iomap of @sg. PAGE_SIZE granularity.
 */
struct ttm_kmap_iter_iomap {
	struct ttm_kmap_iter base;
	struct io_mapping *iomap;
	struct sg_table *st;
	resource_size_t start;
	struct {
		struct scatterlist *sg;
		pgoff_t i;
		pgoff_t end;
		pgoff_t offs;
	} cache;
};

/**
 * struct ttm_kmap_iter_linear_io - Iterator specialization for linear io
 * @base: The base iterator
 * @dmap: Points to the starting address of the region
 * @needs_unmap: Whether we need to unmap on fini
 */
struct ttm_kmap_iter_linear_io {
	struct ttm_kmap_iter base;
	struct dma_buf_map dmap;
	bool needs_unmap;
};

/**
 * ttm_resource_manager_set_used
 *
 * @man: A memory manager object.
 * @used: usage state to set.
 *
 * Set the manager in use flag. If disabled the manager is no longer
 * used for object placement.
 */
static inline void
ttm_resource_manager_set_used(struct ttm_resource_manager *man, bool used)
{
	int i;

	for (i = 0; i < TTM_MAX_BO_PRIORITY; i++)
		WARN_ON(!list_empty(&man->lru[i]));
	man->use_type = used;
}

/**
 * ttm_resource_manager_used
 *
 * @man: Manager to get used state for
 *
 * Get the in use flag for a manager.
 * Returns:
 * true is used, false if not.
 */
static inline bool ttm_resource_manager_used(struct ttm_resource_manager *man)
{
	return man->use_type;
}

/**
 * ttm_resource_manager_cleanup
 *
 * @man: A memory manager object.
 *
 * Cleanup the move fences from the memory manager object.
 */
static inline void
ttm_resource_manager_cleanup(struct ttm_resource_manager *man)
{
	dma_fence_put(man->move);
	man->move = NULL;
}

void ttm_resource_init(struct ttm_buffer_object *bo,
                       const struct ttm_place *place,
                       struct ttm_resource *res);
int ttm_resource_alloc(struct ttm_buffer_object *bo,
		       const struct ttm_place *place,
		       struct ttm_resource **res);
void ttm_resource_free(struct ttm_buffer_object *bo, struct ttm_resource **res);

void ttm_resource_manager_init(struct ttm_resource_manager *man,
			       unsigned long p_size);

int ttm_resource_manager_evict_all(struct ttm_device *bdev,
				   struct ttm_resource_manager *man);

void ttm_resource_manager_debug(struct ttm_resource_manager *man,
				struct drm_printer *p);

struct ttm_kmap_iter *
ttm_kmap_iter_iomap_init(struct ttm_kmap_iter_iomap *iter_io,
			 struct io_mapping *iomap,
			 struct sg_table *st,
			 resource_size_t start);

struct ttm_kmap_iter_linear_io;

struct ttm_kmap_iter *
ttm_kmap_iter_linear_io_init(struct ttm_kmap_iter_linear_io *iter_io,
			     struct ttm_device *bdev,
			     struct ttm_resource *mem);

void ttm_kmap_iter_linear_io_fini(struct ttm_kmap_iter_linear_io *iter_io,
				  struct ttm_device *bdev,
				  struct ttm_resource *mem);
#endif
