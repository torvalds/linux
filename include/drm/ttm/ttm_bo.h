/**************************************************************************
 *
 * Copyright (c) 2006-2009 VMware, Inc., Palo Alto, CA., USA
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

#ifndef _TTM_BO_API_H_
#define _TTM_BO_API_H_

#include <drm/drm_gem.h>

#include <linux/kref.h>
#include <linux/list.h>

#include "ttm_device.h"

/* Default number of pre-faulted pages in the TTM fault handler */
#define TTM_BO_VM_NUM_PREFAULT 16

struct iosys_map;

struct ttm_global;
struct ttm_device;
struct ttm_placement;
struct ttm_place;
struct ttm_resource;
struct ttm_resource_manager;
struct ttm_tt;

/**
 * enum ttm_bo_type
 *
 * @ttm_bo_type_device:	These are 'normal' buffers that can
 * be mmapped by user space. Each of these bos occupy a slot in the
 * device address space, that can be used for normal vm operations.
 *
 * @ttm_bo_type_kernel: These buffers are like ttm_bo_type_device buffers,
 * but they cannot be accessed from user-space. For kernel-only use.
 *
 * @ttm_bo_type_sg: Buffer made from dmabuf sg table shared with another
 * driver.
 */
enum ttm_bo_type {
	ttm_bo_type_device,
	ttm_bo_type_kernel,
	ttm_bo_type_sg
};

/**
 * struct ttm_buffer_object
 *
 * @base: drm_gem_object superclass data.
 * @bdev: Pointer to the buffer object device structure.
 * @type: The bo type.
 * @page_alignment: Page alignment.
 * @destroy: Destruction function. If NULL, kfree is used.
 * @kref: Reference count of this buffer object. When this refcount reaches
 * zero, the object is destroyed or put on the delayed delete list.
 * @resource: structure describing current placement.
 * @ttm: TTM structure holding system pages.
 * @deleted: True if the object is only a zombie and already deleted.
 * @bulk_move: The bulk move object.
 * @priority: Priority for LRU, BOs with lower priority are evicted first.
 * @pin_count: Pin count.
 *
 * Base class for TTM buffer object, that deals with data placement and CPU
 * mappings. GPU mappings are really up to the driver, but for simpler GPUs
 * the driver can usually use the placement offset @offset directly as the
 * GPU virtual address. For drivers implementing multiple
 * GPU memory manager contexts, the driver should manage the address space
 * in these contexts separately and use these objects to get the correct
 * placement and caching for these GPU maps. This makes it possible to use
 * these objects for even quite elaborate memory management schemes.
 * The destroy member, the API visibility of this object makes it possible
 * to derive driver specific types.
 */
struct ttm_buffer_object {
	struct drm_gem_object base;

	/*
	 * Members constant at init.
	 */
	struct ttm_device *bdev;
	enum ttm_bo_type type;
	uint32_t page_alignment;
	void (*destroy) (struct ttm_buffer_object *);

	/*
	* Members not needing protection.
	*/
	struct kref kref;

	/*
	 * Members protected by the bo::resv::reserved lock.
	 */
	struct ttm_resource *resource;
	struct ttm_tt *ttm;
	bool deleted;
	struct ttm_lru_bulk_move *bulk_move;
	unsigned priority;
	unsigned pin_count;

	/**
	 * @delayed_delete: Work item used when we can't delete the BO
	 * immediately
	 */
	struct work_struct delayed_delete;

	/**
	 * @sg: external source of pages and DMA addresses, protected by the
	 * reservation lock.
	 */
	struct sg_table *sg;
};

#define TTM_BO_MAP_IOMEM_MASK 0x80

/**
 * struct ttm_bo_kmap_obj
 *
 * @virtual: The current kernel virtual address.
 * @page: The page when kmap'ing a single page.
 * @bo_kmap_type: Type of bo_kmap.
 * @bo: The TTM BO.
 *
 * Object describing a kernel mapping. Since a TTM bo may be located
 * in various memory types with various caching policies, the
 * mapping can either be an ioremap, a vmap, a kmap or part of a
 * premapped region.
 */
struct ttm_bo_kmap_obj {
	void *virtual;
	struct page *page;
	enum {
		ttm_bo_map_iomap        = 1 | TTM_BO_MAP_IOMEM_MASK,
		ttm_bo_map_vmap         = 2,
		ttm_bo_map_kmap         = 3,
		ttm_bo_map_premapped    = 4 | TTM_BO_MAP_IOMEM_MASK,
	} bo_kmap_type;
	struct ttm_buffer_object *bo;
};

