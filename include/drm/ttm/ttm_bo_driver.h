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
#include "drm_global.h"
#include "linux/workqueue.h"
#include "linux/fs.h"
#include "linux/spinlock.h"

struct ttm_backend;

struct ttm_backend_func {
	/**
	 * struct ttm_backend_func member bind
	 *
	 * @ttm: Pointer to a struct ttm_tt.
	 * @bo_mem: Pointer to a struct ttm_mem_reg describing the
	 * memory type and location for binding.
	 *
	 * Bind the backend pages into the aperture in the location
	 * indicated by @bo_mem. This function should be able to handle
	 * differences between aperture and system page sizes.
	 */
	int (*bind) (struct ttm_tt *ttm, struct ttm_mem_reg *bo_mem);

	/**
	 * struct ttm_backend_func member unbind
	 *
	 * @ttm: Pointer to a struct ttm_tt.
	 *
	 * Unbind previously bound backend pages. This function should be
	 * able to handle differences between aperture and system page sizes.
	 */
	int (*unbind) (struct ttm_tt *ttm);

	/**
	 * struct ttm_backend_func member destroy
	 *
	 * @ttm: Pointer to a struct ttm_tt.
	 *
	 * Destroy the backend. This will be call back from ttm_tt_destroy so
	 * don't call ttm_tt_destroy from the callback or infinite loop.
	 */
	void (*destroy) (struct ttm_tt *ttm);
};

#define TTM_PAGE_FLAG_WRITE           (1 << 3)
#define TTM_PAGE_FLAG_SWAPPED         (1 << 4)
#define TTM_PAGE_FLAG_PERSISTENT_SWAP (1 << 5)
#define TTM_PAGE_FLAG_ZERO_ALLOC      (1 << 6)
#define TTM_PAGE_FLAG_DMA32           (1 << 7)
#define TTM_PAGE_FLAG_SG              (1 << 8)

enum ttm_caching_state {
	tt_uncached,
	tt_wc,
	tt_cached
};

/**
 * struct ttm_tt
 *
 * @bdev: Pointer to a struct ttm_bo_device.
 * @func: Pointer to a struct ttm_backend_func that describes
 * the backend methods.
 * @dummy_read_page: Page to map where the ttm_tt page array contains a NULL
 * pointer.
 * @pages: Array of pages backing the data.
 * @num_pages: Number of pages in the page array.
 * @bdev: Pointer to the current struct ttm_bo_device.
 * @be: Pointer to the ttm backend.
 * @swap_storage: Pointer to shmem struct file for swap storage.
 * @caching_state: The current caching state of the pages.
 * @state: The current binding state of the pages.
 *
 * This is a structure holding the pages, caching- and aperture binding
 * status for a buffer object that isn't backed by fixed (VRAM / AGP)
 * memory.
 */

struct ttm_tt {
	struct ttm_bo_device *bdev;
	struct ttm_backend_func *func;
	struct page *dummy_read_page;
	struct page **pages;
	uint32_t page_flags;
	unsigned long num_pages;
	struct sg_table *sg; /* for SG objects via dma-buf */
	struct ttm_bo_global *glob;
	struct ttm_backend *be;
	struct file *swap_storage;
	enum ttm_caching_state caching_state;
	enum {
		tt_bound,
		tt_unbound,
		tt_unpopulated,
	} state;
};

/**
 * struct ttm_dma_tt
 *
 * @ttm: Base ttm_tt struct.
 * @dma_address: The DMA (bus) addresses of the pages
 * @pages_list: used by some page allocation backend
 *
 * This is a structure holding the pages, caching- and aperture binding
 * status for a buffer object that isn't backed by fixed (VRAM / AGP)
 * memory.
 */
