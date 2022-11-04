#ifndef __DRM_GEM_H__
#define __DRM_GEM_H__

/*
 * GEM Graphics Execution Manager Driver Interfaces
 *
 * Copyright 1999 Precision Insight, Inc., Cedar Park, Texas.
 * Copyright 2000 VA Linux Systems, Inc., Sunnyvale, California.
 * Copyright (c) 2009-2010, Code Aurora Forum.
 * All rights reserved.
 * Copyright © 2014 Intel Corporation
 *   Daniel Vetter <daniel.vetter@ffwll.ch>
 *
 * Author: Rickard E. (Rik) Faith <faith@valinux.com>
 * Author: Gareth Hughes <gareth@valinux.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * VA LINUX SYSTEMS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#include <linux/kref.h>
#include <linux/dma-resv.h>

#include <drm/drm_vma_manager.h>

struct iosys_map;
struct drm_gem_object;

/**
 * struct drm_gem_object_funcs - GEM object functions
 */
struct drm_gem_object_funcs {
	/**
	 * @free:
	 *
	 * Deconstructor for drm_gem_objects.
	 *
	 * This callback is mandatory.
	 */
	void (*free)(struct drm_gem_object *obj);

	/**
	 * @open:
	 *
	 * Called upon GEM handle creation.
	 *
	 * This callback is optional.
	 */
	int (*open)(struct drm_gem_object *obj, struct drm_file *file);

	/**
	 * @close:
	 *
	 * Called upon GEM handle release.
	 *
	 * This callback is optional.
	 */
	void (*close)(struct drm_gem_object *obj, struct drm_file *file);

	/**
	 * @print_info:
	 *
	 * If driver subclasses struct &drm_gem_object, it can implement this
	 * optional hook for printing additional driver specific info.
	 *
	 * drm_printf_indent() should be used in the callback passing it the
	 * indent argument.
	 *
	 * This callback is called from drm_gem_print_info().
	 *
	 * This callback is optional.
	 */
	void (*print_info)(struct drm_printer *p, unsigned int indent,
			   const struct drm_gem_object *obj);

	/**
	 * @export:
	 *
	 * Export backing buffer as a &dma_buf.
	 * If this is not set drm_gem_prime_export() is used.
	 *
	 * This callback is optional.
	 */
	struct dma_buf *(*export)(struct drm_gem_object *obj, int flags);

	/**
	 * @pin:
	 *
	 * Pin backing buffer in memory. Used by the drm_gem_map_attach() helper.
	 *
	 * This callback is optional.
	 */
	int (*pin)(struct drm_gem_object *obj);

	/**
	 * @unpin:
	 *
	 * Unpin backing buffer. Used by the drm_gem_map_detach() helper.
	 *
	 * This callback is optional.
	 */
	void (*unpin)(struct drm_gem_object *obj);

	/**
	 * @get_sg_table:
	 *
	 * Returns a Scatter-Gather table representation of the buffer.
	 * Used when exporting a buffer by the drm_gem_map_dma_buf() helper.
	 * Releasing is done by calling dma_unmap_sg_attrs() and sg_free_table()
	 * in drm_gem_unmap_buf(), therefore these helpers and this callback
	 * here cannot be used for sg tables pointing at driver private memory
	 * ranges.
	 *
	 * See also drm_prime_pages_to_sg().
	 */
	struct sg_table *(*get_sg_table)(struct drm_gem_object *obj);

	/**
	 * @vmap:
	 *
	 * Returns a virtual address for the buffer. Used by the
	 * drm_gem_dmabuf_vmap() helper.
	 *
	 * This callback is optional.
	 */
	int (*vmap)(struct drm_gem_object *obj, struct iosys_map *map);

	/**
	 * @vunmap:
	 *
	 * Releases the address previously returned by @vmap. Used by the
	 * drm_gem_dmabuf_vunmap() helper.
	 *
	 * This callback is optional.
	 */
	void (*vunmap)(struct drm_gem_object *obj, struct iosys_map *map);

	/**
	 * @mmap:
	 *
	 * Handle mmap() of the gem object, setup vma accordingly.
	 *
	 * This callback is optional.
	 *
	 * The callback is used by both drm_gem_mmap_obj() and
	 * drm_gem_prime_mmap().  When @mmap is present @vm_ops is not
	 * used, the @mmap callback must set vma->vm_ops instead.
	 */
	int (*mmap)(struct drm_gem_object *obj, struct vm_area_struct *vma);

	/**
	 * @vm_ops:
	 *
	 * Virtual memory operations used with mmap.
	 *
	 * This is optional but necessary for mmap support.
	 */
	const struct vm_operations_struct *vm_ops;
};

/**
 * struct drm_gem_lru - A simple LRU helper
 *
 * A helper for tracking GEM objects in a given state, to aid in
 * driver's shrinker implementation.  Tracks the count of pages
 * for lockless &shrinker.count_objects, and provides
 * &drm_gem_lru_scan for driver's &shrinker.scan_objects
 * implementation.
 */