/**
 * struct ttm_operation_ctx
 *
 * @interruptible: Sleep interruptible if sleeping.
 * @no_wait_gpu: Return immediately if the GPU is busy.
 * @gfp_retry_mayfail: Set the __GFP_RETRY_MAYFAIL when allocation pages.
 * @allow_res_evict: Allow eviction of reserved BOs. Can be used when multiple
 * BOs share the same reservation object.
 * faults. Should only be used by TTM internally.
 * @resv: Reservation object to allow reserved evictions with.
 * @bytes_moved: Statistics on how many bytes have been moved.
 *
 * Context for TTM operations like changing buffer placement or general memory
 * allocation.
 */
struct ttm_operation_ctx {
	bool interruptible;
	bool no_wait_gpu;
	bool gfp_retry_mayfail;
	bool allow_res_evict;
	struct dma_resv *resv;
	uint64_t bytes_moved;
};

struct ttm_lru_walk;

/** struct ttm_lru_walk_ops - Operations for a LRU walk. */
struct ttm_lru_walk_ops {
	/**
	 * process_bo - Process this bo.
	 * @walk: struct ttm_lru_walk describing the walk.
	 * @bo: A locked and referenced buffer object.
	 *
	 * Return: Negative error code on error, User-defined positive value
	 * (typically, but not always, size of the processed bo) on success.
	 * On success, the returned values are summed by the walk and the
	 * walk exits when its target is met.
	 * 0 also indicates success, -EBUSY means this bo was skipped.
	 */
	s64 (*process_bo)(struct ttm_lru_walk *walk, struct ttm_buffer_object *bo);
};

/**
 * struct ttm_lru_walk_arg - Common part for the variants of BO LRU walk.
 */
struct ttm_lru_walk_arg {
	/** @ctx: Pointer to the struct ttm_operation_ctx. */
	struct ttm_operation_ctx *ctx;
	/** @ticket: The struct ww_acquire_ctx if any. */
	struct ww_acquire_ctx *ticket;
	/** @trylock_only: Only use trylock for locking. */
	bool trylock_only;
};

/**
 * struct ttm_lru_walk - Structure describing a LRU walk.
 */
struct ttm_lru_walk {
	/** @ops: Pointer to the ops structure. */
	const struct ttm_lru_walk_ops *ops;
	/** @arg: Common bo LRU walk arguments. */
	struct ttm_lru_walk_arg arg;
};

s64 ttm_lru_walk_for_evict(struct ttm_lru_walk *walk, struct ttm_device *bdev,
			   struct ttm_resource_manager *man, s64 target);

/**
 * struct ttm_bo_shrink_flags - flags to govern the bo shrinking behaviour
 * @purge: Purge the content rather than backing it up.
 * @writeback: Attempt to immediately write content to swap space.
 * @allow_move: Allow moving to system before shrinking. This is typically
 * not desired for zombie- or ghost objects (with zombie object meaning
 * objects with a zero gem object refcount)
 */
struct ttm_bo_shrink_flags {
	u32 purge : 1;
	u32 writeback : 1;
	u32 allow_move : 1;
};

long ttm_bo_shrink(struct ttm_operation_ctx *ctx, struct ttm_buffer_object *bo,
		   const struct ttm_bo_shrink_flags flags);

bool ttm_bo_shrink_suitable(struct ttm_buffer_object *bo, struct ttm_operation_ctx *ctx);

bool ttm_bo_shrink_avoid_wait(void);

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
 * @ticket: Ticket used to acquire the ww_mutex.
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

void ttm_bo_move_to_lru_tail(struct ttm_buffer_object *bo);

static inline void
ttm_bo_move_to_lru_tail_unlocked(struct ttm_buffer_object *bo)
{
	spin_lock(&bo->bdev->lru_lock);
	ttm_bo_move_to_lru_tail(bo);
	spin_unlock(&bo->bdev->lru_lock);
}

static inline void ttm_bo_assign_mem(struct ttm_buffer_object *bo,
				     struct ttm_resource *new_mem)
{
	WARN_ON(bo->resource);
	bo->resource = new_mem;
}

/**
 * ttm_bo_move_null - assign memory for a buffer object.
 * @bo: The bo to assign the memory to
 * @new_mem: The memory to be assigned.
 *
 * Assign the memory from new_mem to the memory of the buffer object bo.
 */
