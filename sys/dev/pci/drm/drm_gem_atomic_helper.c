/* Public domain. */

#include <linux/kernel.h>

#include <drm/drm_plane.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_atomic_uapi.h>
#include <drm/drm_gem.h>
#include <linux/dma-resv.h>

int
drm_gem_plane_helper_prepare_fb(struct drm_plane *dp,
    struct drm_plane_state *dps)
{
	struct drm_gem_object *obj;
	struct dma_fence *f;
	int r;

	if (dps->fb != NULL) {
		obj = dps->fb->obj[0];
		if (obj == NULL)
			return -EINVAL;
		r = dma_resv_get_singleton(obj->resv, DMA_RESV_USAGE_WRITE, &f);
		if (r)
			return r;
		if (dps->fence)
			dma_fence_put(f);
		else
			dps->fence = f;
	}

	return 0;
}
