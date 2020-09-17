/**************************************************************************
 *
 * Copyright (c) 2006-2009 Vmware, Inc., Palo Alto, CA., USA
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS, AUTHORS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 **************************************************************************/
/*
 * Authors: Thomas Hellstrom <thellstrom-at-vmware-dot-com>
 */
#ifndef _TTM_BO_DRIVER_H_
#define _TTM_BO_DRIVER_H_

#include <drm/drm_mm.h>
#include <drm/drm_vma_manager.h>
#include <linux/workqueue.h>
#include <linux/fs.h>
#include <linux/spinlock.h>
#include <linux/dma-resv.h>

#include "ttm_bo_api.h"
#include "ttm_memory.h"
#include "ttm_module.h"
#include "ttm_placement.h"
#include "ttm_tt.h"

/**
 * struct ttm_bo_driver
 *
 * @create_ttm_backend_entry: Callback to create a struct ttm_backend.
 * @evict_flags: Callback to obtain placement flags when a buffer is evicted.
 * @move: Callback for a driver to hook in accelerated functions to
 * move a buffer.
 * If set to NULL, a potentially slow memcpy() move is used.
 */

struct ttm_bo_driver {
	/**
	 * ttm_tt_create
	 *
	 * @bo: The buffer object to create the ttm for.
	 * @page_flags: Page flags as identified by TTM_PAGE_FLAG_XX flags.
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
	int (*ttm_tt_populate)(struct ttm_bo_device *bdev,
			       struct ttm_tt *ttm,
			       struct ttm_operation_ctx *ctx);

	/**
	 * ttm_tt_unpopulate
	 *
	 * @ttm: The struct ttm_tt to contain the backing pages.
	 *
	 * Free all backing page
	 */
	void (*ttm_tt_unpopulate)(struct ttm_bo_device *bdev, struct ttm_tt *ttm);

	/**
	 * ttm_tt_bind
	 *
	 * @bdev: Pointer to a ttm device
	 * @ttm: Pointer to a struct ttm_tt.
	 * @bo_mem: Pointer to a struct ttm_resource describing the
	 * memory type and location for binding.
	 *
	 * Bind the backend pages into the aperture in the location
	 * indicated by @bo_mem. This function should be able to handle
	 * differences between aperture and system page sizes.
	 */
	int (*ttm_tt_bind)(struct ttm_bo_device *bdev, struct ttm_tt *ttm, struct ttm_resource *bo_mem);

	/**
	 * ttm_tt_unbind
	 *
	 * @bdev: Pointer to a ttm device
	 * @ttm: Pointer to a struct ttm_tt.
	 *
	 * Unbind previously bound backend pages. This function should be
	 * able to handle differences between aperture and system page sizes.
	 */
	void (*ttm_tt_unbind)(struct ttm_bo_device *bdev, struct ttm_tt *ttm);

	/**
	 * ttm_tt_destroy
	 *
	 * @bdev: Pointer to a ttm device
	 * @ttm: Pointer to a struct ttm_tt.
	 *
	 * Destroy the backend. This will be call back from ttm_tt_destroy so
	 * don't call ttm_tt_destroy from the callback or infinite loop.
	 */
	void (*ttm_tt_destroy)(struct ttm_bo_device *bdev, struct ttm_tt *ttm);

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
	 *
	 * Move a buffer between two memory regions.
	 */
	int (*move)(struct ttm_buffer_object *bo, bool evict,
		    struct ttm_operation_ctx *ctx,
		    struct ttm_resource *new_mem);

	/**
	 * struct ttm_bo_driver_member verify_access
	 *
	 * @bo: Pointer to a buffer object.
	 * @filp: Pointer to a struct file trying to access the object.
	 *
	 * Called from the map / write / read methods to verify that the
	 * caller is permitted to access the buffer object.
	 * This member may be set to NULL, which will refuse this kind of
	 * access for all buffer objects.
	 * This function should return 0 if access is granted, -EPERM otherwise.
	 */
	int (*verify_access)(struct ttm_buffer_object *bo,
			     struct file *filp);

