#ifndef __DRM_GEM_CMA_HELPER_H__
#define __DRM_GEM_CMA_HELPER_H__

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

/* free gem object. */
void drm_gem_cma_free_object(struct drm_gem_object *gem_obj);

/* create memory region for drm framebuffer. */
int drm_gem_cma_dumb_create(struct drm_file *file_priv,
		struct drm_device *drm, struct drm_mode_create_dumb *args);

/* map memory region for drm framebuffer to user space. */
int drm_gem_cma_dumb_map_offset(struct drm_file *file_priv,
		struct drm_device *drm, uint32_t handle, uint64_t *offset);

/* set vm_flags and we can change the vm attribute to other one at here. */
int drm_gem_cma_mmap(struct file *filp, struct vm_area_struct *vma);

/* allocate physical memory. */
struct drm_gem_cma_object *drm_gem_cma_create(struct drm_device *drm,
		unsigned int size);

extern const struct vm_operations_struct drm_gem_cma_vm_ops;

#ifdef CONFIG_DEBUG_FS
void drm_gem_cma_describe(struct drm_gem_cma_object *obj, struct seq_file *m);
#endif

struct dma_buf *drm_gem_cma_dmabuf_export(struct drm_device *drm_dev,
					  struct drm_gem_object *obj,
					  int flags);
struct drm_gem_object *drm_gem_cma_dmabuf_import(struct drm_device *drm_dev,
						 struct dma_buf *dma_buf);

struct sg_table *drm_gem_cma_prime_get_sg_table(struct drm_gem_object *obj);
struct drm_gem_object *
drm_gem_cma_prime_import_sg_table(struct drm_device *dev, size_t size,
				  struct sg_table *sgt);
int drm_gem_cma_prime_mmap(struct drm_gem_object *obj,
			   struct vm_area_struct *vma);
void *drm_gem_cma_prime_vmap(struct drm_gem_object *obj);
void drm_gem_cma_prime_vunmap(struct drm_gem_object *obj, void *vaddr);

#endif /* __DRM_GEM_CMA_HELPER_H__ */
