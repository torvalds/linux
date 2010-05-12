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

#include "ttm/ttm_bo_api.h"
#include "ttm/ttm_memory.h"
#include "ttm/ttm_module.h"
#include "drm_mm.h"
#include "linux/workqueue.h"
#include "linux/fs.h"
#include "linux/spinlock.h"

struct ttm_backend;

struct ttm_backend_func {
	/**
	 * struct ttm_backend_func member populate
	 *
	 * @backend: Pointer to a struct ttm_backend.
	 * @num_pages: Number of pages to populate.
	 * @pages: Array of pointers to ttm pages.
	 * @dummy_read_page: Page to be used instead of NULL pages in the
	 * array @pages.
	 *
	 * Populate the backend with ttm pages. Depending on the backend,
	 * it may or may not copy the @pages array.
	 */
	int (*populate) (struct ttm_backend *backend,
			 unsigned long num_pages, struct page **pages,
			 struct page *dummy_read_page);
	/**
	 * struct ttm_backend_func member clear
	 *
	 * @backend: Pointer to a struct ttm_backend.
	 *
	 * This is an "unpopulate" function. Release all resources
	 * allocated with populate.
	 */
	void (*clear) (struct ttm_backend *backend);

	/**
	 * struct ttm_backend_func member bind
	 *
	 * @backend: Pointer to a struct ttm_backend.
	 * @bo_mem: Pointer to a struct ttm_mem_reg describing the
	 * memory type and location for binding.
	 *
	 * Bind the backend pages into the aperture in the location
	 * indicated by @bo_mem. This function should be able to handle
	 * differences between aperture- and system page sizes.
	 */
	int (*bind) (struct ttm_backend *backend, struct ttm_mem_reg *bo_mem);

	/**
	 * struct ttm_backend_func member unbind
	 *
	 * @backend: Pointer to a struct ttm_backend.
	 *
	 * Unbind previously bound backend pages. This function should be
	 * able to handle differences between aperture- and system page sizes.
	 */
	int (*unbind) (struct ttm_backend *backend);

	/**
	 * struct ttm_backend_func member destroy
	 *
	 * @backend: Pointer to a struct ttm_backend.
	 *
	 * Destroy the backend.
	 */
	void (*destroy) (struct ttm_backend *backend);
};

/**
 * struct ttm_backend
 *
 * @bdev: Pointer to a struct ttm_bo_device.
 * @flags: For driver use.
 * @func: Pointer to a struct ttm_backend_func that describes
 * the backend methods.
 *
 */

struct ttm_backend {
	struct ttm_bo_device *bdev;
	uint32_t flags;
	struct ttm_backend_func *func;
};

#define TTM_PAGE_FLAG_USER            (1 << 1)
#define TTM_PAGE_FLAG_USER_DIRTY      (1 << 2)
#define TTM_PAGE_FLAG_WRITE           (1 << 3)
#define TTM_PAGE_FLAG_SWAPPED         (1 << 4)
#define TTM_PAGE_FLAG_PERSISTANT_SWAP (1 << 5)
#define TTM_PAGE_FLAG_ZERO_ALLOC      (1 << 6)
#define TTM_PAGE_FLAG_DMA32           (1 << 7)

enum ttm_caching_state {
	tt_uncached,
	tt_wc,
	tt_cached
};

/**
 * struct ttm_tt
 *
 * @dummy_read_page: Page to map where the ttm_tt page array contains a NULL
 * pointer.
 * @pages: Array of pages backing the data.
 * @first_himem_page: Himem pages are put last in the page array, which
 * enables us to run caching attribute changes on only the first part
 * of the page array containing lomem pages. This is the index of the
 * first himem page.
 * @last_lomem_page: Index of the last lomem page in the page array.
 * @num_pages: Number of pages in the page array.
 * @bdev: Pointer to the current struct ttm_bo_device.
 * @be: Pointer to the ttm backend.
 * @tsk: The task for user ttm.
 * @start: virtual address for user ttm.
 * @swap_storage: Pointer to shmem struct file for swap storage.
 * @caching_state: The current caching state of the pages.
 * @state: The current binding state of the pages.
 *
 * This is a structure holding the pages, caching- and aperture binding
 * status for a buffer object that isn't backed by fixed (VRAM / AGP)
 * memory.
 */

