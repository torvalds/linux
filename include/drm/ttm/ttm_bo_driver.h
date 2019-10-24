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
#include <linux/reservation.h>

#include "ttm_bo_api.h"
#include "ttm_memory.h"
#include "ttm_module.h"
#include "ttm_placement.h"
#include "ttm_tt.h"

#define TTM_MAX_BO_PRIORITY	4U

#define TTM_MEMTYPE_FLAG_FIXED         (1 << 0)	/* Fixed (on-card) PCI memory */
#define TTM_MEMTYPE_FLAG_MAPPABLE      (1 << 1)	/* Memory mappable */
#define TTM_MEMTYPE_FLAG_CMA           (1 << 3)	/* Can't map aperture */

struct ttm_mem_type_manager;

struct ttm_mem_type_manager_func {
	/**
	 * struct ttm_mem_type_manager member init
	 *
	 * @man: Pointer to a memory type manager.
	 * @p_size: Implementation dependent, but typically the size of the
	 * range to be managed in pages.
	 *
	 * Called to initialize a private range manager. The function is
	 * expected to initialize the man::priv member.
	 * Returns 0 on success, negative error code on failure.
	 */
	int  (*init)(struct ttm_mem_type_manager *man, unsigned long p_size);

	/**
	 * struct ttm_mem_type_manager member takedown
	 *
	 * @man: Pointer to a memory type manager.
	 *
	 * Called to undo the setup done in init. All allocated resources
	 * should be freed.
	 */
	int  (*takedown)(struct ttm_mem_type_manager *man);

	/**
	 * struct ttm_mem_type_manager member get_node
	 *
	 * @man: Pointer to a memory type manager.
	 * @bo: Pointer to the buffer object we're allocating space for.
	 * @placement: Placement details.
	 * @flags: Additional placement flags.
	 * @mem: Pointer to a struct ttm_mem_reg to be filled in.
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
	 * struct ttm_mem_type_manager functions and optionally by the driver,
	 * which has knowledge of the underlying type.
	 *
	 * This function may not be called from within atomic context, so
	 * an implementation can and must use either a mutex or a spinlock to
	 * protect any data structures managing the space.
	 */
	int  (*get_node)(struct ttm_mem_type_manager *man,
			 struct ttm_buffer_object *bo,
			 const struct ttm_place *place,
			 struct ttm_mem_reg *mem);

	/**
	 * struct ttm_mem_type_manager member put_node
	 *
	 * @man: Pointer to a memory type manager.
	 * @mem: Pointer to a struct ttm_mem_reg to be filled in.
	 *
	 * This function frees memory type resources previously allocated
	 * and that are identified by @mem::mm_node and @mem::start. May not
	 * be called from within atomic context.
	 */
	void (*put_node)(struct ttm_mem_type_manager *man,
			 struct ttm_mem_reg *mem);

	/**
	 * struct ttm_mem_type_manager member debug
	 *
	 * @man: Pointer to a memory type manager.
	 * @printer: Prefix to be used in printout to identify the caller.
	 *
	 * This function is called to print out the state of the memory
	 * type manager to aid debugging of out-of-memory conditions.
	 * It may not be called from within atomic context.
	 */
	void (*debug)(struct ttm_mem_type_manager *man,
		      struct drm_printer *printer);
};

/**
 * struct ttm_mem_type_manager
 *
 * @has_type: The memory type has been initialized.
 * @use_type: The memory type is enabled.
 * @flags: TTM_MEMTYPE_XX flags identifying the traits of the memory
 * managed by this memory type.
 * @gpu_offset: If used, the GPU offset of the first managed page of
 * fixed memory or the first managed location in an aperture.
 * @size: Size of the managed region.
 * @available_caching: A mask of available caching types, TTM_PL_FLAG_XX,
 * as defined in ttm_placement_common.h
 * @default_caching: The default caching policy used for a buffer object
 * placed in this memory type if the user doesn't provide one.
 * @func: structure pointer implementing the range manager. See above
 * @priv: Driver private closure for @func.
 * @io_reserve_mutex: Mutex optionally protecting shared io_reserve structures
 * @use_io_reserve_lru: Use an lru list to try to unreserve io_mem_regions
 * reserved by the TTM vm system.
 * @io_reserve_lru: Optional lru list for unreserving io mem regions.
 * @io_reserve_fastpath: Only use bdev::driver::io_mem_reserve to obtain
 * @move_lock: lock for move fence
 * static information. bdev::driver::io_mem_free is never used.
 * @lru: The lru list for this memory type.
 * @move: The fence of the last pipelined move operation.
 *
 * This structure is used to identify and manage memory types for a device.
 * It's set up by the ttm_bo_driver::init_mem_type method.
 */



