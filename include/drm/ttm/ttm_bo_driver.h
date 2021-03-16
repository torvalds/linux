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

#include <drm/ttm/ttm_device.h>

#include "ttm_bo_api.h"
#include "ttm_placement.h"
#include "ttm_tt.h"
#include "ttm_pool.h"

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

/**
 * ttm_bo_unmap_virtual
 *
 * @bo: tear down the virtual mappings for this BO
 */
void ttm_bo_unmap_virtual(struct ttm_buffer_object *bo);

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

static inline void
ttm_bo_move_to_lru_tail_unlocked(struct ttm_buffer_object *bo)
{
	spin_lock(&ttm_glob.lru_lock);
	ttm_bo_move_to_lru_tail(bo, &bo->mem, NULL);
	spin_unlock(&ttm_glob.lru_lock);
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
int ttm_mem_io_reserve(struct ttm_device *bdev,
		       struct ttm_resource *mem);
void ttm_mem_io_free(struct ttm_device *bdev,
		     struct ttm_resource *mem);

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
 * bo: ttm buffer object
 * res: ttm resource object
 * @tmp: Page protection flag for a normal, cached mapping.
 *
 * Utility function that returns the pgprot_t that should be used for
 * setting up a PTE with the caching model indicated by @c_state.
 */
pgprot_t ttm_io_prot(struct ttm_buffer_object *bo, struct ttm_resource *res,
		     pgprot_t tmp);

/**
 * ttm_bo_tt_bind
 *
 * Bind the object tt to a memory resource.
 */
int ttm_bo_tt_bind(struct ttm_buffer_object *bo, struct ttm_resource *mem);

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
int ttm_range_man_init(struct ttm_device *bdev,
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
int ttm_range_man_fini(struct ttm_device *bdev,
		       unsigned type);

#endif
