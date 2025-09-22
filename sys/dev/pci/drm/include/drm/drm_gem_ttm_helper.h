/* Public domain. */

#ifndef _DRM_GEM_TTM_HELPER_H_
#define _DRM_GEM_TTM_HELPER_H_

#include <drm/drm_gem.h>

struct iosys_map;

int drm_gem_ttm_mmap(struct drm_gem_object *, vm_prot_t, voff_t, vsize_t);
int drm_gem_ttm_vmap(struct drm_gem_object *, struct iosys_map *);
void drm_gem_ttm_vunmap(struct drm_gem_object *, struct iosys_map *);

#endif
