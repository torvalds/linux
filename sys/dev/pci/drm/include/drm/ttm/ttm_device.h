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

#ifndef _TTM_DEVICE_H_
#define _TTM_DEVICE_H_

#include <linux/types.h>
#include <linux/workqueue.h>
#include <drm/ttm/ttm_resource.h>
#include <drm/ttm/ttm_pool.h>

struct ttm_device;
struct ttm_placement;
struct ttm_buffer_object;
struct ttm_operation_ctx;

/**
 * struct ttm_global - Buffer object driver global data.
 */
extern struct ttm_global {

	/**
	 * @dummy_read_page: Pointer to a dummy page used for mapping requests
	 * of unpopulated pages. Constant after init.
	 */
	struct vm_page *dummy_read_page;

	/**
	 * @device_list: List of buffer object devices. Protected by
	 * ttm_global_mutex.
	 */
	struct list_head device_list;

	/**
	 * @bo_count: Number of buffer objects allocated by devices.
	 */
	atomic_t bo_count;
} ttm_glob;

struct ttm_device_funcs {
	/**
	 * ttm_tt_create
	 *
	 * @bo: The buffer object to create the ttm for.
	 * @page_flags: Page flags as identified by TTM_TT_FLAG_XX flags.
	 *
	 * Create a struct ttm_tt to back data with system memory pages.
	 * No pages are actually allocated.
	 * Returns:
	 * NULL: Out of memory.
	 */
	struct ttm_tt *(*ttm_tt_create)(struct ttm_buffer_object *bo,
					uint32_t page_flags);

	/**
	 * ttm_tt_populate
	 *
	 * @ttm: The struct ttm_tt to contain the backing pages.
	 *
	 * Allocate all backing pages
	 * Returns:
	 * -ENOMEM: Out of memory.
	 */
	int (*ttm_tt_populate)(struct ttm_device *bdev,
			       struct ttm_tt *ttm,
			       struct ttm_operation_ctx *ctx);

	/**
	 * ttm_tt_unpopulate
	 *
	 * @ttm: The struct ttm_tt to contain the backing pages.
	 *
	 * Free all backing page
	 */
	void (*ttm_tt_unpopulate)(struct ttm_device *bdev,
				  struct ttm_tt *ttm);

	/**
	 * ttm_tt_destroy
	 *
	 * @bdev: Pointer to a ttm device
	 * @ttm: Pointer to a struct ttm_tt.
	 *
	 * Destroy the backend. This will be call back from ttm_tt_destroy so
	 * don't call ttm_tt_destroy from the callback or infinite loop.
	 */
	void (*ttm_tt_destroy)(struct ttm_device *bdev, struct ttm_tt *ttm);

	/**
	 * struct ttm_bo_driver member eviction_valuable
	 *
	 * @bo: the buffer object to be evicted
	 * @place: placement we need room for
	 *
	 * Check with the driver if it is valuable to evict a BO to make room
	 * for a certain placement.
	 */
	bool (*eviction_valuable)(struct ttm_buffer_object *bo,
				  const struct ttm_place *place);
	/**
	 * struct ttm_bo_driver member evict_flags:
	 *
	 * @bo: the buffer object to be evicted
	 *
	 * Return the bo flags for a buffer which is not mapped to the hardware.
	 * These will be placed in proposed_flags so that when the move is
	 * finished, they'll end up in bo->mem.flags
	 * This should not cause multihop evictions, and the core will warn
	 * if one is proposed.
	 */

	void (*evict_flags)(struct ttm_buffer_object *bo,
			    struct ttm_placement *placement);

	/**
	 * struct ttm_bo_driver member move:
	 *
	 * @bo: the buffer to move
	 * @evict: whether this motion is evicting the buffer from
	 * the graphics address space
	 * @ctx: context for this move with parameters
	 * @new_mem: the new memory region receiving the buffer
	 * @hop: placement for driver directed intermediate hop
	 *
	 * Move a buffer between two memory regions.
	 * Returns errno -EMULTIHOP if driver requests a hop
	 */
	int (*move)(struct ttm_buffer_object *bo, bool evict,
		    struct ttm_operation_ctx *ctx,
		    struct ttm_resource *new_mem,
		    struct ttm_place *hop);

	/**
	 * Hook to notify driver about a resource delete.
	 */
	void (*delete_mem_notify)(struct ttm_buffer_object *bo);

	/**
	 * notify the driver that we're about to swap out this bo
	 */
	void (*swap_notify)(struct ttm_buffer_object *bo);