struct ttm_tt {
	struct page *dummy_read_page;
	struct page **pages;
	long first_himem_page;
	long last_lomem_page;
	uint32_t page_flags;
	unsigned long num_pages;
	struct ttm_bo_global *glob;
	struct ttm_backend *be;
	struct task_struct *tsk;
	unsigned long start;
	struct file *swap_storage;
	enum ttm_caching_state caching_state;
	enum {
		tt_bound,
		tt_unbound,
		tt_unpopulated,
	} state;
};

#define TTM_MEMTYPE_FLAG_FIXED         (1 << 0)	/* Fixed (on-card) PCI memory */
#define TTM_MEMTYPE_FLAG_MAPPABLE      (1 << 1)	/* Memory mappable */
#define TTM_MEMTYPE_FLAG_NEEDS_IOREMAP (1 << 2)	/* Fixed memory needs ioremap
						   before kernel access. */
#define TTM_MEMTYPE_FLAG_CMA           (1 << 3)	/* Can't map aperture */

/**
 * struct ttm_mem_type_manager
 *
 * @has_type: The memory type has been initialized.
 * @use_type: The memory type is enabled.
 * @flags: TTM_MEMTYPE_XX flags identifying the traits of the memory
 * managed by this memory type.
 * @gpu_offset: If used, the GPU offset of the first managed page of
 * fixed memory or the first managed location in an aperture.
 * @io_offset: The io_offset of the first managed page of IO memory or
 * the first managed location in an aperture. For TTM_MEMTYPE_FLAG_CMA
 * memory, this should be set to NULL.
 * @io_size: The size of a managed IO region (fixed memory or aperture).
 * @io_addr: Virtual kernel address if the io region is pre-mapped. For
 * TTM_MEMTYPE_FLAG_NEEDS_IOREMAP there is no pre-mapped io map and
 * @io_addr should be set to NULL.
 * @size: Size of the managed region.
 * @available_caching: A mask of available caching types, TTM_PL_FLAG_XX,
 * as defined in ttm_placement_common.h
 * @default_caching: The default caching policy used for a buffer object
 * placed in this memory type if the user doesn't provide one.
 * @manager: The range manager used for this memory type. FIXME: If the aperture
 * has a page size different from the underlying system, the granularity
 * of this manager should take care of this. But the range allocating code
 * in ttm_bo.c needs to be modified for this.
 * @lru: The lru list for this memory type.
 *
 * This structure is used to identify and manage memory types for a device.
 * It's set up by the ttm_bo_driver::init_mem_type method.
 */

struct ttm_mem_type_manager {

	/*
	 * No protection. Constant from start.
	 */

	bool has_type;
	bool use_type;
	uint32_t flags;
	unsigned long gpu_offset;
	unsigned long io_offset;
	unsigned long io_size;
	void *io_addr;
	uint64_t size;
	uint32_t available_caching;
	uint32_t default_caching;

	/*
	 * Protected by the bdev->lru_lock.
	 * TODO: Consider one lru_lock per ttm_mem_type_manager.
	 * Plays ill with list removal, though.
	 */

	struct drm_mm manager;
	struct list_head lru;
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
 * @sync_obj_signaled: See ttm_fence_api.h
 * @sync_obj_wait: See ttm_fence_api.h
 * @sync_obj_flush: See ttm_fence_api.h
 * @sync_obj_unref: See ttm_fence_api.h
 * @sync_obj_ref: See ttm_fence_api.h
 */

struct ttm_bo_driver {
	/**
	 * struct ttm_bo_driver member create_ttm_backend_entry
	 *
	 * @bdev: The buffer object device.
	 *
	 * Create a driver specific struct ttm_backend.
	 */

