/* Public Domain */

#include <drm/drm_framebuffer.h>
#include <drm/drm_fb_dma_helper.h>
#include <drm/drm_gem_dma_helper.h>

struct drm_gem_dma_object *
drm_fb_dma_get_gem_obj(struct drm_framebuffer *fb, unsigned int plane)
{
	struct drm_gem_object *obj;

	KASSERT(plane == 0);
	obj = fb->obj[plane];
	if (obj)
		return to_drm_gem_dma_obj(obj);
	return NULL;
}