struct ttm_dma_tt {
	struct ttm_tt ttm;
	dma_addr_t *dma_address;
	struct list_head pages_list;
};

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
			 struct ttm_placement *placement,
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
	 * @prefix: Prefix to be used in printout to identify the caller.
	 *
	 * This function is called to print out the state of the memory
	 * type manager to aid debugging of out-of-memory conditions.
	 * It may not be called from within atomic context.
	 */
	void (*debug)(struct ttm_mem_type_manager *man, const char *prefix);
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
 * static information. bdev::driver::io_mem_free is never used.
 * @lru: The lru list for this memory type.
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
	unsigned long gpu_offset;
	uint64_t size;
	uint32_t available_caching;
	uint32_t default_caching;
	const struct ttm_mem_type_manager_func *func;
	void *priv;
	struct mutex io_reserve_mutex;
	bool use_io_reserve_lru;
	bool io_reserve_fastpath;

	/*
	 * Protected by @io_reserve_mutex:
	 */

	struct list_head io_reserve_lru;

	/*
	 * Protected by the global->lru_lock.
	 */

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
	struct ttm_tt *(*ttm_tt_create)(struct ttm_bo_device *bdev,
					unsigned long size,
					uint32_t page_flags,
					struct page *dummy_read_page);

	/**
	 * ttm_tt_populate
	 *
	 * @ttm: The struct ttm_tt to contain the backing pages.
	 *
	 * Allocate all backing pages
	 * Returns:
	 * -ENOMEM: Out of memory.
	 */
	int (*ttm_tt_populate)(struct ttm_tt *ttm);

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
		     bool no_wait_reserve, bool no_wait_gpu,
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
	int (*fault_reserve_notify)(struct ttm_buffer_object *bo);

	/**
	 * notify the driver that we're about to swap out this bo
	 */
	void (*swap_notify) (struct ttm_buffer_object *bo);

	/**
	 * Driver callback on when mapping io memory (for bo_move_memcpy
	 * for instance). TTM will take care to call io_mem_free whenever
	 * the mapping is not use anymore. io_mem_reserve & io_mem_free
	 * are balanced.
	 */
	int (*io_mem_reserve)(struct ttm_bo_device *bdev, struct ttm_mem_reg *mem);
	void (*io_mem_free)(struct ttm_bo_device *bdev, struct ttm_mem_reg *mem);
};

/**
 * struct ttm_bo_global_ref - Argument to initialize a struct ttm_bo_global.
 */

struct ttm_bo_global_ref {
	struct drm_global_reference ref;
	struct ttm_mem_global *mem_glob;
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

struct ttm_bo_global {

	/**
	 * Constant after init.
	 */

	struct kobject kobj;
	struct ttm_mem_global *mem_glob;
	struct page *dummy_read_page;
	struct ttm_mem_shrink shrink;
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
 * @fence_lock: Protects the synchronizing members on *all* bos belonging
 * to this device.
 * @addr_space_mm: Range manager for the device address space.
 * lru_lock: Spinlock that protects the buffer+device lru lists and
 * ddestroy lists.
 * @val_seq: Current validation sequence.
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
	spinlock_t fence_lock;
	/*
	 * Protected by the vm lock.
	 */
	struct rb_root addr_space_rb;
	struct drm_mm addr_space_mm;