	struct ttm_backend *(*create_ttm_backend_entry)
	 (struct ttm_bo_device *bdev);

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

	int (*invalidate_caches) (struct ttm_bo_device *bdev, uint32_t flags);
	int (*init_mem_type) (struct ttm_bo_device *bdev, uint32_t type,
			      struct ttm_mem_type_manager *man);
	/**
	 * struct ttm_bo_driver member evict_flags:
	 *
	 * @bo: the buffer object to be evicted
	 *
	 * Return the bo flags for a buffer which is not mapped to the hardware.
	 * These will be placed in proposed_flags so that when the move is
	 * finished, they'll end up in bo->mem.flags
	 */

	 void(*evict_flags) (struct ttm_buffer_object *bo,
				struct ttm_placement *placement);
	/**
	 * struct ttm_bo_driver member move:
	 *
	 * @bo: the buffer to move
	 * @evict: whether this motion is evicting the buffer from
	 * the graphics address space
	 * @interruptible: Use interruptible sleeps if possible when sleeping.
	 * @no_wait: whether this should give up and return -EBUSY
	 * if this move would require sleeping
	 * @new_mem: the new memory region receiving the buffer
	 *
	 * Move a buffer between two memory regions.
	 */
	int (*move) (struct ttm_buffer_object *bo,
		     bool evict, bool interruptible,
		     bool no_wait, struct ttm_mem_reg *new_mem);

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
	int (*verify_access) (struct ttm_buffer_object *bo,
			      struct file *filp);

	/**
	 * In case a driver writer dislikes the TTM fence objects,
	 * the driver writer can replace those with sync objects of
	 * his / her own. If it turns out that no driver writer is
	 * using these. I suggest we remove these hooks and plug in
	 * fences directly. The bo driver needs the following functionality:
	 * See the corresponding functions in the fence object API
	 * documentation.
	 */

	bool (*sync_obj_signaled) (void *sync_obj, void *sync_arg);
	int (*sync_obj_wait) (void *sync_obj, void *sync_arg,
			      bool lazy, bool interruptible);
	int (*sync_obj_flush) (void *sync_obj, void *sync_arg);
	void (*sync_obj_unref) (void **sync_obj);
	void *(*sync_obj_ref) (void *sync_obj);

	/* hook to notify driver about a driver move so it
	 * can do tiling things */
	void (*move_notify)(struct ttm_buffer_object *bo,
			    struct ttm_mem_reg *new_mem);
	/* notify the driver we are taking a fault on this BO
	 * and have reserved it */
	void (*fault_reserve_notify)(struct ttm_buffer_object *bo);

	/**
	 * notify the driver that we're about to swap out this bo
	 */
	void (*swap_notify) (struct ttm_buffer_object *bo);
};

/**
 * struct ttm_bo_global_ref - Argument to initialize a struct ttm_bo_global.
 */

struct ttm_bo_global_ref {
	struct ttm_global_reference ref;
	struct ttm_mem_global *mem_glob;
};

/**
 * struct ttm_bo_global - Buffer object driver global data.
 *
 * @mem_glob: Pointer to a struct ttm_mem_global object for accounting.
 * @dummy_read_page: Pointer to a dummy page used for mapping requests
 * of unpopulated pages.
 * @shrink: A shrink callback object used for buffer object swap.
 * @ttm_bo_extra_size: Extra size (sizeof(struct ttm_buffer_object) excluded)
 * used by a buffer object. This is excluding page arrays and backing pages.
 * @ttm_bo_size: This is @ttm_bo_extra_size + sizeof(struct ttm_buffer_object).
 * @device_list_mutex: Mutex protecting the device list.
 * This mutex is held while traversing the device list for pm options.
 * @lru_lock: Spinlock protecting the bo subsystem lru lists.
 * @device_list: List of buffer object devices.
 * @swap_lru: Lru list of buffer objects used for swapping.
 */

struct ttm_bo_global {

	/**
	 * Constant after init.
	 */