struct ttm_mem_type_manager {
	struct ttm_bo_device *bdev;

	/*
	 * No protection. Constant from start.
	 */

	bool has_type;
	bool use_type;
	uint32_t flags;
	uint64_t gpu_offset; /* GPU address space is independent of CPU word size */
	uint64_t size;
	uint32_t available_caching;
	uint32_t default_caching;
	const struct ttm_mem_type_manager_func *func;
	void *priv;
	struct mutex io_reserve_mutex;
	bool use_io_reserve_lru;
	bool io_reserve_fastpath;
	spinlock_t move_lock;

	/*
	 * Protected by @io_reserve_mutex:
	 */

	struct list_head io_reserve_lru;

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
 * struct ttm_bo_driver
 *
 * @create_ttm_backend_entry: Callback to create a struct ttm_backend.
 * @invalidate_caches: Callback to invalidate read caches when a buffer object
 * has been evicted.
 * @init_mem_type: Callback to initialize a struct ttm_mem_type_manager
 * structure.
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
	int (*ttm_tt_populate)(struct ttm_tt *ttm,
			struct ttm_operation_ctx *ctx);

	/**
	 * ttm_tt_unpopulate
	 *
	 * @ttm: The struct ttm_tt to contain the backing pages.
	 *
	 * Free all backing page
	 */
	void (*ttm_tt_unpopulate)(struct ttm_tt *ttm);

	/**
	 * struct ttm_bo_driver member invalidate_caches
	 *
	 * @bdev: the buffer object device.
	 * @flags: new placement of the rebound buffer object.
	 *
	 * A previosly evicted buffer has been rebound in a
	 * potentially new location. Tell the driver that it might
	 * consider invalidating read (texture) caches on the next command
	 * submission as a consequence.
	 */

	int (*invalidate_caches)(struct ttm_bo_device *bdev, uint32_t flags);
	int (*init_mem_type)(struct ttm_bo_device *bdev, uint32_t type,
			     struct ttm_mem_type_manager *man);

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
		    struct ttm_mem_reg *new_mem);

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
			    struct ttm_mem_reg *new_mem);
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
			      struct ttm_mem_reg *mem);
	void (*io_mem_free)(struct ttm_bo_device *bdev,
			    struct ttm_mem_reg *mem);

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
};

/**
 * struct ttm_bo_global - Buffer object driver global data.
 *
 * @mem_glob: Pointer to a struct ttm_mem_global object for accounting.
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
	struct ttm_mem_global *mem_glob;
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
 * @man: An array of mem_type_managers.
 * @vma_manager: Address space manager
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
	struct ttm_bo_global *glob;
	struct ttm_bo_driver *driver;
	struct ttm_mem_type_manager man[TTM_NUM_MEM_TYPES];

	/*
	 * Protected by internal locks.
	 */
	struct drm_vma_offset_manager vma_manager;

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

/**
 * ttm_flag_masked
 *
 * @old: Pointer to the result and original value.
 * @new: New value of bits.
 * @mask: Mask of bits to change.
 *
 * Convenience function to change a number of bits identified by a mask.
 */

static inline uint32_t
ttm_flag_masked(uint32_t *old, uint32_t new, uint32_t mask)
{
	*old ^= (*old ^ new) & mask;
	return *old;
}

/*
 * ttm_bo.c
 */

