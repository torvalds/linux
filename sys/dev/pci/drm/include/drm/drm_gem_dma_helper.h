/* Public Domain */

#ifndef _DRM_DRM_GEM_DMA_HELPER_H
#define _DRM_DRM_GEM_DMA_HELPER_H

#include <drm/drm_gem.h>

void drm_gem_dma_free_object(struct drm_gem_object *);
int drm_gem_dma_dumb_create(struct drm_file *, struct drm_device *,
    struct drm_mode_create_dumb *);
int drm_gem_dma_dumb_create_internal(struct drm_file *, struct drm_device *,
    struct drm_mode_create_dumb *);
int drm_gem_dma_dumb_map_offset(struct drm_file *, struct drm_device *,
    uint32_t, uint64_t *);
struct drm_gem_dma_object *drm_gem_dma_create(struct drm_device *,
    size_t);

int drm_gem_dma_fault(struct drm_gem_object *, struct uvm_faultinfo *,
    off_t, vaddr_t, vm_page_t *, int, int, vm_prot_t, int);

struct sg_table *drm_gem_dma_get_sg_table(struct drm_gem_object *);

int drm_gem_dma_vmap(struct drm_gem_object *, struct iosys_map *);

struct drm_gem_dma_object {
	struct drm_gem_object	base;
	bus_dma_tag_t		dmat;
	bus_dmamap_t		dmamap;
	bus_dma_segment_t	dmasegs[1];
	size_t			dmasize;
	dma_addr_t		dma_addr;
	caddr_t			vaddr;
	struct sg_table		*sgt;
	bool			map_noncoherent;
};

#define to_drm_gem_dma_obj(gem_obj) container_of(gem_obj, struct drm_gem_dma_object, base)

#define DEFINE_DRM_GEM_DMA_FOPS(name) struct file_operations name = {};

#define DRM_GEM_DMA_DRIVER_OPS_WITH_DUMB_CREATE(x)	\
	.dumb_create = (x),				\
	.gem_fault = drm_gem_dma_fault

#endif
