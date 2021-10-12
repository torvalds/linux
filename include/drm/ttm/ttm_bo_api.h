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
#include <drm/drm_hashtab.h>
#include <drm/drm_vma_manager.h>
#include <linux/kref.h>
#include <linux/list.h>
#include <linux/wait.h>
#include <linux/mutex.h>
#include <linux/mm.h>
#include <linux/bitmap.h>
#include <linux/dma-resv.h>

#include "ttm_resource.h"

struct ttm_global;

struct ttm_device;

struct dma_buf_map;

struct drm_mm_node;

struct ttm_placement;

struct ttm_place;

struct ttm_lru_bulk_move;

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

struct ttm_tt;

/**
 * struct ttm_buffer_object
 *
 * @base: drm_gem_object superclass data.
 * @bdev: Pointer to the buffer object device structure.
 * @type: The bo type.
 * @page_alignment: Page alignment.
 * @destroy: Destruction function. If NULL, kfree is used.
 * @num_pages: Actual number of pages.
 * @kref: Reference count of this buffer object. When this refcount reaches
 * zero, the object is destroyed or put on the delayed delete list.
 * @mem: structure describing current placement.
 * @ttm: TTM structure holding system pages.
 * @evicted: Whether the object was evicted without user-space knowing.
 * @deleted: True if the object is only a zombie and already deleted.
 * @lru: List head for the lru list.
 * @ddestroy: List head for the delayed destroy list.
 * @swap: List head for swap LRU list.
 * @moving: Fence set when BO is moving
 * @offset: The current GPU offset, which can have different meanings
 * depending on the memory type. For SYSTEM type memory, it should be 0.
 * @cur_placement: Hint of current placement.
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

	/**
	 * Members constant at init.
	 */

	struct ttm_device *bdev;
	enum ttm_bo_type type;
	uint32_t page_alignment;
	void (*destroy) (struct ttm_buffer_object *);

	/**
	* Members not needing protection.
	*/
	struct kref kref;

	/**
	 * Members protected by the bo::resv::reserved lock.
	 */

	struct ttm_resource *resource;
	struct ttm_tt *ttm;
	bool deleted;

	/**
	 * Members protected by the bdev::lru_lock.
	 */

	struct list_head lru;
	struct list_head ddestroy;

	/**
	 * Members protected by a bo reservation.
	 */

	struct dma_fence *moving;
	unsigned priority;
	unsigned pin_count;

	/**
	 * Special members that are protected by the reserve lock
	 * and the bo::lock when written to. Can be read with
	 * either of these locks held.
	 */

	struct sg_table *sg;
};

/**
 * struct ttm_bo_kmap_obj
 *
 * @virtual: The current kernel virtual address.
 * @page: The page when kmap'ing a single page.
 * @bo_kmap_type: Type of bo_kmap.
 *
 * Object describing a kernel mapping. Since a TTM bo may be located
 * in various memory types with various caching policies, the
 * mapping can either be an ioremap, a vmap, a kmap or part of a
 * premapped region.
 */

#define TTM_BO_MAP_IOMEM_MASK 0x80
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
 * @force_alloc: Don't check the memory account during suspend or CPU page
 * faults. Should only be used by TTM internally.
 * @resv: Reservation object to allow reserved evictions with.
 *
 * Context for TTM operations like changing buffer placement or general memory
 * allocation.
 */
struct ttm_operation_ctx {
	bool interruptible;
	bool no_wait_gpu;
	bool gfp_retry_mayfail;
	bool allow_res_evict;
	bool force_alloc;
	struct dma_resv *resv;
	uint64_t bytes_moved;
};

/**
 * ttm_bo_get - reference a struct ttm_buffer_object
 *
 * @bo: The buffer object.
 */
static inline void ttm_bo_get(struct ttm_buffer_object *bo)
{
	kref_get(&bo->kref);
}

/**
 * ttm_bo_get_unless_zero - reference a struct ttm_buffer_object unless
 * its refcount has already reached zero.
 * @bo: The buffer object.
 *
 * Used to reference a TTM buffer object in lookups where the object is removed
 * from the lookup structure during the destructor and for RCU lookups.
 *
 * Returns: @bo if the referencing was successful, NULL otherwise.
 */