/**
 * ttm_mem_reg_is_pci
 *
 * @bdev: Pointer to a struct ttm_bo_device.
 * @mem: A valid struct ttm_mem_reg.
 *
 * Returns true if the memory described by @mem is PCI memory,
 * false otherwise.
 */
bool ttm_mem_reg_is_pci(struct ttm_bo_device *bdev, struct ttm_mem_reg *mem);

/**
 * ttm_bo_mem_space
 *
 * @bo: Pointer to a struct ttm_buffer_object. the data of which
 * we want to allocate space for.
 * @proposed_placement: Proposed new placement for the buffer object.
 * @mem: A struct ttm_mem_reg.
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
		     struct ttm_mem_reg *mem,
		     struct ttm_operation_ctx *ctx);

void ttm_bo_mem_put(struct ttm_buffer_object *bo, struct ttm_mem_reg *mem);
void ttm_bo_mem_put_locked(struct ttm_buffer_object *bo,
			   struct ttm_mem_reg *mem);

int ttm_bo_device_release(struct ttm_bo_device *bdev);

/**
 * ttm_bo_device_init
 *
 * @bdev: A pointer to a struct ttm_bo_device to initialize.
 * @glob: A pointer to an initialized struct ttm_bo_global.
 * @driver: A pointer to a struct ttm_bo_driver set up by the caller.
 * @mapping: The address space to use for this bo.
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

int ttm_mem_io_reserve_vm(struct ttm_buffer_object *bo);
void ttm_mem_io_free_vm(struct ttm_buffer_object *bo);
int ttm_mem_io_lock(struct ttm_mem_type_manager *man, bool interruptible);
void ttm_mem_io_unlock(struct ttm_mem_type_manager *man);

void ttm_bo_del_sub_from_lru(struct ttm_buffer_object *bo);
void ttm_bo_add_to_lru(struct ttm_buffer_object *bo);

/**
 * __ttm_bo_reserve:
 *
 * @bo: A pointer to a struct ttm_buffer_object.
 * @interruptible: Sleep interruptible if waiting.
 * @no_wait: Don't sleep while trying to reserve, rather return -EBUSY.
 * @ticket: ticket used to acquire the ww_mutex.
 *
 * Will not remove reserved buffers from the lru lists.
 * Otherwise identical to ttm_bo_reserve.
 *
 * Returns:
 * -EDEADLK: The reservation may cause a deadlock.
 * Release all buffer reservations, wait for @bo to become unreserved and
 * try again. (only if use_sequence == 1).
 * -ERESTARTSYS: A wait for the buffer to become unreserved was interrupted by
 * a signal. Release all buffer reservations and return to user-space.
 * -EBUSY: The function needed to sleep, but @no_wait was true
 * -EALREADY: Bo already reserved using @ticket. This error code will only
 * be returned if @use_ticket is set to true.
 */
static inline int __ttm_bo_reserve(struct ttm_buffer_object *bo,
				   bool interruptible, bool no_wait,
				   struct ww_acquire_ctx *ticket)
{
	int ret = 0;

	if (no_wait) {
		bool success;
		if (WARN_ON(ticket))
			return -EBUSY;

		success = reservation_object_trylock(bo->resv);
		return success ? 0 : -EBUSY;
	}

	if (interruptible)
		ret = reservation_object_lock_interruptible(bo->resv, ticket);
	else
		ret = reservation_object_lock(bo->resv, ticket);
	if (ret == -EINTR)
		return -ERESTARTSYS;
	return ret;
}