	/**
	 * Driver callback on when mapping io memory (for bo_move_memcpy
	 * for instance). TTM will take care to call io_mem_free whenever
	 * the mapping is not use anymore. io_mem_reserve & io_mem_free
	 * are balanced.
	 */
	int (*io_mem_reserve)(struct ttm_device *bdev,
			      struct ttm_resource *mem);
	void (*io_mem_free)(struct ttm_device *bdev,
			    struct ttm_resource *mem);

	/**
	 * Return the pfn for a given page_offset inside the BO.
	 *
	 * @bo: the BO to look up the pfn for
	 * @page_offset: the offset to look up
	 */
	unsigned long (*io_mem_pfn)(struct ttm_buffer_object *bo,
				    unsigned long page_offset);

	/**
	 * Read/write memory buffers for ptrace access
	 *
	 * @bo: the BO to access
	 * @offset: the offset from the start of the BO
	 * @buf: pointer to source/destination buffer
	 * @len: number of bytes to copy
	 * @write: whether to read (0) from or write (non-0) to BO
	 *
	 * If successful, this function should return the number of
	 * bytes copied, -EIO otherwise. If the number of bytes
	 * returned is < len, the function may be called again with
	 * the remainder of the buffer to copy.
	 */
	int (*access_memory)(struct ttm_buffer_object *bo, unsigned long offset,
			     void *buf, int len, int write);

	/**
	 * Notify the driver that we're about to release a BO
	 *
	 * @bo: BO that is about to be released
	 *
	 * Gives the driver a chance to do any cleanup, including
	 * adding fences that may force a delayed delete
	 */
	void (*release_notify)(struct ttm_buffer_object *bo);
};

/**
 * struct ttm_device - Buffer object driver device-specific data.
 */
struct ttm_device {
	/**
	 * @device_list: Our entry in the global device list.
	 * Constant after bo device init
	 */
	struct list_head device_list;

	/**
	 * @funcs: Function table for the device.
	 * Constant after bo device init
	 */
	const struct ttm_device_funcs *funcs;

	/**
	 * @sysman: Resource manager for the system domain.
	 * Access via ttm_manager_type.
	 */
	struct ttm_resource_manager sysman;

	/**
	 * @man_drv: An array of resource_managers, one per resource type.
	 */
	struct ttm_resource_manager *man_drv[TTM_NUM_MEM_TYPES];

	bus_space_tag_t iot;
	bus_space_tag_t memt;
	bus_dma_tag_t dmat;

	/**
	 * @vma_manager: Address space manager for finding BOs to mmap.
	 */
	struct drm_vma_offset_manager *vma_manager;

	/**
	 * @pool: page pool for the device.
	 */
	struct ttm_pool pool;

	/**
	 * @lru_lock: Protection for the per manager LRU and ddestroy lists.
	 */
	spinlock_t lru_lock;

	/**
	 * @pinned: Buffer objects which are pinned and so not on any LRU list.
	 */
	struct list_head pinned;

	/**
	 * @dev_mapping: A pointer to the struct address_space for invalidating
	 * CPU mappings on buffer move. Protected by load/unload sync.
	 */
	struct address_space *dev_mapping;

	/**
	 * @wq: Work queue structure for the delayed delete workqueue.
	 */
	struct workqueue_struct *wq;
};

int ttm_global_swapout(struct ttm_operation_ctx *ctx, gfp_t gfp_flags);
int ttm_device_swapout(struct ttm_device *bdev, struct ttm_operation_ctx *ctx,
		       gfp_t gfp_flags);

static inline struct ttm_resource_manager *
ttm_manager_type(struct ttm_device *bdev, int mem_type)
{
#ifdef notyet
	BUILD_BUG_ON(__builtin_constant_p(mem_type)
		     && mem_type >= TTM_NUM_MEM_TYPES);
#endif
	return bdev->man_drv[mem_type];
}

static inline void ttm_set_driver_manager(struct ttm_device *bdev, int type,
					  struct ttm_resource_manager *manager)
{
#ifdef notyet
	BUILD_BUG_ON(__builtin_constant_p(type) && type >= TTM_NUM_MEM_TYPES);
#endif
	bdev->man_drv[type] = manager;
}

int ttm_device_init(struct ttm_device *bdev, const struct ttm_device_funcs *funcs,
		    struct device *dev, struct address_space *mapping,
		    struct drm_vma_offset_manager *vma_manager,
		    bool use_dma_alloc, bool use_dma32);
void ttm_device_fini(struct ttm_device *bdev);
void ttm_device_clear_dma_mappings(struct ttm_device *bdev);

#endif
