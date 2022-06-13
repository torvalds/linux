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
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/iosys-map.h>
#include <linux/dma-fence.h>

#include <drm/drm_print.h>
#include <drm/ttm/ttm_caching.h>
#include <drm/ttm/ttm_kmap_iter.h>

#define TTM_MAX_BO_PRIORITY	4U
#define TTM_NUM_MEM_TYPES 8

struct ttm_device;
struct ttm_resource_manager;
struct ttm_resource;
struct ttm_place;
struct ttm_buffer_object;
struct ttm_placement;
struct iosys_map;
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
 * @use_tt: If a TT object should be used for the backing store.
 * @size: Size of the managed region.
 * @bdev: ttm device this manager belongs to
 * @func: structure pointer implementing the range manager. See above
 * @move_lock: lock for move fence
 * @move: The fence of the last pipelined move operation.
 * @lru: The lru list for this memory type.
 *
 * This structure is used to identify and manage memory types for a device.
 */
struct ttm_resource_manager {
	/*
	 * No protection. Constant from start.
	 */
	bool use_type;
	bool use_tt;
	struct ttm_device *bdev;
	uint64_t size;
	const struct ttm_resource_manager_func *func;
	spinlock_t move_lock;

	/*
	 * Protected by @move_lock.
	 */
	struct dma_fence *move;

	/*
	 * Protected by the bdev->lru_lock.
	 */
	struct list_head lru[TTM_MAX_BO_PRIORITY];

	/**
	 * @usage: How much of the resources are used, protected by the
	 * bdev->lru_lock.
	 */
	uint64_t usage;
};

/**
 * struct ttm_bus_placement
 *
 * @addr:		mapped virtual address
 * @offset:		physical addr
 * @is_iomem:		is this io memory ?
 * @caching:		See enum ttm_caching
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
 * @bo: weak reference to the BO, protected by ttm_device::lru_lock
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
	struct ttm_buffer_object *bo;

	/**
	 * @lru: Least recently used list, see &ttm_resource_manager.lru
	 */
	struct list_head lru;
};

/**
 * struct ttm_resource_cursor
 *
 * @priority: the current priority
 *
 * Cursor to iterate over the resources in a manager.
 */
struct ttm_resource_cursor {
	unsigned int priority;
};

/**
 * struct ttm_lru_bulk_move_pos
 *
 * @first: first res in the bulk move range
 * @last: last res in the bulk move range
 *
 * Range of resources for a lru bulk move.
 */
struct ttm_lru_bulk_move_pos {
	struct ttm_resource *first;
	struct ttm_resource *last;
};

/**
 * struct ttm_lru_bulk_move
 *
 * @pos: first/last lru entry for resources in the each domain/priority
 *
 * Container for the current bulk move state. Should be used with
 * ttm_lru_bulk_move_init() and ttm_bo_set_bulk_move().
 */
struct ttm_lru_bulk_move {
	struct ttm_lru_bulk_move_pos pos[TTM_NUM_MEM_TYPES][TTM_MAX_BO_PRIORITY];
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
	struct iosys_map dmap;
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

void ttm_lru_bulk_move_init(struct ttm_lru_bulk_move *bulk);
void ttm_lru_bulk_move_tail(struct ttm_lru_bulk_move *bulk);

void ttm_resource_add_bulk_move(struct ttm_resource *res,
				struct ttm_buffer_object *bo);
void ttm_resource_del_bulk_move(struct ttm_resource *res,
				struct ttm_buffer_object *bo);
void ttm_resource_move_to_lru_tail(struct ttm_resource *res);

void ttm_resource_init(struct ttm_buffer_object *bo,
                       const struct ttm_place *place,
                       struct ttm_resource *res);
void ttm_resource_fini(struct ttm_resource_manager *man,
		       struct ttm_resource *res);

int ttm_resource_alloc(struct ttm_buffer_object *bo,
		       const struct ttm_place *place,
		       struct ttm_resource **res);
void ttm_resource_free(struct ttm_buffer_object *bo, struct ttm_resource **res);
bool ttm_resource_compat(struct ttm_resource *res,
			 struct ttm_placement *placement);
void ttm_resource_set_bo(struct ttm_resource *res,
			 struct ttm_buffer_object *bo);

void ttm_resource_manager_init(struct ttm_resource_manager *man,
			       struct ttm_device *bdev,
			       uint64_t size);

int ttm_resource_manager_evict_all(struct ttm_device *bdev,
				   struct ttm_resource_manager *man);

uint64_t ttm_resource_manager_usage(struct ttm_resource_manager *man);
void ttm_resource_manager_debug(struct ttm_resource_manager *man,
				struct drm_printer *p);

struct ttm_resource *
ttm_resource_manager_first(struct ttm_resource_manager *man,
			   struct ttm_resource_cursor *cursor);
struct ttm_resource *
ttm_resource_manager_next(struct ttm_resource_manager *man,
			  struct ttm_resource_cursor *cursor,
			  struct ttm_resource *res);

/**
 * ttm_resource_manager_for_each_res - iterate over all resources
 * @man: the resource manager
 * @cursor: struct ttm_resource_cursor for the current position
 * @res: the current resource
 *
 * Iterate over all the evictable resources in a resource manager.
 */
#define ttm_resource_manager_for_each_res(man, cursor, res)		\
	for (res = ttm_resource_manager_first(man, cursor); res;	\
	     res = ttm_resource_manager_next(man, cursor, res))

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

void ttm_resource_manager_create_debugfs(struct ttm_resource_manager *man,
					 struct dentry * parent,
					 const char *name);
#endif
