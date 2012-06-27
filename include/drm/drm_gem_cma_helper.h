#ifndef __DRM_GEM_CMA_HELPER_H__
#define __DRM_GEM_CMA_HELPER_H__

struct drm_gem_cma_object {
	struct drm_gem_object base;
	dma_addr_t paddr;
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

/*
 * destroy memory region allocated.
 *	- a gem handle and physical memory region pointed by a gem object
 *	would be released by drm_gem_handle_delete().
 */
int drm_gem_cma_dumb_destroy(struct drm_file *file_priv,
		struct drm_device *drm, unsigned int handle);

/* allocate physical memory. */
struct drm_gem_cma_object *drm_gem_cma_create(struct drm_device *drm,
		unsigned int size);

extern const struct vm_operations_struct drm_gem_cma_vm_ops;

#endif /* __DRM_GEM_CMA_HELPER_H__ */