	/**
	 * Hook to notify driver about a driver move so it
	 * can do tiling things and book-keeping.
	 *
	 * @evict: whether this move is evicting the buffer from the graphics
	 * address space
	 */
	void (*move_notify)(struct ttm_buffer_object *bo,
			    bool evict,
			    struct ttm_resource *new_mem);
	/* notify the driver we are taking a fault on this BO
	 * and have reserved it */
	int (*fault_reserve_notify)(struct ttm_buffer_object *bo);

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
	int (*io_mem_reserve)(struct ttm_bo_device *bdev,
			      struct ttm_resource *mem);
	void (*io_mem_free)(struct ttm_bo_device *bdev,
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
	 * struct ttm_bo_driver member del_from_lru_notify
	 *
	 * @bo: the buffer object deleted from lru
	 *
	 * notify driver that a BO was deleted from LRU.
	 */
	void (*del_from_lru_notify)(struct ttm_buffer_object *bo);

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
 * struct ttm_bo_global - Buffer object driver global data.
 *
 * @dummy_read_page: Pointer to a dummy page used for mapping requests
 * of unpopulated pages.
 * @shrink: A shrink callback object used for buffer object swap.
 * @device_list_mutex: Mutex protecting the device list.
 * This mutex is held while traversing the device list for pm options.
 * @lru_lock: Spinlock protecting the bo subsystem lru lists.
 * @device_list: List of buffer object devices.
 * @swap_lru: Lru list of buffer objects used for swapping.
 */

extern struct ttm_bo_global {

	/**
	 * Constant after init.
	 */

	struct kobject kobj;
	struct page *dummy_read_page;
	spinlock_t lru_lock;

	/**
	 * Protected by ttm_global_mutex.
	 */
	struct list_head device_list;

	/**
	 * Protected by the lru_lock.
	 */
	struct list_head swap_lru[TTM_MAX_BO_PRIORITY];

	/**
	 * Internal protection.
	 */
	atomic_t bo_count;
} ttm_bo_glob;


#define TTM_NUM_MEM_TYPES 8

/**
 * struct ttm_bo_device - Buffer object driver device-specific data.
 *
 * @driver: Pointer to a struct ttm_bo_driver struct setup by the driver.
 * @man: An array of resource_managers.
 * @vma_manager: Address space manager (pointer)
 * lru_lock: Spinlock that protects the buffer+device lru lists and
 * ddestroy lists.
 * @dev_mapping: A pointer to the struct address_space representing the
 * device address space.
 * @wq: Work queue structure for the delayed delete workqueue.
 * @no_retry: Don't retry allocation if it fails
 *
 */

struct ttm_bo_device {

	/*
	 * Constant after bo device init / atomic.
	 */
	struct list_head device_list;
	struct ttm_bo_driver *driver;
	/*
	 * access via ttm_manager_type.
	 */
	struct ttm_resource_manager sysman;
	struct ttm_resource_manager *man_drv[TTM_NUM_MEM_TYPES];
	/*
	 * Protected by internal locks.
	 */
	struct drm_vma_offset_manager *vma_manager;

	/*
	 * Protected by the global:lru lock.
	 */
	struct list_head ddestroy;

	/*
	 * Protected by load / firstopen / lastclose /unload sync.
	 */

	struct address_space *dev_mapping;

	/*
	 * Internal protection.
	 */

	struct delayed_work wq;

	bool need_dma32;

	bool no_retry;
};

static inline struct ttm_resource_manager *ttm_manager_type(struct ttm_bo_device *bdev,
							    int mem_type)
{
	return bdev->man_drv[mem_type];
}

static inline void ttm_set_driver_manager(struct ttm_bo_device *bdev,
					  int type,
					  struct ttm_resource_manager *manager)
{
	bdev->man_drv[type] = manager;
}

/**
 * struct ttm_lru_bulk_move_pos
 *
 * @first: first BO in the bulk move range
 * @last: last BO in the bulk move range
 *
 * Positions for a lru bulk move.
 */
struct ttm_lru_bulk_move_pos {
	struct ttm_buffer_object *first;
	struct ttm_buffer_object *last;
};

/**
 * struct ttm_lru_bulk_move
 *
 * @tt: first/last lru entry for BOs in the TT domain
 * @vram: first/last lru entry for BOs in the VRAM domain
 * @swap: first/last lru entry for BOs on the swap list
 *
 * Helper structure for bulk moves on the LRU list.
 */
struct ttm_lru_bulk_move {
	struct ttm_lru_bulk_move_pos tt[TTM_MAX_BO_PRIORITY];
	struct ttm_lru_bulk_move_pos vram[TTM_MAX_BO_PRIORITY];
	struct ttm_lru_bulk_move_pos swap[TTM_MAX_BO_PRIORITY];
};

/*
 * ttm_bo.c
 */

/**
 * ttm_bo_mem_space
 *
 * @bo: Pointer to a struct ttm_buffer_object. the data of which
 * we want to allocate space for.
 * @proposed_placement: Proposed new placement for the buffer object.
 * @mem: A struct ttm_resource.
 * @interruptible: Sleep interruptible when sliping.
 * @no_wait_gpu: Return immediately if the GPU is busy.
 *
 * Allocate memory space for the buffer object pointed to by @bo, using
 * the placement flags in @mem, potentially evicting other idle buffer objects.
 * This function may sleep while waiting for space to become available.
 * Returns:
 * -EBUSY: No space available (only if no_wait == 1).
 * -ENOMEM: Could not allocate memory for the buffer object, either due to
 * fragmentation or concurrent allocators.
 * -ERESTARTSYS: An interruptible sleep was interrupted by a signal.
 */
int ttm_bo_mem_space(struct ttm_buffer_object *bo,
		     struct ttm_placement *placement,
		     struct ttm_resource *mem,
		     struct ttm_operation_ctx *ctx);

int ttm_bo_device_release(struct ttm_bo_device *bdev);

/**
 * ttm_bo_device_init
 *
 * @bdev: A pointer to a struct ttm_bo_device to initialize.
 * @glob: A pointer to an initialized struct ttm_bo_global.
 * @driver: A pointer to a struct ttm_bo_driver set up by the caller.
 * @mapping: The address space to use for this bo.
 * @vma_manager: A pointer to a vma manager.
 * @file_page_offset: Offset into the device address space that is available
 * for buffer data. This ensures compatibility with other users of the
 * address space.
 *
 * Initializes a struct ttm_bo_device:
 * Returns:
 * !0: Failure.
 */
int ttm_bo_device_init(struct ttm_bo_device *bdev,
		       struct ttm_bo_driver *driver,
		       struct address_space *mapping,
		       struct drm_vma_offset_manager *vma_manager,
		       bool need_dma32);

/**
 * ttm_bo_unmap_virtual
 *
 * @bo: tear down the virtual mappings for this BO
 */
void ttm_bo_unmap_virtual(struct ttm_buffer_object *bo);

/**
 * ttm_bo_unmap_virtual
 *
 * @bo: tear down the virtual mappings for this BO
 *
 * The caller must take ttm_mem_io_lock before calling this function.
 */
void ttm_bo_unmap_virtual_locked(struct ttm_buffer_object *bo);

/**
 * ttm_bo_reserve:
 *
 * @bo: A pointer to a struct ttm_buffer_object.
 * @interruptible: Sleep interruptible if waiting.
 * @no_wait: Don't sleep while trying to reserve, rather return -EBUSY.
 * @ticket: ticket used to acquire the ww_mutex.
 *
 * Locks a buffer object for validation. (Or prevents other processes from
 * locking it for validation), while taking a number of measures to prevent
 * deadlocks.
 *
 * Returns:
 * -EDEADLK: The reservation may cause a deadlock.
 * Release all buffer reservations, wait for @bo to become unreserved and
 * try again.
 * -ERESTARTSYS: A wait for the buffer to become unreserved was interrupted by
 * a signal. Release all buffer reservations and return to user-space.
 * -EBUSY: The function needed to sleep, but @no_wait was true
 * -EALREADY: Bo already reserved using @ticket. This error code will only
 * be returned if @use_ticket is set to true.
 */
static inline int ttm_bo_reserve(struct ttm_buffer_object *bo,
				 bool interruptible, bool no_wait,
				 struct ww_acquire_ctx *ticket)
{
	int ret = 0;

	if (no_wait) {
		bool success;
		if (WARN_ON(ticket))
			return -EBUSY;

		success = dma_resv_trylock(bo->base.resv);
		return success ? 0 : -EBUSY;
	}

	if (interruptible)
		ret = dma_resv_lock_interruptible(bo->base.resv, ticket);
	else
		ret = dma_resv_lock(bo->base.resv, ticket);
	if (ret == -EINTR)
		return -ERESTARTSYS;
	return ret;
}

/**
 * ttm_bo_reserve_slowpath:
 * @bo: A pointer to a struct ttm_buffer_object.
 * @interruptible: Sleep interruptible if waiting.
 * @sequence: Set (@bo)->sequence to this value after lock
 *
 * This is called after ttm_bo_reserve returns -EAGAIN and we backed off
 * from all our other reservations. Because there are no other reservations
 * held by us, this function cannot deadlock any more.
 */
static inline int ttm_bo_reserve_slowpath(struct ttm_buffer_object *bo,
					  bool interruptible,
					  struct ww_acquire_ctx *ticket)
{
	if (interruptible) {
		int ret = dma_resv_lock_slow_interruptible(bo->base.resv,
							   ticket);
		if (ret == -EINTR)
			ret = -ERESTARTSYS;
		return ret;
	}
	dma_resv_lock_slow(bo->base.resv, ticket);
	return 0;
}

static inline void ttm_bo_move_to_lru_tail_unlocked(struct ttm_buffer_object *bo)
{
	spin_lock(&ttm_bo_glob.lru_lock);
	ttm_bo_move_to_lru_tail(bo, NULL);
	spin_unlock(&ttm_bo_glob.lru_lock);
}

static inline void ttm_bo_assign_mem(struct ttm_buffer_object *bo,
				     struct ttm_resource *new_mem)
{
	bo->mem = *new_mem;
	new_mem->mm_node = NULL;
}

/**
 * ttm_bo_move_null = assign memory for a buffer object.
 * @bo: The bo to assign the memory to
 * @new_mem: The memory to be assigned.
 *
 * Assign the memory from new_mem to the memory of the buffer object bo.
 */
static inline void ttm_bo_move_null(struct ttm_buffer_object *bo,
				    struct ttm_resource *new_mem)
{
	struct ttm_resource *old_mem = &bo->mem;

	WARN_ON(old_mem->mm_node != NULL);
	ttm_bo_assign_mem(bo, new_mem);
}

/**
 * ttm_bo_unreserve
 *
 * @bo: A pointer to a struct ttm_buffer_object.
 *
 * Unreserve a previous reservation of @bo.
 */
static inline void ttm_bo_unreserve(struct ttm_buffer_object *bo)
{
	ttm_bo_move_to_lru_tail_unlocked(bo);
	dma_resv_unlock(bo->base.resv);
}

/*
 * ttm_bo_util.c
 */

int ttm_mem_io_reserve(struct ttm_bo_device *bdev,
		       struct ttm_resource *mem);
void ttm_mem_io_free(struct ttm_bo_device *bdev,
		     struct ttm_resource *mem);
/**
 * ttm_bo_move_ttm
 *
 * @bo: A pointer to a struct ttm_buffer_object.
 * @interruptible: Sleep interruptible if waiting.
 * @no_wait_gpu: Return immediately if the GPU is busy.
 * @new_mem: struct ttm_resource indicating where to move.
 *
 * Optimized move function for a buffer object with both old and
 * new placement backed by a TTM. The function will, if successful,
 * free any old aperture space, and set (@new_mem)->mm_node to NULL,
 * and update the (@bo)->mem placement flags. If unsuccessful, the old
 * data remains untouched, and it's up to the caller to free the
 * memory space indicated by @new_mem.
 * Returns:
 * !0: Failure.
 */

int ttm_bo_move_ttm(struct ttm_buffer_object *bo,
		    struct ttm_operation_ctx *ctx,
		    struct ttm_resource *new_mem);

/**
 * ttm_bo_move_memcpy
 *
 * @bo: A pointer to a struct ttm_buffer_object.
 * @interruptible: Sleep interruptible if waiting.
 * @no_wait_gpu: Return immediately if the GPU is busy.
 * @new_mem: struct ttm_resource indicating where to move.
 *
 * Fallback move function for a mappable buffer object in mappable memory.
 * The function will, if successful,
 * free any old aperture space, and set (@new_mem)->mm_node to NULL,
 * and update the (@bo)->mem placement flags. If unsuccessful, the old
 * data remains untouched, and it's up to the caller to free the
 * memory space indicated by @new_mem.
 * Returns:
 * !0: Failure.
 */

int ttm_bo_move_memcpy(struct ttm_buffer_object *bo,
		       struct ttm_operation_ctx *ctx,
		       struct ttm_resource *new_mem);

/**
 * ttm_bo_free_old_node
 *
 * @bo: A pointer to a struct ttm_buffer_object.
 *
 * Utility function to free an old placement after a successful move.
 */
void ttm_bo_free_old_node(struct ttm_buffer_object *bo);

/**
 * ttm_bo_move_accel_cleanup.
 *
 * @bo: A pointer to a struct ttm_buffer_object.
 * @fence: A fence object that signals when moving is complete.
 * @evict: This is an evict move. Don't return until the buffer is idle.
 * @pipeline: evictions are to be pipelined.
 * @new_mem: struct ttm_resource indicating where to move.
 *
 * Accelerated move function to be called when an accelerated move
 * has been scheduled. The function will create a new temporary buffer object
 * representing the old placement, and put the sync object on both buffer
 * objects. After that the newly created buffer object is unref'd to be
 * destroyed when the move is complete. This will help pipeline
 * buffer moves.
 */
int ttm_bo_move_accel_cleanup(struct ttm_buffer_object *bo,
			      struct dma_fence *fence, bool evict,
			      bool pipeline,
			      struct ttm_resource *new_mem);

/**
 * ttm_bo_pipeline_gutting.
 *
 * @bo: A pointer to a struct ttm_buffer_object.
 *
 * Pipelined gutting a BO of its backing store.
 */
int ttm_bo_pipeline_gutting(struct ttm_buffer_object *bo);

/**
 * ttm_io_prot
 *
 * @c_state: Caching state.
 * @tmp: Page protection flag for a normal, cached mapping.
 *
 * Utility function that returns the pgprot_t that should be used for
 * setting up a PTE with the caching model indicated by @c_state.
 */
pgprot_t ttm_io_prot(uint32_t caching_flags, pgprot_t tmp);

/**
 * ttm_bo_tt_bind
 *
 * Bind the object tt to a memory resource.
 */
int ttm_bo_tt_bind(struct ttm_buffer_object *bo, struct ttm_resource *mem);

/**
 * ttm_bo_tt_bind
 *
 * Unbind the object tt from a memory resource.
 */
void ttm_bo_tt_unbind(struct ttm_buffer_object *bo);

/**
 * ttm_bo_tt_destroy.
 */
void ttm_bo_tt_destroy(struct ttm_buffer_object *bo);

/**
 * ttm_range_man_init
 *
 * @bdev: ttm device
 * @type: memory manager type
 * @use_tt: if the memory manager uses tt
 * @p_size: size of area to be managed in pages.
 *
 * Initialise a generic range manager for the selected memory type.
 * The range manager is installed for this device in the type slot.
 */
int ttm_range_man_init(struct ttm_bo_device *bdev,
		       unsigned type, bool use_tt,
		       unsigned long p_size);

/**
 * ttm_range_man_fini
 *
 * @bdev: ttm device
 * @type: memory manager type
 *
 * Remove the generic range manager from a slot and tear it down.
 */
int ttm_range_man_fini(struct ttm_bo_device *bdev,
		       unsigned type);

#endif