static inline __must_check struct ttm_buffer_object *
ttm_bo_get_unless_zero(struct ttm_buffer_object *bo)
{
	if (!kref_get_unless_zero(&bo->kref))
		return NULL;
	return bo;
}

/**
 * ttm_bo_wait - wait for buffer idle.
 *
 * @bo:  The buffer object.
 * @interruptible:  Use interruptible wait.
 * @no_wait:  Return immediately if buffer is busy.
 *
 * This function must be called with the bo::mutex held, and makes
 * sure any previous rendering to the buffer is completed.
 * Note: It might be necessary to block validations before the
 * wait by reserving the buffer.
 * Returns -EBUSY if no_wait is true and the buffer is busy.
 * Returns -ERESTARTSYS if interrupted by a signal.
 */
int ttm_bo_wait(struct ttm_buffer_object *bo, bool interruptible, bool no_wait);

static inline int ttm_bo_wait_ctx(struct ttm_buffer_object *bo, struct ttm_operation_ctx *ctx)
{
	return ttm_bo_wait(bo, ctx->interruptible, ctx->no_wait_gpu);
}

/**
 * ttm_bo_validate
 *
 * @bo: The buffer object.
 * @placement: Proposed placement for the buffer object.
 * @ctx: validation parameters.
 *
 * Changes placement and caching policy of the buffer object
 * according proposed placement.
 * Returns
 * -EINVAL on invalid proposed placement.
 * -ENOMEM on out-of-memory condition.
 * -EBUSY if no_wait is true and buffer busy.
 * -ERESTARTSYS if interrupted by a signal.
 */
int ttm_bo_validate(struct ttm_buffer_object *bo,
		    struct ttm_placement *placement,
		    struct ttm_operation_ctx *ctx);

/**
 * ttm_bo_put
 *
 * @bo: The buffer object.
 *
 * Unreference a buffer object.
 */
void ttm_bo_put(struct ttm_buffer_object *bo);

/**
 * ttm_bo_move_to_lru_tail
 *
 * @bo: The buffer object.
 * @mem: Resource object.
 * @bulk: optional bulk move structure to remember BO positions
 *
 * Move this BO to the tail of all lru lists used to lookup and reserve an
 * object. This function must be called with struct ttm_global::lru_lock
 * held, and is used to make a BO less likely to be considered for eviction.
 */
void ttm_bo_move_to_lru_tail(struct ttm_buffer_object *bo,
			     struct ttm_resource *mem,
			     struct ttm_lru_bulk_move *bulk);

/**
 * ttm_bo_bulk_move_lru_tail
 *
 * @bulk: bulk move structure
 *
 * Bulk move BOs to the LRU tail, only valid to use when driver makes sure that
 * BO order never changes. Should be called with ttm_global::lru_lock held.
 */
void ttm_bo_bulk_move_lru_tail(struct ttm_lru_bulk_move *bulk);

/**
 * ttm_bo_lock_delayed_workqueue
 *
 * Prevent the delayed workqueue from running.
 * Returns
 * True if the workqueue was queued at the time
 */
int ttm_bo_lock_delayed_workqueue(struct ttm_device *bdev);

/**
 * ttm_bo_unlock_delayed_workqueue
 *
 * Allows the delayed workqueue to run.
 */
void ttm_bo_unlock_delayed_workqueue(struct ttm_device *bdev, int resched);

/**
 * ttm_bo_eviction_valuable
 *
 * @bo: The buffer object to evict
 * @place: the placement we need to make room for
 *
 * Check if it is valuable to evict the BO to make room for the given placement.
 */
bool ttm_bo_eviction_valuable(struct ttm_buffer_object *bo,
			      const struct ttm_place *place);