static inline void ttm_bo_move_null(struct ttm_buffer_object *bo,
				    struct ttm_resource *new_mem)
{
	ttm_resource_free(bo, &bo->resource);
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

/**
 * ttm_kmap_obj_virtual
 *
 * @map: A struct ttm_bo_kmap_obj returned from ttm_bo_kmap.
 * @is_iomem: Pointer to an integer that on return indicates 1 if the
 * virtual map is io memory, 0 if normal memory.
 *
 * Returns the virtual address of a buffer object area mapped by ttm_bo_kmap.
 * If *is_iomem is 1 on return, the virtual address points to an io memory area,
 * that should strictly be accessed by the iowriteXX() and similar functions.
 */
static inline void *ttm_kmap_obj_virtual(struct ttm_bo_kmap_obj *map,
					 bool *is_iomem)
{
	*is_iomem = !!(map->bo_kmap_type & TTM_BO_MAP_IOMEM_MASK);
	return map->virtual;
}

int ttm_bo_wait_ctx(struct ttm_buffer_object *bo,
		    struct ttm_operation_ctx *ctx);
int ttm_bo_validate(struct ttm_buffer_object *bo,
		    struct ttm_placement *placement,
		    struct ttm_operation_ctx *ctx);
void ttm_bo_put(struct ttm_buffer_object *bo);
void ttm_bo_set_bulk_move(struct ttm_buffer_object *bo,
			  struct ttm_lru_bulk_move *bulk);
bool ttm_bo_eviction_valuable(struct ttm_buffer_object *bo,
			      const struct ttm_place *place);
int ttm_bo_init_reserved(struct ttm_device *bdev, struct ttm_buffer_object *bo,
			 enum ttm_bo_type type, struct ttm_placement *placement,
			 uint32_t alignment, struct ttm_operation_ctx *ctx,
			 struct sg_table *sg, struct dma_resv *resv,
			 void (*destroy)(struct ttm_buffer_object *));
int ttm_bo_init_validate(struct ttm_device *bdev, struct ttm_buffer_object *bo,
			 enum ttm_bo_type type, struct ttm_placement *placement,
			 uint32_t alignment, bool interruptible,
			 struct sg_table *sg, struct dma_resv *resv,
			 void (*destroy)(struct ttm_buffer_object *));
int ttm_bo_kmap(struct ttm_buffer_object *bo, unsigned long start_page,
		unsigned long num_pages, struct ttm_bo_kmap_obj *map);
void ttm_bo_kunmap(struct ttm_bo_kmap_obj *map);
void *ttm_bo_kmap_try_from_panic(struct ttm_buffer_object *bo, unsigned long page);
int ttm_bo_vmap(struct ttm_buffer_object *bo, struct iosys_map *map);
void ttm_bo_vunmap(struct ttm_buffer_object *bo, struct iosys_map *map);
int ttm_bo_mmap_obj(struct vm_area_struct *vma, struct ttm_buffer_object *bo);
s64 ttm_bo_swapout(struct ttm_device *bdev, struct ttm_operation_ctx *ctx,
		   struct ttm_resource_manager *man, gfp_t gfp_flags,
		   s64 target);
void ttm_bo_pin(struct ttm_buffer_object *bo);
void ttm_bo_unpin(struct ttm_buffer_object *bo);
int ttm_bo_evict_first(struct ttm_device *bdev,
		       struct ttm_resource_manager *man,
		       struct ttm_operation_ctx *ctx);
int ttm_bo_access(struct ttm_buffer_object *bo, unsigned long offset,
		  void *buf, int len, int write);
vm_fault_t ttm_bo_vm_reserve(struct ttm_buffer_object *bo,
			     struct vm_fault *vmf);
vm_fault_t ttm_bo_vm_fault_reserved(struct vm_fault *vmf,
				    pgprot_t prot,
				    pgoff_t num_prefault);
vm_fault_t ttm_bo_vm_fault(struct vm_fault *vmf);
void ttm_bo_vm_open(struct vm_area_struct *vma);
void ttm_bo_vm_close(struct vm_area_struct *vma);
int ttm_bo_vm_access(struct vm_area_struct *vma, unsigned long addr,
		     void *buf, int len, int write);
vm_fault_t ttm_bo_vm_dummy_page(struct vm_fault *vmf, pgprot_t prot);

int ttm_bo_mem_space(struct ttm_buffer_object *bo,
		     struct ttm_placement *placement,
		     struct ttm_resource **mem,
		     struct ttm_operation_ctx *ctx);

void ttm_bo_unmap_virtual(struct ttm_buffer_object *bo);
/*
 * ttm_bo_util.c
 */
int ttm_mem_io_reserve(struct ttm_device *bdev,
		       struct ttm_resource *mem);
void ttm_mem_io_free(struct ttm_device *bdev,
		     struct ttm_resource *mem);
void ttm_move_memcpy(bool clear, u32 num_pages,
		     struct ttm_kmap_iter *dst_iter,
		     struct ttm_kmap_iter *src_iter);
int ttm_bo_move_memcpy(struct ttm_buffer_object *bo,
		       struct ttm_operation_ctx *ctx,
		       struct ttm_resource *new_mem);
int ttm_bo_move_accel_cleanup(struct ttm_buffer_object *bo,
			      struct dma_fence *fence, bool evict,
			      bool pipeline,
			      struct ttm_resource *new_mem);
void ttm_bo_move_sync_cleanup(struct ttm_buffer_object *bo,
			      struct ttm_resource *new_mem);
int ttm_bo_pipeline_gutting(struct ttm_buffer_object *bo);
pgprot_t ttm_io_prot(struct ttm_buffer_object *bo, struct ttm_resource *res,
		     pgprot_t tmp);
void ttm_bo_tt_destroy(struct ttm_buffer_object *bo);
int ttm_bo_populate(struct ttm_buffer_object *bo,
		    struct ttm_operation_ctx *ctx);
int ttm_bo_setup_export(struct ttm_buffer_object *bo,
			struct ttm_operation_ctx *ctx);

/* Driver LRU walk helpers initially targeted for shrinking. */

/**
 * struct ttm_bo_lru_cursor - Iterator cursor for TTM LRU list looping
 */
struct ttm_bo_lru_cursor {
	/** @res_curs: Embedded struct ttm_resource_cursor. */
	struct ttm_resource_cursor res_curs;
	/**
	 * @bo: Buffer object pointer if a buffer object is refcounted,
	 * NULL otherwise.
	 */
	struct ttm_buffer_object *bo;
	/**
	 * @needs_unlock: Valid iff @bo != NULL. The bo resv needs
	 * unlock before the next iteration or after loop exit.
	 */
	bool needs_unlock;
	/** @arg: Pointer to common BO LRU walk arguments. */
	struct ttm_lru_walk_arg *arg;
};

void ttm_bo_lru_cursor_fini(struct ttm_bo_lru_cursor *curs);

struct ttm_bo_lru_cursor *
ttm_bo_lru_cursor_init(struct ttm_bo_lru_cursor *curs,
		       struct ttm_resource_manager *man,
		       struct ttm_lru_walk_arg *arg);

struct ttm_buffer_object *ttm_bo_lru_cursor_first(struct ttm_bo_lru_cursor *curs);

struct ttm_buffer_object *ttm_bo_lru_cursor_next(struct ttm_bo_lru_cursor *curs);

/*
 * Defines needed to use autocleanup (linux/cleanup.h) with struct ttm_bo_lru_cursor.
 */
DEFINE_CLASS(ttm_bo_lru_cursor, struct ttm_bo_lru_cursor *,
	     if (_T) {ttm_bo_lru_cursor_fini(_T); },
	     ttm_bo_lru_cursor_init(curs, man, arg),
	     struct ttm_bo_lru_cursor *curs, struct ttm_resource_manager *man,
	     struct ttm_lru_walk_arg *arg);
static inline void *
class_ttm_bo_lru_cursor_lock_ptr(class_ttm_bo_lru_cursor_t *_T)
{ return *_T; }
#define class_ttm_bo_lru_cursor_is_conditional false

/**
 * ttm_bo_lru_for_each_reserved_guarded() - Iterate over buffer objects owning
 * resources on LRU lists.
 * @_cursor: struct ttm_bo_lru_cursor to use for the iteration.
 * @_man: The resource manager whose LRU lists to iterate over.
 * @_arg: The struct ttm_lru_walk_arg to govern the LRU walk.
 * @_bo: The struct ttm_buffer_object pointer pointing to the buffer object
 * for the current iteration.
 *
 * Iterate over all resources of @_man and for each resource, attempt to
 * reference and lock (using the locking mode detailed in @_ctx) the buffer
 * object it points to. If successful, assign @_bo to the address of the
 * buffer object and update @_cursor. The iteration is guarded in the
 * sense that @_cursor will be initialized before looping start and cleaned
 * up at looping termination, even if terminated prematurely by, for
 * example a return or break statement. Exiting the loop will also unlock
 * (if needed) and unreference @_bo.
 *
 * Return: If locking of a bo returns an error, then iteration is terminated
 * and @_bo is set to a corresponding error pointer. It's illegal to
 * dereference @_bo after loop exit.
 */
#define ttm_bo_lru_for_each_reserved_guarded(_cursor, _man, _arg, _bo)	\
	scoped_guard(ttm_bo_lru_cursor, _cursor, _man, _arg)		\
		for ((_bo) = ttm_bo_lru_cursor_first(_cursor);		\
		       !IS_ERR_OR_NULL(_bo);				\
		       (_bo) = ttm_bo_lru_cursor_next(_cursor))

#endif