struct drm_gem_lru {
	/**
	 * @lock:
	 *
	 * Lock protecting movement of GEM objects between LRUs.  All
	 * LRUs that the object can move between should be protected
	 * by the same lock.
	 */
	struct mutex *lock;

	/**
	 * @count:
	 *
	 * The total number of backing pages of the GEM objects in
	 * this LRU.
	 */
	long count;

	/**
	 * @list:
	 *
	 * The LRU list.
	 */
	struct list_head list;
};

/**
 * struct drm_gem_object - GEM buffer object
 *
 * This structure defines the generic parts for GEM buffer objects, which are
 * mostly around handling mmap and userspace handles.
 *
 * Buffer objects are often abbreviated to BO.
 */
struct drm_gem_object {
	/**
	 * @refcount:
	 *
	 * Reference count of this object
	 *
	 * Please use drm_gem_object_get() to acquire and drm_gem_object_put_locked()
	 * or drm_gem_object_put() to release a reference to a GEM
	 * buffer object.
	 */
	struct kref refcount;

	/**
	 * @handle_count:
	 *
	 * This is the GEM file_priv handle count of this object.
	 *
	 * Each handle also holds a reference. Note that when the handle_count
	 * drops to 0 any global names (e.g. the id in the flink namespace) will
	 * be cleared.
	 *
	 * Protected by &drm_device.object_name_lock.
	 */
	unsigned handle_count;

	/**
	 * @dev: DRM dev this object belongs to.
	 */
	struct drm_device *dev;

	/**
	 * @filp:
	 *
	 * SHMEM file node used as backing storage for swappable buffer objects.
	 * GEM also supports driver private objects with driver-specific backing
	 * storage (contiguous DMA memory, special reserved blocks). In this
	 * case @filp is NULL.
	 */
	struct file *filp;

	/**
	 * @vma_node:
	 *
	 * Mapping info for this object to support mmap. Drivers are supposed to
	 * allocate the mmap offset using drm_gem_create_mmap_offset(). The
	 * offset itself can be retrieved using drm_vma_node_offset_addr().
	 *
	 * Memory mapping itself is handled by drm_gem_mmap(), which also checks
	 * that userspace is allowed to access the object.
	 */
	struct drm_vma_offset_node vma_node;

	/**
	 * @size:
	 *
	 * Size of the object, in bytes.  Immutable over the object's
	 * lifetime.
	 */
	size_t size;

	/**
	 * @name:
	 *
	 * Global name for this object, starts at 1. 0 means unnamed.
	 * Access is covered by &drm_device.object_name_lock. This is used by
	 * the GEM_FLINK and GEM_OPEN ioctls.
	 */
	int name;

	/**
	 * @dma_buf:
	 *
	 * dma-buf associated with this GEM object.
	 *
	 * Pointer to the dma-buf associated with this gem object (either
	 * through importing or exporting). We break the resulting reference
	 * loop when the last gem handle for this object is released.
	 *
	 * Protected by &drm_device.object_name_lock.
	 */
	struct dma_buf *dma_buf;

	/**
	 * @import_attach:
	 *
	 * dma-buf attachment backing this object.
	 *
	 * Any foreign dma_buf imported as a gem object has this set to the
	 * attachment point for the device. This is invariant over the lifetime
	 * of a gem object.
	 *
	 * The &drm_gem_object_funcs.free callback is responsible for
	 * cleaning up the dma_buf attachment and references acquired at import
	 * time.
	 *
	 * Note that the drm gem/prime core does not depend upon drivers setting
	 * this field any more. So for drivers where this doesn't make sense
	 * (e.g. virtual devices or a displaylink behind an usb bus) they can
	 * simply leave it as NULL.
	 */
	struct dma_buf_attachment *import_attach;

	/**
	 * @resv:
	 *
	 * Pointer to reservation object associated with the this GEM object.
	 *
	 * Normally (@resv == &@_resv) except for imported GEM objects.
	 */
	struct dma_resv *resv;

	/**
	 * @_resv:
	 *
	 * A reservation object for this GEM object.
	 *
	 * This is unused for imported GEM objects.
	 */
	struct dma_resv _resv;

	/**
	 * @funcs:
	 *
	 * Optional GEM object functions. If this is set, it will be used instead of the
	 * corresponding &drm_driver GEM callbacks.
	 *
	 * New drivers should use this.
	 *
	 */
	const struct drm_gem_object_funcs *funcs;

	/**
	 * @lru_node:
	 *
	 * List node in a &drm_gem_lru.
	 */
	struct list_head lru_node;

	/**
	 * @lru:
	 *
	 * The current LRU list that the GEM object is on.
	 */
	struct drm_gem_lru *lru;
};