/**
 * ttm_bo_init_reserved
 *
 * @bdev: Pointer to a ttm_device struct.
 * @bo: Pointer to a ttm_buffer_object to be initialized.
 * @size: Requested size of buffer object.
 * @type: Requested type of buffer object.
 * @flags: Initial placement flags.
 * @page_alignment: Data alignment in pages.
 * @ctx: TTM operation context for memory allocation.
 * @resv: Pointer to a dma_resv, or NULL to let ttm allocate one.
 * @destroy: Destroy function. Use NULL for kfree().
 *
 * This function initializes a pre-allocated struct ttm_buffer_object.
 * As this object may be part of a larger structure, this function,
 * together with the @destroy function,
 * enables driver-specific objects derived from a ttm_buffer_object.
 *
 * On successful return, the caller owns an object kref to @bo. The kref and
 * list_kref are usually set to 1, but note that in some situations, other
 * tasks may already be holding references to @bo as well.
 * Furthermore, if resv == NULL, the buffer's reservation lock will be held,
 * and it is the caller's responsibility to call ttm_bo_unreserve.
 *
 * If a failure occurs, the function will call the @destroy function, or
 * kfree() if @destroy is NULL. Thus, after a failure, dereferencing @bo is
 * illegal and will likely cause memory corruption.
 *
 * Returns
 * -ENOMEM: Out of memory.
 * -EINVAL: Invalid placement flags.
 * -ERESTARTSYS: Interrupted by signal while sleeping waiting for resources.
 */

int ttm_bo_init_reserved(struct ttm_device *bdev,
			 struct ttm_buffer_object *bo,
			 size_t size, enum ttm_bo_type type,
			 struct ttm_placement *placement,
			 uint32_t page_alignment,
			 struct ttm_operation_ctx *ctx,
			 struct sg_table *sg, struct dma_resv *resv,
			 void (*destroy) (struct ttm_buffer_object *));

/**
 * ttm_bo_init
 *
 * @bdev: Pointer to a ttm_device struct.
 * @bo: Pointer to a ttm_buffer_object to be initialized.
 * @size: Requested size of buffer object.
 * @type: Requested type of buffer object.
 * @flags: Initial placement flags.
 * @page_alignment: Data alignment in pages.
 * @interruptible: If needing to sleep to wait for GPU resources,
 * sleep interruptible.
 * pinned in physical memory. If this behaviour is not desired, this member
 * holds a pointer to a persistent shmem object. Typically, this would
 * point to the shmem object backing a GEM object if TTM is used to back a
 * GEM user interface.
 * @resv: Pointer to a dma_resv, or NULL to let ttm allocate one.
 * @destroy: Destroy function. Use NULL for kfree().
 *
 * This function initializes a pre-allocated struct ttm_buffer_object.
 * As this object may be part of a larger structure, this function,
 * together with the @destroy function,
 * enables driver-specific objects derived from a ttm_buffer_object.
 *
 * On successful return, the caller owns an object kref to @bo. The kref and
 * list_kref are usually set to 1, but note that in some situations, other
 * tasks may already be holding references to @bo as well.
 *
 * If a failure occurs, the function will call the @destroy function, or
 * kfree() if @destroy is NULL. Thus, after a failure, dereferencing @bo is
 * illegal and will likely cause memory corruption.
 *
 * Returns
 * -ENOMEM: Out of memory.
 * -EINVAL: Invalid placement flags.
 * -ERESTARTSYS: Interrupted by signal while sleeping waiting for resources.
 */
int ttm_bo_init(struct ttm_device *bdev, struct ttm_buffer_object *bo,
		size_t size, enum ttm_bo_type type,
		struct ttm_placement *placement,
		uint32_t page_alignment, bool interrubtible,
		struct sg_table *sg, struct dma_resv *resv,
		void (*destroy) (struct ttm_buffer_object *));

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

/**
 * ttm_bo_kmap
 *
 * @bo: The buffer object.
 * @start_page: The first page to map.
 * @num_pages: Number of pages to map.
 * @map: pointer to a struct ttm_bo_kmap_obj representing the map.
 *
 * Sets up a kernel virtual mapping, using ioremap, vmap or kmap to the
 * data in the buffer object. The ttm_kmap_obj_virtual function can then be
 * used to obtain a virtual address to the data.
 *
 * Returns
 * -ENOMEM: Out of memory.
 * -EINVAL: Invalid range.
 */
int ttm_bo_kmap(struct ttm_buffer_object *bo, unsigned long start_page,
		unsigned long num_pages, struct ttm_bo_kmap_obj *map);

/**
 * ttm_bo_kunmap
 *
 * @map: Object describing the map to unmap.
 *
 * Unmaps a kernel map set up by ttm_bo_kmap.
 */
void ttm_bo_kunmap(struct ttm_bo_kmap_obj *map);