/**
 * ttm_bo_reserve:
 *
 * @bo: A pointer to a struct ttm_buffer_object.
 * @interruptible: Sleep interruptible if waiting.
 * @no_wait: Don't sleep while trying to reserve, rather return -EBUSY.
 * @ticket: ticket used to acquire the ww_mutex.
 *
 * Locks a buffer object for validation. (Or prevents other processes from
 * locking it for validation) and removes it from lru lists, while taking
 * a number of measures to prevent deadlocks.
 *
 * Deadlocks may occur when two processes try to reserve multiple buffers in
 * different order, either by will or as a result of a buffer being evicted
 * to make room for a buffer already reserved. (Buffers are reserved before
 * they are evicted). The following algorithm prevents such deadlocks from
 * occurring:
 * Processes attempting to reserve multiple buffers other than for eviction,
 * (typically execbuf), should first obtain a unique 32-bit
 * validation sequence number,
 * and call this function with @use_ticket == 1 and @ticket->stamp == the unique
 * sequence number. If upon call of this function, the buffer object is already
 * reserved, the validation sequence is checked against the validation
 * sequence of the process currently reserving the buffer,
 * and if the current validation sequence is greater than that of the process
 * holding the reservation, the function returns -EDEADLK. Otherwise it sleeps
 * waiting for the buffer to become unreserved, after which it retries
 * reserving.
 * The caller should, when receiving an -EDEADLK error
 * release all its buffer reservations, wait for @bo to become unreserved, and
 * then rerun the validation with the same validation sequence. This procedure
 * will always guarantee that the process with the lowest validation sequence
 * will eventually succeed, preventing both deadlocks and starvation.
 *
 * Returns:
 * -EDEADLK: The reservation may cause a deadlock.
 * Release all buffer reservations, wait for @bo to become unreserved and
 * try again. (only if use_sequence == 1).
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
	int ret;

	WARN_ON(!kref_read(&bo->kref));

	ret = __ttm_bo_reserve(bo, interruptible, no_wait, ticket);
	if (likely(ret == 0))
		ttm_bo_del_sub_from_lru(bo);

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
	int ret = 0;

	WARN_ON(!kref_read(&bo->kref));

	if (interruptible)
		ret = ww_mutex_lock_slow_interruptible(&bo->resv->lock,
						       ticket);
	else
		ww_mutex_lock_slow(&bo->resv->lock, ticket);

	if (likely(ret == 0))
		ttm_bo_del_sub_from_lru(bo);
	else if (ret == -EINTR)
		ret = -ERESTARTSYS;

	return ret;
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
	spin_lock(&bo->bdev->glob->lru_lock);
	if (list_empty(&bo->lru))
		ttm_bo_add_to_lru(bo);
	else
		ttm_bo_move_to_lru_tail(bo, NULL);
	spin_unlock(&bo->bdev->glob->lru_lock);
	reservation_object_unlock(bo->resv);
}

/*
 * ttm_bo_util.c
 */

int ttm_mem_io_reserve(struct ttm_bo_device *bdev,
		       struct ttm_mem_reg *mem);
void ttm_mem_io_free(struct ttm_bo_device *bdev,
		     struct ttm_mem_reg *mem);
/**
 * ttm_bo_move_ttm
 *
 * @bo: A pointer to a struct ttm_buffer_object.
 * @interruptible: Sleep interruptible if waiting.
 * @no_wait_gpu: Return immediately if the GPU is busy.
 * @new_mem: struct ttm_mem_reg indicating where to move.
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
		    struct ttm_mem_reg *new_mem);

/**
 * ttm_bo_move_memcpy
 *
 * @bo: A pointer to a struct ttm_buffer_object.
 * @interruptible: Sleep interruptible if waiting.
 * @no_wait_gpu: Return immediately if the GPU is busy.
 * @new_mem: struct ttm_mem_reg indicating where to move.
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
		       struct ttm_mem_reg *new_mem);

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
 * @new_mem: struct ttm_mem_reg indicating where to move.
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
			      struct ttm_mem_reg *new_mem);

/**
 * ttm_bo_pipeline_move.
 *
 * @bo: A pointer to a struct ttm_buffer_object.
 * @fence: A fence object that signals when moving is complete.
 * @evict: This is an evict move. Don't return until the buffer is idle.
 * @new_mem: struct ttm_mem_reg indicating where to move.
 *
 * Function for pipelining accelerated moves. Either free the memory
 * immediately or hang it on a temporary buffer object.
 */
int ttm_bo_pipeline_move(struct ttm_buffer_object *bo,
			 struct dma_fence *fence, bool evict,
			 struct ttm_mem_reg *new_mem);

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

extern const struct ttm_mem_type_manager_func ttm_bo_manager_func;

#endif