/**
 * DRM_GEM_FOPS - Default drm GEM file operations
 *
 * This macro provides a shorthand for setting the GEM file ops in the
 * &file_operations structure.  If all you need are the default ops, use
 * DEFINE_DRM_GEM_FOPS instead.
 */
#define DRM_GEM_FOPS \
	.open		= drm_open,\
	.release	= drm_release,\
	.unlocked_ioctl	= drm_ioctl,\
	.compat_ioctl	= drm_compat_ioctl,\
	.poll		= drm_poll,\
	.read		= drm_read,\
	.llseek		= noop_llseek,\
	.mmap		= drm_gem_mmap

/**
 * DEFINE_DRM_GEM_FOPS() - macro to generate file operations for GEM drivers
 * @name: name for the generated structure
 *
 * This macro autogenerates a suitable &struct file_operations for GEM based
 * drivers, which can be assigned to &drm_driver.fops. Note that this structure
 * cannot be shared between drivers, because it contains a reference to the
 * current module using THIS_MODULE.
 *
 * Note that the declaration is already marked as static - if you need a
 * non-static version of this you're probably doing it wrong and will break the
 * THIS_MODULE reference by accident.
 */
#define DEFINE_DRM_GEM_FOPS(name) \
	static const struct file_operations name = {\
		.owner		= THIS_MODULE,\
		DRM_GEM_FOPS,\
	}

void drm_gem_object_release(struct drm_gem_object *obj);
void drm_gem_object_free(struct kref *kref);
int drm_gem_object_init(struct drm_device *dev,
			struct drm_gem_object *obj, size_t size);
void drm_gem_private_object_init(struct drm_device *dev,
				 struct drm_gem_object *obj, size_t size);
void drm_gem_vm_open(struct vm_area_struct *vma);
void drm_gem_vm_close(struct vm_area_struct *vma);
int drm_gem_mmap_obj(struct drm_gem_object *obj, unsigned long obj_size,
		     struct vm_area_struct *vma);
int drm_gem_mmap(struct file *filp, struct vm_area_struct *vma);

/**
 * drm_gem_object_get - acquire a GEM buffer object reference
 * @obj: GEM buffer object
 *
 * This function acquires an additional reference to @obj. It is illegal to
 * call this without already holding a reference. No locks required.
 */
static inline void drm_gem_object_get(struct drm_gem_object *obj)
{
	kref_get(&obj->refcount);
}

__attribute__((nonnull))
static inline void
__drm_gem_object_put(struct drm_gem_object *obj)
{
	kref_put(&obj->refcount, drm_gem_object_free);
}

/**
 * drm_gem_object_put - drop a GEM buffer object reference
 * @obj: GEM buffer object
 *
 * This releases a reference to @obj.
 */
static inline void
drm_gem_object_put(struct drm_gem_object *obj)
{
	if (obj)
		__drm_gem_object_put(obj);
}

int drm_gem_handle_create(struct drm_file *file_priv,
			  struct drm_gem_object *obj,
			  u32 *handlep);
int drm_gem_handle_delete(struct drm_file *filp, u32 handle);


void drm_gem_free_mmap_offset(struct drm_gem_object *obj);
int drm_gem_create_mmap_offset(struct drm_gem_object *obj);
int drm_gem_create_mmap_offset_size(struct drm_gem_object *obj, size_t size);

struct page **drm_gem_get_pages(struct drm_gem_object *obj);
void drm_gem_put_pages(struct drm_gem_object *obj, struct page **pages,
		bool dirty, bool accessed);

int drm_gem_vmap_unlocked(struct drm_gem_object *obj, struct iosys_map *map);
void drm_gem_vunmap_unlocked(struct drm_gem_object *obj, struct iosys_map *map);

int drm_gem_objects_lookup(struct drm_file *filp, void __user *bo_handles,
			   int count, struct drm_gem_object ***objs_out);
struct drm_gem_object *drm_gem_object_lookup(struct drm_file *filp, u32 handle);
long drm_gem_dma_resv_wait(struct drm_file *filep, u32 handle,
				    bool wait_all, unsigned long timeout);
int drm_gem_lock_reservations(struct drm_gem_object **objs, int count,
			      struct ww_acquire_ctx *acquire_ctx);
void drm_gem_unlock_reservations(struct drm_gem_object **objs, int count,
				 struct ww_acquire_ctx *acquire_ctx);
int drm_gem_dumb_map_offset(struct drm_file *file, struct drm_device *dev,
			    u32 handle, u64 *offset);

void drm_gem_lru_init(struct drm_gem_lru *lru, struct mutex *lock);
void drm_gem_lru_remove(struct drm_gem_object *obj);
void drm_gem_lru_move_tail(struct drm_gem_lru *lru, struct drm_gem_object *obj);
unsigned long drm_gem_lru_scan(struct drm_gem_lru *lru,
			       unsigned int nr_to_scan,
			       unsigned long *remaining,
			       bool (*shrink)(struct drm_gem_object *obj));

#endif /* __DRM_GEM_H__ */
