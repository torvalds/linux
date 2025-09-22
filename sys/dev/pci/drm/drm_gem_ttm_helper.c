/* Public domain. */

#include <sys/types.h>
#include <sys/param.h>
#include <uvm/uvm_extern.h>

#include <linux/kernel.h>
#include <linux/iosys-map.h>
#include <drm/drm_gem.h>
#include <drm/ttm/ttm_bo.h>

int
drm_gem_ttm_mmap(struct drm_gem_object *obj,
    vm_prot_t accessprot, voff_t off, vsize_t size)
{
	struct ttm_buffer_object *tbo =
	    container_of(obj, struct ttm_buffer_object, base);
	int r = ttm_bo_mmap_obj(tbo);
	if (r >= 0)
		drm_gem_object_put(obj);
	return r;
}

int
drm_gem_ttm_vmap(struct drm_gem_object *obj, struct iosys_map *ism)
{
	struct ttm_buffer_object *tbo =
	    container_of(obj, struct ttm_buffer_object, base);

	return ttm_bo_vmap(tbo, ism);
}

void
drm_gem_ttm_vunmap(struct drm_gem_object *obj, struct iosys_map *ism)
{
	struct ttm_buffer_object *tbo =
	    container_of(obj, struct ttm_buffer_object, base);

	ttm_bo_vunmap(tbo, ism);
}