	struct kobject kobj;
	struct ttm_mem_global *mem_glob;
	struct page *dummy_read_page;
	struct ttm_mem_shrink shrink;
	size_t ttm_bo_extra_size;
	size_t ttm_bo_size;
	struct mutex device_list_mutex;
	spinlock_t lru_lock;

	/**
	 * Protected by device_list_mutex.
	 */
	struct list_head device_list;

	/**
	 * Protected by the lru_lock.
	 */
	struct list_head swap_lru;

	/**
	 * Internal protection.
	 */
	atomic_t bo_count;
};


#define TTM_NUM_MEM_TYPES 8

#define TTM_BO_PRIV_FLAG_MOVING  0	/* Buffer object is moving and needs
					   idling before CPU mapping */
#define TTM_BO_PRIV_FLAG_MAX 1
/**
 * struct ttm_bo_device - Buffer object driver device-specific data.
 *
 * @driver: Pointer to a struct ttm_bo_driver struct setup by the driver.
 * @man: An array of mem_type_managers.
 * @addr_space_mm: Range manager for the device address space.
 * lru_lock: Spinlock that protects the buffer+device lru lists and
 * ddestroy lists.
 * @nice_mode: Try nicely to wait for buffer idle when cleaning a manager.
 * If a GPU lockup has been detected, this is forced to 0.
 * @dev_mapping: A pointer to the struct address_space representing the
 * device address space.
 * @wq: Work queue structure for the delayed delete workqueue.
 *
 */

struct ttm_bo_device {

	/*
	 * Constant after bo device init / atomic.
	 */
	struct list_head device_list;
	struct ttm_bo_global *glob;
	struct ttm_bo_driver *driver;
	rwlock_t vm_lock;
	struct ttm_mem_type_manager man[TTM_NUM_MEM_TYPES];
	/*
	 * Protected by the vm lock.
	 */
	struct rb_root addr_space_rb;
	struct drm_mm addr_space_mm;

	/*
	 * Protected by the global:lru lock.
	 */
	struct list_head ddestroy;

	/*
	 * Protected by load / firstopen / lastclose /unload sync.
	 */

	bool nice_mode;
	struct address_space *dev_mapping;

	/*
	 * Internal protection.
	 */

	struct delayed_work wq;

