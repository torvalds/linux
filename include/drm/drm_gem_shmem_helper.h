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
	 * @pages_lock: Protects the page table and use count
	 */
	struct mutex pages_lock;

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
	unsigned int pages_use_count;

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
	 * @pages_mark_dirty_on_put:
	 *
	 * Mark pages as dirty when they are put.
	 */
	unsigned int pages_mark_dirty_on_put    : 1;

	/**
	 * @pages_mark_accessed_on_put:
	 *
	 * Mark pages as accessed when they are put.
	 */
	unsigned int pages_mark_accessed_on_put : 1;

	/**
	 * @sgt: Scatter/gather table for imported PRIME buffers
	 */
	struct sg_table *sgt;

	/**
	 * @vmap_lock: Protects the vmap address and use count
	 */
	struct mutex vmap_lock;

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
	unsigned int vmap_use_count;

	/**
	 * @map_cached: map object cached (instead of using writecombine).
	 */
	bool map_cached;
};

#define to_drm_gem_shmem_obj(obj) \
	container_of(obj, struct drm_gem_shmem_object, base)

struct drm_gem_shmem_object *drm_gem_shmem_create(struct drm_device *dev, size_t size);
void drm_gem_shmem_free_object(struct drm_gem_object *obj);

int drm_gem_shmem_get_pages(struct drm_gem_shmem_object *shmem);
void drm_gem_shmem_put_pages(struct drm_gem_shmem_object *shmem);
int drm_gem_shmem_pin(struct drm_gem_object *obj);
void drm_gem_shmem_unpin(struct drm_gem_object *obj);
int drm_gem_shmem_vmap(struct drm_gem_object *obj, struct dma_buf_map *map);
void drm_gem_shmem_vunmap(struct drm_gem_object *obj, struct dma_buf_map *map);

int drm_gem_shmem_madvise(struct drm_gem_object *obj, int madv);

static inline bool drm_gem_shmem_is_purgeable(struct drm_gem_shmem_object *shmem)
{
	return (shmem->madv > 0) &&
		!shmem->vmap_use_count && shmem->sgt &&
		!shmem->base.dma_buf && !shmem->base.import_attach;
}

void drm_gem_shmem_purge_locked(struct drm_gem_object *obj);
bool drm_gem_shmem_purge(struct drm_gem_object *obj);

struct drm_gem_shmem_object *
drm_gem_shmem_create_with_handle(struct drm_file *file_priv,
				 struct drm_device *dev, size_t size,
				 uint32_t *handle);

struct drm_gem_object *
drm_gem_shmem_create_object_cached(struct drm_device *dev, size_t size);

int drm_gem_shmem_dumb_create(struct drm_file *file, struct drm_device *dev,
			      struct drm_mode_create_dumb *args);

int drm_gem_shmem_mmap(struct drm_gem_object *obj, struct vm_area_struct *vma);

void drm_gem_shmem_print_info(struct drm_printer *p, unsigned int indent,
			      const struct drm_gem_object *obj);

struct sg_table *drm_gem_shmem_get_sg_table(struct drm_gem_object *obj);
struct drm_gem_object *
drm_gem_shmem_prime_import_sg_table(struct drm_device *dev,
				    struct dma_buf_attachment *attach,
				    struct sg_table *sgt);

struct sg_table *drm_gem_shmem_get_pages_sgt(struct drm_gem_object *obj);

/**
 * DRM_GEM_SHMEM_DRIVER_OPS - Default shmem GEM operations
 *
 * This macro provides a shortcut for setting the shmem GEM operations in
 * the &drm_driver structure.
 */
#define DRM_GEM_SHMEM_DRIVER_OPS \
	.prime_handle_to_fd	= drm_gem_prime_handle_to_fd, \
	.prime_fd_to_handle	= drm_gem_prime_fd_to_handle, \
	.gem_prime_import_sg_table = drm_gem_shmem_prime_import_sg_table, \
	.gem_prime_mmap		= drm_gem_prime_mmap, \
	.dumb_create		= drm_gem_shmem_dumb_create

#endif /* __DRM_GEM_SHMEM_HELPER_H__ */
