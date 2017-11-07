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

#define to_drm_gem_cma_obj(gem_obj) \
	container_of(gem_obj, struct drm_gem_cma_object, base)

#ifndef CONFIG_MMU
#define DRM_GEM_CMA_UNMAPPED_AREA_FOPS \
	.get_unmapped_area	= drm_gem_cma_get_unmapped_area,
#else
#define DRM_GEM_CMA_UNMAPPED_AREA_FOPS
#endif

/**
 * DEFINE_DRM_GEM_CMA_FOPS() - macro to generate file operations for CMA drivers
 * @name: name for the generated structure
 *
 * This macro autogenerates a suitable &struct file_operations for CMA based
 * drivers, which can be assigned to &drm_driver.fops. Note that this structure
 * cannot be shared between drivers, because it contains a reference to the
 * current module using THIS_MODULE.
 *
 * Note that the declaration is already marked as static - if you need a
 * non-static version of this you're probably doing it wrong and will break the
 * THIS_MODULE reference by accident.
 */
#define DEFINE_DRM_GEM_CMA_FOPS(name) \
	static const struct file_operations name = {\
		.owner		= THIS_MODULE,\
		.open		= drm_open,\
		.release	= drm_release,\
		.unlocked_ioctl	= drm_ioctl,\
		.compat_ioctl	= drm_compat_ioctl,\
		.poll		= drm_poll,\
		.read		= drm_read,\
		.llseek		= noop_llseek,\
		.mmap		= drm_gem_cma_mmap,\
		DRM_GEM_CMA_UNMAPPED_AREA_FOPS \
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

/* set vm_flags and we can change the VM attribute to other one at here */
int drm_gem_cma_mmap(struct file *filp, struct vm_area_struct *vma);

/* allocate physical memory */
struct drm_gem_cma_object *drm_gem_cma_create(struct drm_device *drm,
					      size_t size);

extern const struct vm_operations_struct drm_gem_cma_vm_ops;

#ifndef CONFIG_MMU
unsigned long drm_gem_cma_get_unmapped_area(struct file *filp,
					    unsigned long addr,
					    unsigned long len,
					    unsigned long pgoff,
					    unsigned long flags);
#endif

#ifdef CONFIG_DEBUG_FS
void drm_gem_cma_describe(struct drm_gem_cma_object *obj, struct seq_file *m);
#endif

void drm_gem_cma_print_info(struct drm_printer *p, unsigned int indent,
			    const struct drm_gem_object *obj);

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
