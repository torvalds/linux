/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __DRM_GEM_SHMEM_HELPER_H__
#define __DRM_GEM_SHMEM_HELPER_H__

#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/mutex.h>

#include <drm/drm_file.h>
#include <drm/drm_gem.h>
#include <drm/drm_ioctl.h>
#include <drm/drm_prime.h>

struct dma_buf_attachment;
struct drm_mode_create_dumb;
struct drm_printer;
struct sg_table;

/**
 * struct drm_gem_shmem_object - GEM object backed by shmem
 */
struct drm_gem_shmem_object {
	/**
	 * @base: Base GEM object
	 */
	struct drm_gem_object base;

	/**
	 * @pages: Page table
	 */
	struct page **pages;

	/**
	 * @pages_use_count:
	 *
	 * Reference count on the pages table.
	 * The pages are put when the count reaches zero.
	 */
	refcount_t pages_use_count;

	/**
	 * @pages_pin_count:
	 *
	 * Reference count on the pinned pages table.
	 *
	 * Pages are hard-pinned and reside in memory if count
	 * greater than zero. Otherwise, when count is zero, the pages are
	 * allowed to be evicted and purged by memory shrinker.
	 */
	refcount_t pages_pin_count;

	/**
	 * @madv: State for madvise
	 *
	 * 0 is active/inuse.
	 * A negative value is the object is purged.
	 * Positive values are driver specific and not used by the helpers.
	 */
	int madv;

	/**
	 * @madv_list: List entry for madvise tracking
	 *
	 * Typically used by drivers to track purgeable objects
	 */
	struct list_head madv_list;

	/**
	 * @sgt: Scatter/gather table for imported PRIME buffers
	 */
	struct sg_table *sgt;

	/**
	 * @vaddr: Kernel virtual address of the backing memory
	 */
	void *vaddr;

	/**
	 * @vmap_use_count:
	 *
	 * Reference count on the virtual address.
	 * The address are un-mapped when the count reaches zero.
	 */
	refcount_t vmap_use_count;

	/**
	 * @pages_mark_dirty_on_put:
	 *
	 * Mark pages as dirty when they are put.
	 */
	bool pages_mark_dirty_on_put : 1;

	/**
	 * @pages_mark_accessed_on_put:
	 *
	 * Mark pages as accessed when they are put.
	 */
	bool pages_mark_accessed_on_put : 1;

	/**
	 * @map_wc: map object write-combined (instead of using shmem defaults).
	 */
	bool map_wc : 1;
};

#define to_drm_gem_shmem_obj(obj) \
	container_of(obj, struct drm_gem_shmem_object, base)

struct drm_gem_shmem_object *drm_gem_shmem_create(struct drm_device *dev, size_t size);
struct drm_gem_shmem_object *drm_gem_shmem_create_with_mnt(struct drm_device *dev,
							   size_t size,
							   struct vfsmount *gemfs);
void drm_gem_shmem_free(struct drm_gem_shmem_object *shmem);

void drm_gem_shmem_put_pages_locked(struct drm_gem_shmem_object *shmem);
int drm_gem_shmem_pin(struct drm_gem_shmem_object *shmem);
void drm_gem_shmem_unpin(struct drm_gem_shmem_object *shmem);
int drm_gem_shmem_vmap_locked(struct drm_gem_shmem_object *shmem,
			      struct iosys_map *map);
void drm_gem_shmem_vunmap_locked(struct drm_gem_shmem_object *shmem,
				 struct iosys_map *map);
int drm_gem_shmem_mmap(struct drm_gem_shmem_object *shmem, struct vm_area_struct *vma);

int drm_gem_shmem_pin_locked(struct drm_gem_shmem_object *shmem);
void drm_gem_shmem_unpin_locked(struct drm_gem_shmem_object *shmem);

int drm_gem_shmem_madvise_locked(struct drm_gem_shmem_object *shmem, int madv);

static inline bool drm_gem_shmem_is_purgeable(struct drm_gem_shmem_object *shmem)
{
	return (shmem->madv > 0) &&
		!refcount_read(&shmem->pages_pin_count) && shmem->sgt &&
		!shmem->base.dma_buf && !drm_gem_is_imported(&shmem->base);
}

void drm_gem_shmem_purge_locked(struct drm_gem_shmem_object *shmem);

struct sg_table *drm_gem_shmem_get_sg_table(struct drm_gem_shmem_object *shmem);
struct sg_table *drm_gem_shmem_get_pages_sgt(struct drm_gem_shmem_object *shmem);

void drm_gem_shmem_print_info(const struct drm_gem_shmem_object *shmem,
			      struct drm_printer *p, unsigned int indent);

extern const struct vm_operations_struct drm_gem_shmem_vm_ops;

/*
 * GEM object functions
 */

/**
 * drm_gem_shmem_object_free - GEM object function for drm_gem_shmem_free()
 * @obj: GEM object to free
 *
 * This function wraps drm_gem_shmem_free(). Drivers that employ the shmem helpers
 * should use it as their &drm_gem_object_funcs.free handler.
 */
static inline void drm_gem_shmem_object_free(struct drm_gem_object *obj)
{
	struct drm_gem_shmem_object *shmem = to_drm_gem_shmem_obj(obj);

	drm_gem_shmem_free(shmem);
}

/**
 * drm_gem_shmem_object_print_info() - Print &drm_gem_shmem_object info for debugfs
 * @p: DRM printer
 * @indent: Tab indentation level
 * @obj: GEM object
 *
 * This function wraps drm_gem_shmem_print_info(). Drivers that employ the shmem helpers should
 * use this function as their &drm_gem_object_funcs.print_info handler.
 */