/**
 * ttm_bo_vmap
 *
 * @bo: The buffer object.
 * @map: pointer to a struct dma_buf_map representing the map.
 *
 * Sets up a kernel virtual mapping, using ioremap or vmap to the
 * data in the buffer object. The parameter @map returns the virtual
 * address as struct dma_buf_map. Unmap the buffer with ttm_bo_vunmap().
 *
 * Returns
 * -ENOMEM: Out of memory.
 * -EINVAL: Invalid range.
 */
int ttm_bo_vmap(struct ttm_buffer_object *bo, struct dma_buf_map *map);

/**
 * ttm_bo_vunmap
 *
 * @bo: The buffer object.
 * @map: Object describing the map to unmap.
 *
 * Unmaps a kernel map set up by ttm_bo_vmap().
 */
void ttm_bo_vunmap(struct ttm_buffer_object *bo, struct dma_buf_map *map);

/**
 * ttm_bo_mmap_obj - mmap memory backed by a ttm buffer object.
 *
 * @vma:       vma as input from the fbdev mmap method.
 * @bo:        The bo backing the address space.
 *
 * Maps a buffer object.
 */
int ttm_bo_mmap_obj(struct vm_area_struct *vma, struct ttm_buffer_object *bo);

/**
 * ttm_bo_io
 *
 * @bdev:      Pointer to the struct ttm_device.
 * @filp:      Pointer to the struct file attempting to read / write.
 * @wbuf:      User-space pointer to address of buffer to write. NULL on read.
 * @rbuf:      User-space pointer to address of buffer to read into.
 * Null on write.
 * @count:     Number of bytes to read / write.
 * @f_pos:     Pointer to current file position.
 * @write:     1 for read, 0 for write.
 *
 * This function implements read / write into ttm buffer objects, and is
 * intended to
 * be called from the fops::read and fops::write method.
 * Returns:
 * See man (2) write, man(2) read. In particular,
 * the function may return -ERESTARTSYS if
 * interrupted by a signal.
 */
ssize_t ttm_bo_io(struct ttm_device *bdev, struct file *filp,
		  const char __user *wbuf, char __user *rbuf,
		  size_t count, loff_t *f_pos, bool write);

int ttm_bo_swapout(struct ttm_buffer_object *bo, struct ttm_operation_ctx *ctx,
		   gfp_t gfp_flags);

/**
 * ttm_bo_pin - Pin the buffer object.
 * @bo: The buffer object to pin
 *
 * Make sure the buffer is not evicted any more during memory pressure.
 */
static inline void ttm_bo_pin(struct ttm_buffer_object *bo)
{
	dma_resv_assert_held(bo->base.resv);
	WARN_ON_ONCE(!kref_read(&bo->kref));
	++bo->pin_count;
}

/**
 * ttm_bo_unpin - Unpin the buffer object.
 * @bo: The buffer object to unpin
 *
 * Allows the buffer object to be evicted again during memory pressure.
 */
static inline void ttm_bo_unpin(struct ttm_buffer_object *bo)
{
	dma_resv_assert_held(bo->base.resv);
	WARN_ON_ONCE(!kref_read(&bo->kref));
	if (bo->pin_count)
		--bo->pin_count;
	else
		WARN_ON_ONCE(true);
}

int ttm_mem_evict_first(struct ttm_device *bdev,
			struct ttm_resource_manager *man,
			const struct ttm_place *place,
			struct ttm_operation_ctx *ctx,
			struct ww_acquire_ctx *ticket);

/* Default number of pre-faulted pages in the TTM fault handler */
#define TTM_BO_VM_NUM_PREFAULT 16

vm_fault_t ttm_bo_vm_reserve(struct ttm_buffer_object *bo,
			     struct vm_fault *vmf);

vm_fault_t ttm_bo_vm_fault_reserved(struct vm_fault *vmf,
				    pgprot_t prot,
				    pgoff_t num_prefault,
				    pgoff_t fault_page_size);

vm_fault_t ttm_bo_vm_fault(struct vm_fault *vmf);

void ttm_bo_vm_open(struct vm_area_struct *vma);

void ttm_bo_vm_close(struct vm_area_struct *vma);

int ttm_bo_vm_access(struct vm_area_struct *vma, unsigned long addr,
		     void *buf, int len, int write);
bool ttm_bo_delayed_delete(struct ttm_device *bdev, bool remove_all);

vm_fault_t ttm_bo_vm_dummy_page(struct vm_fault *vmf, pgprot_t prot);

#endif