	bool need_dma32;
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

/**
 * ttm_tt_create
 *
 * @bdev: pointer to a struct ttm_bo_device:
 * @size: Size of the data needed backing.
 * @page_flags: Page flags as identified by TTM_PAGE_FLAG_XX flags.
 * @dummy_read_page: See struct ttm_bo_device.
 *
 * Create a struct ttm_tt to back data with system memory pages.
 * No pages are actually allocated.
 * Returns:
 * NULL: Out of memory.
 */
extern struct ttm_tt *ttm_tt_create(struct ttm_bo_device *bdev,
				    unsigned long size,
				    uint32_t page_flags,
				    struct page *dummy_read_page);

/**
 * ttm_tt_set_user:
 *
 * @ttm: The struct ttm_tt to populate.
 * @tsk: A struct task_struct for which @start is a valid user-space address.
 * @start: A valid user-space address.
 * @num_pages: Size in pages of the user memory area.
 *
 * Populate a struct ttm_tt with a user-space memory area after first pinning
 * the pages backing it.
 * Returns:
 * !0: Error.
 */

extern int ttm_tt_set_user(struct ttm_tt *ttm,
			   struct task_struct *tsk,
			   unsigned long start, unsigned long num_pages);

/**
 * ttm_ttm_bind:
 *
 * @ttm: The struct ttm_tt containing backing pages.
 * @bo_mem: The struct ttm_mem_reg identifying the binding location.
 *
 * Bind the pages of @ttm to an aperture location identified by @bo_mem
 */
extern int ttm_tt_bind(struct ttm_tt *ttm, struct ttm_mem_reg *bo_mem);

/**
 * ttm_tt_populate:
 *
 * @ttm: The struct ttm_tt to contain the backing pages.
 *
 * Add backing pages to all of @ttm
 */
extern int ttm_tt_populate(struct ttm_tt *ttm);

/**
 * ttm_ttm_destroy:
 *
 * @ttm: The struct ttm_tt.
 *
 * Unbind, unpopulate and destroy a struct ttm_tt.
 */
extern void ttm_tt_destroy(struct ttm_tt *ttm);

/**
 * ttm_ttm_unbind:
 *
 * @ttm: The struct ttm_tt.
 *
 * Unbind a struct ttm_tt.
 */
extern void ttm_tt_unbind(struct ttm_tt *ttm);

/**
 * ttm_ttm_destroy:
 *
 * @ttm: The struct ttm_tt.
 * @index: Index of the desired page.
 *
 * Return a pointer to the struct page backing @ttm at page
 * index @index. If the page is unpopulated, one will be allocated to
 * populate that index.
 *
 * Returns:
 * NULL on OOM.
 */
extern struct page *ttm_tt_get_page(struct ttm_tt *ttm, int index);

/**
 * ttm_tt_cache_flush:
 *
 * @pages: An array of pointers to struct page:s to flush.
 * @num_pages: Number of pages to flush.
 *
 * Flush the data of the indicated pages from the cpu caches.
 * This is used when changing caching attributes of the pages from
 * cache-coherent.
 */
extern void ttm_tt_cache_flush(struct page *pages[], unsigned long num_pages);

/**
 * ttm_tt_set_placement_caching:
 *
 * @ttm A struct ttm_tt the backing pages of which will change caching policy.
 * @placement: Flag indicating the desired caching policy.
 *
 * This function will change caching policy of any default kernel mappings of
 * the pages backing @ttm. If changing from cached to uncached or
 * write-combined,
 * all CPU caches will first be flushed to make sure the data of the pages
 * hit RAM. This function may be very costly as it involves global TLB
 * and cache flushes and potential page splitting / combining.
 */
extern int ttm_tt_set_placement_caching(struct ttm_tt *ttm, uint32_t placement);
extern int ttm_tt_swapout(struct ttm_tt *ttm,
			  struct file *persistant_swap_storage);

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
extern bool ttm_mem_reg_is_pci(struct ttm_bo_device *bdev,
				   struct ttm_mem_reg *mem);

/**
 * ttm_bo_mem_space
 *
 * @bo: Pointer to a struct ttm_buffer_object. the data of which
 * we want to allocate space for.
 * @proposed_placement: Proposed new placement for the buffer object.
 * @mem: A struct ttm_mem_reg.
 * @interruptible: Sleep interruptible when sliping.
 * @no_wait: Don't sleep waiting for space to become available.
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
extern int ttm_bo_mem_space(struct ttm_buffer_object *bo,
				struct ttm_placement *placement,
				struct ttm_mem_reg *mem,
				bool interruptible, bool no_wait);
/**
 * ttm_bo_wait_for_cpu
 *
 * @bo: Pointer to a struct ttm_buffer_object.
 * @no_wait: Don't sleep while waiting.
 *
 * Wait until a buffer object is no longer sync'ed for CPU access.
 * Returns:
 * -EBUSY: Buffer object was sync'ed for CPU access. (only if no_wait == 1).
 * -ERESTARTSYS: An interruptible sleep was interrupted by a signal.
 */

extern int ttm_bo_wait_cpu(struct ttm_buffer_object *bo, bool no_wait);

/**
 * ttm_bo_pci_offset - Get the PCI offset for the buffer object memory.
 *
 * @bo Pointer to a struct ttm_buffer_object.
 * @bus_base On return the base of the PCI region
 * @bus_offset On return the byte offset into the PCI region
 * @bus_size On return the byte size of the buffer object or zero if
 * the buffer object memory is not accessible through a PCI region.
 *
 * Returns:
 * -EINVAL if the buffer object is currently not mappable.
 * 0 otherwise.
 */

extern int ttm_bo_pci_offset(struct ttm_bo_device *bdev,
			     struct ttm_mem_reg *mem,
			     unsigned long *bus_base,
			     unsigned long *bus_offset,
			     unsigned long *bus_size);

extern void ttm_bo_global_release(struct ttm_global_reference *ref);
extern int ttm_bo_global_init(struct ttm_global_reference *ref);

extern int ttm_bo_device_release(struct ttm_bo_device *bdev);

/**
 * ttm_bo_device_init
 *
 * @bdev: A pointer to a struct ttm_bo_device to initialize.
 * @mem_global: A pointer to an initialized struct ttm_mem_global.
 * @driver: A pointer to a struct ttm_bo_driver set up by the caller.
 * @file_page_offset: Offset into the device address space that is available
 * for buffer data. This ensures compatibility with other users of the
 * address space.
 *
 * Initializes a struct ttm_bo_device:
 * Returns:
 * !0: Failure.
 */
extern int ttm_bo_device_init(struct ttm_bo_device *bdev,
			      struct ttm_bo_global *glob,
			      struct ttm_bo_driver *driver,
			      uint64_t file_page_offset, bool need_dma32);

/**
 * ttm_bo_unmap_virtual
 *
 * @bo: tear down the virtual mappings for this BO
 */
extern void ttm_bo_unmap_virtual(struct ttm_buffer_object *bo);

/**
 * ttm_bo_reserve:
 *
 * @bo: A pointer to a struct ttm_buffer_object.
 * @interruptible: Sleep interruptible if waiting.
 * @no_wait: Don't sleep while trying to reserve, rather return -EBUSY.
 * @use_sequence: If @bo is already reserved, Only sleep waiting for
 * it to become unreserved if @sequence < (@bo)->sequence.
 *
 * Locks a buffer object for validation. (Or prevents other processes from
 * locking it for validation) and removes it from lru lists, while taking
 * a number of measures to prevent deadlocks.
 *
 * Deadlocks may occur when two processes try to reserve multiple buffers in
 * different order, either by will or as a result of a buffer being evicted
 * to make room for a buffer already reserved. (Buffers are reserved before
 * they are evicted). The following algorithm prevents such deadlocks from
 * occuring:
 * 1) Buffers are reserved with the lru spinlock held. Upon successful
 * reservation they are removed from the lru list. This stops a reserved buffer
 * from being evicted. However the lru spinlock is released between the time
 * a buffer is selected for eviction and the time it is reserved.
 * Therefore a check is made when a buffer is reserved for eviction, that it
 * is still the first buffer in the lru list, before it is removed from the
 * list. @check_lru == 1 forces this check. If it fails, the function returns
 * -EINVAL, and the caller should then choose a new buffer to evict and repeat
 * the procedure.
 * 2) Processes attempting to reserve multiple buffers other than for eviction,
 * (typically execbuf), should first obtain a unique 32-bit
 * validation sequence number,
 * and call this function with @use_sequence == 1 and @sequence == the unique
 * sequence number. If upon call of this function, the buffer object is already
 * reserved, the validation sequence is checked against the validation
 * sequence of the process currently reserving the buffer,
 * and if the current validation sequence is greater than that of the process
 * holding the reservation, the function returns -EAGAIN. Otherwise it sleeps
 * waiting for the buffer to become unreserved, after which it retries
 * reserving.
 * The caller should, when receiving an -EAGAIN error
 * release all its buffer reservations, wait for @bo to become unreserved, and
 * then rerun the validation with the same validation sequence. This procedure
 * will always guarantee that the process with the lowest validation sequence
 * will eventually succeed, preventing both deadlocks and starvation.
 *
 * Returns:
 * -EAGAIN: The reservation may cause a deadlock.
 * Release all buffer reservations, wait for @bo to become unreserved and
 * try again. (only if use_sequence == 1).
 * -ERESTARTSYS: A wait for the buffer to become unreserved was interrupted by
 * a signal. Release all buffer reservations and return to user-space.
 */
extern int ttm_bo_reserve(struct ttm_buffer_object *bo,
			  bool interruptible,
			  bool no_wait, bool use_sequence, uint32_t sequence);

/**
 * ttm_bo_unreserve
 *
 * @bo: A pointer to a struct ttm_buffer_object.
 *
 * Unreserve a previous reservation of @bo.
 */
extern void ttm_bo_unreserve(struct ttm_buffer_object *bo);

/**
 * ttm_bo_wait_unreserved
 *
 * @bo: A pointer to a struct ttm_buffer_object.
 *
 * Wait for a struct ttm_buffer_object to become unreserved.
 * This is typically used in the execbuf code to relax cpu-usage when
 * a potential deadlock condition backoff.
 */
extern int ttm_bo_wait_unreserved(struct ttm_buffer_object *bo,
				  bool interruptible);

/*
 * ttm_bo_util.c
 */

/**
 * ttm_bo_move_ttm
 *
 * @bo: A pointer to a struct ttm_buffer_object.
 * @evict: 1: This is an eviction. Don't try to pipeline.
 * @no_wait: Never sleep, but rather return with -EBUSY.
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

extern int ttm_bo_move_ttm(struct ttm_buffer_object *bo,
			   bool evict, bool no_wait,
			   struct ttm_mem_reg *new_mem);

/**
 * ttm_bo_move_memcpy
 *
 * @bo: A pointer to a struct ttm_buffer_object.
 * @evict: 1: This is an eviction. Don't try to pipeline.
 * @no_wait: Never sleep, but rather return with -EBUSY.
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

extern int ttm_bo_move_memcpy(struct ttm_buffer_object *bo,
			      bool evict,
			      bool no_wait, struct ttm_mem_reg *new_mem);

/**
 * ttm_bo_free_old_node
 *
 * @bo: A pointer to a struct ttm_buffer_object.
 *
 * Utility function to free an old placement after a successful move.
 */
extern void ttm_bo_free_old_node(struct ttm_buffer_object *bo);

/**
 * ttm_bo_move_accel_cleanup.
 *
 * @bo: A pointer to a struct ttm_buffer_object.
 * @sync_obj: A sync object that signals when moving is complete.
 * @sync_obj_arg: An argument to pass to the sync object idle / wait
 * functions.
 * @evict: This is an evict move. Don't return until the buffer is idle.
 * @no_wait: Never sleep, but rather return with -EBUSY.
 * @new_mem: struct ttm_mem_reg indicating where to move.
 *
 * Accelerated move function to be called when an accelerated move
 * has been scheduled. The function will create a new temporary buffer object
 * representing the old placement, and put the sync object on both buffer
 * objects. After that the newly created buffer object is unref'd to be
 * destroyed when the move is complete. This will help pipeline
 * buffer moves.
 */

extern int ttm_bo_move_accel_cleanup(struct ttm_buffer_object *bo,
				     void *sync_obj,
				     void *sync_obj_arg,
				     bool evict, bool no_wait,
				     struct ttm_mem_reg *new_mem);
/**
 * ttm_io_prot
 *
 * @c_state: Caching state.
 * @tmp: Page protection flag for a normal, cached mapping.
 *
 * Utility function that returns the pgprot_t that should be used for
 * setting up a PTE with the caching model indicated by @c_state.
 */
extern pgprot_t ttm_io_prot(uint32_t caching_flags, pgprot_t tmp);

#if (defined(CONFIG_AGP) || (defined(CONFIG_AGP_MODULE) && defined(MODULE)))
#define TTM_HAS_AGP
#include <linux/agp_backend.h>

/**
 * ttm_agp_backend_init
 *
 * @bdev: Pointer to a struct ttm_bo_device.
 * @bridge: The agp bridge this device is sitting on.
 *
 * Create a TTM backend that uses the indicated AGP bridge as an aperture
 * for TT memory. This function uses the linux agpgart interface to
 * bind and unbind memory backing a ttm_tt.
 */
extern struct ttm_backend *ttm_agp_backend_init(struct ttm_bo_device *bdev,
						struct agp_bridge_data *bridge);
#endif

#endif
