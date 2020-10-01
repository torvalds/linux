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
#include <linux/dma-fence.h>
#include <drm/drm_print.h>

#define TTM_MAX_BO_PRIORITY	4U

struct ttm_bo_device;
struct ttm_resource_manager;
struct ttm_resource;
struct ttm_place;
struct ttm_buffer_object;

struct ttm_resource_manager_func {
	/**
	 * struct ttm_resource_manager_func member alloc
	 *
	 * @man: Pointer to a memory type manager.
	 * @bo: Pointer to the buffer object we're allocating space for.
	 * @placement: Placement details.
	 * @flags: Additional placement flags.
	 * @mem: Pointer to a struct ttm_resource to be filled in.
	 *
	 * This function should allocate space in the memory type managed
	 * by @man. Placement details if
	 * applicable are given by @placement. If successful,
	 * @mem::mm_node should be set to a non-null value, and
	 * @mem::start should be set to a value identifying the beginning
	 * of the range allocated, and the function should return zero.
	 * If the memory region accommodate the buffer object, @mem::mm_node
	 * should be set to NULL, and the function should return 0.
	 * If a system error occurred, preventing the request to be fulfilled,
	 * the function should return a negative error code.
	 *
	 * Note that @mem::mm_node will only be dereferenced by
	 * struct ttm_resource_manager functions and optionally by the driver,
	 * which has knowledge of the underlying type.
	 *
	 * This function may not be called from within atomic context, so
	 * an implementation can and must use either a mutex or a spinlock to
	 * protect any data structures managing the space.
	 */
	int  (*alloc)(struct ttm_resource_manager *man,
		      struct ttm_buffer_object *bo,
		      const struct ttm_place *place,
		      struct ttm_resource *mem);

	/**
	 * struct ttm_resource_manager_func member free
	 *
	 * @man: Pointer to a memory type manager.
	 * @mem: Pointer to a struct ttm_resource to be filled in.
	 *
	 * This function frees memory type resources previously allocated
	 * and that are identified by @mem::mm_node and @mem::start. May not
	 * be called from within atomic context.
	 */
	void (*free)(struct ttm_resource_manager *man,
		     struct ttm_resource *mem);

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
	void		*addr;
	phys_addr_t	offset;
	bool		is_iomem;
};

/**
 * struct ttm_resource
 *
 * @mm_node: Memory manager node.
 * @size: Requested size of memory region.
 * @num_pages: Actual size of memory region in pages.
 * @page_alignment: Page alignment.
 * @placement: Placement flags.
 * @bus: Placement on io bus accessible to the CPU
 *
 * Structure indicating the placement and space resources used by a
 * buffer object.
 */
struct ttm_resource {
	void *mm_node;
	unsigned long start;
	unsigned long size;
	unsigned long num_pages;
	uint32_t page_alignment;
	uint32_t mem_type;
	uint32_t placement;
	struct ttm_bus_placement bus;
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

int ttm_resource_alloc(struct ttm_buffer_object *bo,
		       const struct ttm_place *place,
		       struct ttm_resource *res);
void ttm_resource_free(struct ttm_buffer_object *bo, struct ttm_resource *res);

void ttm_resource_manager_init(struct ttm_resource_manager *man,
			       unsigned long p_size);

int ttm_resource_manager_evict_all(struct ttm_bo_device *bdev,
				   struct ttm_resource_manager *man);

void ttm_resource_manager_debug(struct ttm_resource_manager *man,
				struct drm_printer *p);

#endif
