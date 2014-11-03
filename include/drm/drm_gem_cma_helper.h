#ifndef __DRM_GEM_CMA_HELPER_H__
#define __DRM_GEM_CMA_HELPER_H__

#include <drm/drmP.h>
#include <drm/drm_gem.h>

/**
 * struct drm_gem_cma_object - GEM object backed by CMA memory allocations
 * @base: base GEM object
 * @paddr: physical address of the backing memory
 * @sgt: scatter/gather table for imported PRIME buffers
 * @vaddr: kernel virtual address of the backing memory
 */
struct drm_gem_cma_object {
	struct drm_gem_object base;
	dma_addr_t paddr;
	struct sg_table *sgt;

	/* For objects with DMA memory allocated by GEM CMA */
	void *vaddr;
};

static inline struct drm_gem_cma_object *
to_drm_gem_cma_obj(struct drm_gem_object *gem_obj)
{
	return container_of(gem_obj, struct drm_gem_cma_object, base);
}

/* free GEM object */
void drm_gem_cma_free_object(struct drm_gem_object *gem_obj);

/* create memory region for DRM framebuffer */
int drm_gem_cma_dumb_create_internal(struct drm_file *file_priv,
				     struct drm_device *drm,
				     struct drm_mode_create_dumb *args);

/* create memory region for DRM framebuffer */
int drm_gem_cma_dumb_create(struct drm_file *file_priv,
			    struct drm_device *drm,
			    struct drm_mode_create_dumb *args);

/* map memory region for DRM framebuffer to user space */
int drm_gem_cma_dumb_map_offset(struct drm_file *file_priv,
				struct drm_device *drm, u32 handle,
				u64 *offset);

/* set vm_flags and we can change the VM attribute to other one at here */
int drm_gem_cma_mmap(struct file *filp, struct vm_area_struct *vma);

/* allocate physical memory */
struct drm_gem_cma_object *drm_gem_cma_create(struct drm_device *drm,
					      size_t size);

extern const struct vm_operations_struct drm_gem_cma_vm_ops;

#ifdef CONFIG_DEBUG_FS
void drm_gem_cma_describe(struct drm_gem_cma_object *obj, struct seq_file *m);
#endif

struct sg_table *drm_gem_cma_prime_get_sg_table(struct drm_gem_object *obj);
struct drm_gem_object *
drm_gem_cma_prime_import_sg_table(struct drm_device *dev,
				  struct dma_buf_attachment *attach,
				  struct sg_table *sgt);
int drm_gem_cma_prime_mmap(struct drm_gem_object *obj,
			   struct vm_area_struct *vma);
void *drm_gem_cma_prime_vmap(struct drm_gem_object *obj);
void drm_gem_cma_prime_vunmap(struct drm_gem_object *obj, void *vaddr);

#endif /* __DRM_GEM_CMA_HELPER_H__ */
