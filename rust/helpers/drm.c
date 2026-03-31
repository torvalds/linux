// SPDX-License-Identifier: GPL-2.0

#include <drm/drm_gem.h>
#include <drm/drm_gem_shmem_helper.h>
#include <drm/drm_vma_manager.h>

#ifdef CONFIG_DRM

__rust_helper void rust_helper_drm_gem_object_get(struct drm_gem_object *obj)
{
	drm_gem_object_get(obj);
}

__rust_helper void rust_helper_drm_gem_object_put(struct drm_gem_object *obj)
{
	drm_gem_object_put(obj);
}

__rust_helper __u64
rust_helper_drm_vma_node_offset_addr(struct drm_vma_offset_node *node)
{
	return drm_vma_node_offset_addr(node);
}

#ifdef CONFIG_DRM_GEM_SHMEM_HELPER
__rust_helper void
rust_helper_drm_gem_shmem_object_free(struct drm_gem_object *obj)
{
	return drm_gem_shmem_object_free(obj);
}

__rust_helper void
rust_helper_drm_gem_shmem_object_print_info(struct drm_printer *p, unsigned int indent,
					    const struct drm_gem_object *obj)
{
	drm_gem_shmem_object_print_info(p, indent, obj);
}

__rust_helper int
rust_helper_drm_gem_shmem_object_pin(struct drm_gem_object *obj)
{
	return drm_gem_shmem_object_pin(obj);
}

__rust_helper void
rust_helper_drm_gem_shmem_object_unpin(struct drm_gem_object *obj)
{
	drm_gem_shmem_object_unpin(obj);
}

__rust_helper struct sg_table *
rust_helper_drm_gem_shmem_object_get_sg_table(struct drm_gem_object *obj)
{
	return drm_gem_shmem_object_get_sg_table(obj);
}

__rust_helper int
rust_helper_drm_gem_shmem_object_vmap(struct drm_gem_object *obj,
				      struct iosys_map *map)
{
	return drm_gem_shmem_object_vmap(obj, map);
}

__rust_helper void
rust_helper_drm_gem_shmem_object_vunmap(struct drm_gem_object *obj,
					struct iosys_map *map)
{
	drm_gem_shmem_object_vunmap(obj, map);
}

__rust_helper int
rust_helper_drm_gem_shmem_object_mmap(struct drm_gem_object *obj, struct vm_area_struct *vma)
{
	return drm_gem_shmem_object_mmap(obj, vma);
}

#endif /* CONFIG_DRM_GEM_SHMEM_HELPER */
#endif /* CONFIG_DRM */