static inline void drm_gem_shmem_object_print_info(struct drm_printer *p, unsigned int indent,
						   const struct drm_gem_object *obj)
{
	const struct drm_gem_shmem_object *shmem = to_drm_gem_shmem_obj(obj);

	drm_gem_shmem_print_info(shmem, p, indent);
}

/**
 * drm_gem_shmem_object_pin - GEM object function for drm_gem_shmem_pin()
 * @obj: GEM object
 *
 * This function wraps drm_gem_shmem_pin(). Drivers that employ the shmem helpers should
 * use it as their &drm_gem_object_funcs.pin handler.
 */
static inline int drm_gem_shmem_object_pin(struct drm_gem_object *obj)
{
	struct drm_gem_shmem_object *shmem = to_drm_gem_shmem_obj(obj);

	return drm_gem_shmem_pin_locked(shmem);
}

/**
 * drm_gem_shmem_object_unpin - GEM object function for drm_gem_shmem_unpin()
 * @obj: GEM object
 *
 * This function wraps drm_gem_shmem_unpin(). Drivers that employ the shmem helpers should
 * use it as their &drm_gem_object_funcs.unpin handler.
 */
static inline void drm_gem_shmem_object_unpin(struct drm_gem_object *obj)
{
	struct drm_gem_shmem_object *shmem = to_drm_gem_shmem_obj(obj);

	drm_gem_shmem_unpin_locked(shmem);
}

/**
 * drm_gem_shmem_object_get_sg_table - GEM object function for drm_gem_shmem_get_sg_table()
 * @obj: GEM object
 *
 * This function wraps drm_gem_shmem_get_sg_table(). Drivers that employ the shmem helpers should
 * use it as their &drm_gem_object_funcs.get_sg_table handler.
 *
 * Returns:
 * A pointer to the scatter/gather table of pinned pages or error pointer on failure.
 */
static inline struct sg_table *drm_gem_shmem_object_get_sg_table(struct drm_gem_object *obj)
{
	struct drm_gem_shmem_object *shmem = to_drm_gem_shmem_obj(obj);

	return drm_gem_shmem_get_sg_table(shmem);
}

/*
 * drm_gem_shmem_object_vmap - GEM object function for drm_gem_shmem_vmap_locked()
 * @obj: GEM object
 * @map: Returns the kernel virtual address of the SHMEM GEM object's backing store.
 *
 * This function wraps drm_gem_shmem_vmap_locked(). Drivers that employ the shmem
 * helpers should use it as their &drm_gem_object_funcs.vmap handler.
 *
 * Returns:
 * 0 on success or a negative error code on failure.
 */
static inline int drm_gem_shmem_object_vmap(struct drm_gem_object *obj,
					    struct iosys_map *map)
{
	struct drm_gem_shmem_object *shmem = to_drm_gem_shmem_obj(obj);

	return drm_gem_shmem_vmap_locked(shmem, map);
}

/*
 * drm_gem_shmem_object_vunmap - GEM object function for drm_gem_shmem_vunmap()
 * @obj: GEM object
 * @map: Kernel virtual address where the SHMEM GEM object was mapped
 *
 * This function wraps drm_gem_shmem_vunmap_locked(). Drivers that employ the shmem
 * helpers should use it as their &drm_gem_object_funcs.vunmap handler.
 */
static inline void drm_gem_shmem_object_vunmap(struct drm_gem_object *obj,
					       struct iosys_map *map)
{
	struct drm_gem_shmem_object *shmem = to_drm_gem_shmem_obj(obj);

	drm_gem_shmem_vunmap_locked(shmem, map);
}

/**
 * drm_gem_shmem_object_mmap - GEM object function for drm_gem_shmem_mmap()
 * @obj: GEM object
 * @vma: VMA for the area to be mapped
 *
 * This function wraps drm_gem_shmem_mmap(). Drivers that employ the shmem helpers should
 * use it as their &drm_gem_object_funcs.mmap handler.
 *
 * Returns:
 * 0 on success or a negative error code on failure.
 */
static inline int drm_gem_shmem_object_mmap(struct drm_gem_object *obj, struct vm_area_struct *vma)
{
	struct drm_gem_shmem_object *shmem = to_drm_gem_shmem_obj(obj);

	return drm_gem_shmem_mmap(shmem, vma);
}

/*
 * Driver ops
 */

struct drm_gem_object *
drm_gem_shmem_prime_import_sg_table(struct drm_device *dev,
				    struct dma_buf_attachment *attach,
				    struct sg_table *sgt);
int drm_gem_shmem_dumb_create(struct drm_file *file, struct drm_device *dev,
			      struct drm_mode_create_dumb *args);
struct drm_gem_object *drm_gem_shmem_prime_import_no_map(struct drm_device *dev,
							 struct dma_buf *buf);

/**
 * DRM_GEM_SHMEM_DRIVER_OPS - Default shmem GEM operations
 *
 * This macro provides a shortcut for setting the shmem GEM operations
 * in the &drm_driver structure. Drivers that do not require an s/g table
 * for imported buffers should use this.
 */
#define DRM_GEM_SHMEM_DRIVER_OPS \
	.gem_prime_import       = drm_gem_shmem_prime_import_no_map, \
	.dumb_create            = drm_gem_shmem_dumb_create

#endif /* __DRM_GEM_SHMEM_HELPER_H__ */