	/*
	 * Protected by the global:lru lock.
	 */
	struct list_head ddestroy;
	uint32_t val_seq;

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
 * ttm_tt_init
 *
 * @ttm: The struct ttm_tt.
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
extern int ttm_tt_init(struct ttm_tt *ttm, struct ttm_bo_device *bdev,
			unsigned long size, uint32_t page_flags,
			struct page *dummy_read_page);
extern int ttm_dma_tt_init(struct ttm_dma_tt *ttm_dma, struct ttm_bo_device *bdev,
			   unsigned long size, uint32_t page_flags,
			   struct page *dummy_read_page);

/**
 * ttm_tt_fini
 *
 * @ttm: the ttm_tt structure.
 *
 * Free memory of ttm_tt structure
 */
extern void ttm_tt_fini(struct ttm_tt *ttm);
extern void ttm_dma_tt_fini(struct ttm_dma_tt *ttm_dma);

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
 * ttm_ttm_destroy:
 *
 * @ttm: The struct ttm_tt.
 *
 * Unbind, unpopulate and destroy common struct ttm_tt.
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
 * ttm_tt_swapin:
 *
 * @ttm: The struct ttm_tt.
 *
 * Swap in a previously swap out ttm_tt.
 */
extern int ttm_tt_swapin(struct ttm_tt *ttm);

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
			  struct file *persistent_swap_storage);

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
 * @no_wait_reserve: Return immediately if other buffers are busy.
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
extern int ttm_bo_mem_space(struct ttm_buffer_object *bo,
				struct ttm_placement *placement,
				struct ttm_mem_reg *mem,
				bool interruptible,
				bool no_wait_reserve, bool no_wait_gpu);

extern void ttm_bo_mem_put(struct ttm_buffer_object *bo,
			   struct ttm_mem_reg *mem);
extern void ttm_bo_mem_put_locked(struct ttm_buffer_object *bo,
				  struct ttm_mem_reg *mem);

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

extern void ttm_bo_global_release(struct drm_global_reference *ref);
extern int ttm_bo_global_init(struct drm_global_reference *ref);

extern int ttm_bo_device_release(struct ttm_bo_device *bdev);

/**
 * ttm_bo_device_init
 *
 * @bdev: A pointer to a struct ttm_bo_device to initialize.
 * @glob: A pointer to an initialized struct ttm_bo_global.
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
 * ttm_bo_unmap_virtual
 *
 * @bo: tear down the virtual mappings for this BO
 *
 * The caller must take ttm_mem_io_lock before calling this function.
 */
extern void ttm_bo_unmap_virtual_locked(struct ttm_buffer_object *bo);

extern int ttm_mem_io_reserve_vm(struct ttm_buffer_object *bo);
extern void ttm_mem_io_free_vm(struct ttm_buffer_object *bo);
extern int ttm_mem_io_lock(struct ttm_mem_type_manager *man,
			   bool interruptible);
extern void ttm_mem_io_unlock(struct ttm_mem_type_manager *man);


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
 * occurring:
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
 * -EBUSY: The function needed to sleep, but @no_wait was true
 * -EDEADLK: Bo already reserved using @sequence. This error code will only
 * be returned if @use_sequence is set to true.
 */
extern int ttm_bo_reserve(struct ttm_buffer_object *bo,
			  bool interruptible,
			  bool no_wait, bool use_sequence, uint32_t sequence);


/**
 * ttm_bo_reserve_locked:
 *
 * @bo: A pointer to a struct ttm_buffer_object.
 * @interruptible: Sleep interruptible if waiting.
 * @no_wait: Don't sleep while trying to reserve, rather return -EBUSY.
 * @use_sequence: If @bo is already reserved, Only sleep waiting for
 * it to become unreserved if @sequence < (@bo)->sequence.
 *
 * Must be called with struct ttm_bo_global::lru_lock held,
 * and will not remove reserved buffers from the lru lists.
 * The function may release the LRU spinlock if it needs to sleep.
 * Otherwise identical to ttm_bo_reserve.
 *
 * Returns:
 * -EAGAIN: The reservation may cause a deadlock.
 * Release all buffer reservations, wait for @bo to become unreserved and
 * try again. (only if use_sequence == 1).
 * -ERESTARTSYS: A wait for the buffer to become unreserved was interrupted by
 * a signal. Release all buffer reservations and return to user-space.
 * -EBUSY: The function needed to sleep, but @no_wait was true
 * -EDEADLK: Bo already reserved using @sequence. This error code will only
 * be returned if @use_sequence is set to true.
 */
extern int ttm_bo_reserve_locked(struct ttm_buffer_object *bo,
				 bool interruptible,
				 bool no_wait, bool use_sequence,
				 uint32_t sequence);

/**
 * ttm_bo_unreserve
 *
 * @bo: A pointer to a struct ttm_buffer_object.
 *
 * Unreserve a previous reservation of @bo.
 */
extern void ttm_bo_unreserve(struct ttm_buffer_object *bo);

/**
 * ttm_bo_unreserve_locked
 *
 * @bo: A pointer to a struct ttm_buffer_object.
 *
 * Unreserve a previous reservation of @bo.
 * Needs to be called with struct ttm_bo_global::lru_lock held.
 */
extern void ttm_bo_unreserve_locked(struct ttm_buffer_object *bo);

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
 * @no_wait_reserve: Return immediately if other buffers are busy.
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

extern int ttm_bo_move_ttm(struct ttm_buffer_object *bo,
			   bool evict, bool no_wait_reserve,
			   bool no_wait_gpu, struct ttm_mem_reg *new_mem);

/**
 * ttm_bo_move_memcpy
 *
 * @bo: A pointer to a struct ttm_buffer_object.
 * @evict: 1: This is an eviction. Don't try to pipeline.
 * @no_wait_reserve: Return immediately if other buffers are busy.
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

extern int ttm_bo_move_memcpy(struct ttm_buffer_object *bo,
			      bool evict, bool no_wait_reserve,
			      bool no_wait_gpu, struct ttm_mem_reg *new_mem);

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
 * @no_wait_reserve: Return immediately if other buffers are busy.
 * @no_wait_gpu: Return immediately if the GPU is busy.
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
				     bool evict, bool no_wait_reserve,
				     bool no_wait_gpu,
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

extern const struct ttm_mem_type_manager_func ttm_bo_manager_func;

#if (defined(CONFIG_AGP) || (defined(CONFIG_AGP_MODULE) && defined(MODULE)))
#define TTM_HAS_AGP
#include <linux/agp_backend.h>

/**
 * ttm_agp_tt_create
 *
 * @bdev: Pointer to a struct ttm_bo_device.
 * @bridge: The agp bridge this device is sitting on.
 * @size: Size of the data needed backing.
 * @page_flags: Page flags as identified by TTM_PAGE_FLAG_XX flags.
 * @dummy_read_page: See struct ttm_bo_device.
 *
 *
 * Create a TTM backend that uses the indicated AGP bridge as an aperture
 * for TT memory. This function uses the linux agpgart interface to
 * bind and unbind memory backing a ttm_tt.
 */
extern struct ttm_tt *ttm_agp_tt_create(struct ttm_bo_device *bdev,
					struct agp_bridge_data *bridge,
					unsigned long size, uint32_t page_flags,
					struct page *dummy_read_page);
int ttm_agp_tt_populate(struct ttm_tt *ttm);
void ttm_agp_tt_unpopulate(struct ttm_tt *ttm);
#endif

#endif
