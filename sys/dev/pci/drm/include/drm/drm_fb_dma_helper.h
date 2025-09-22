/* Public Domain */

#ifndef _DRM_DRM_FB_DMA_HELPER_H
#define _DRM_DRM_FB_DMA_HELPER_H

#include <linux/types.h>

struct drm_framebuffer;

struct drm_gem_dma_object *drm_fb_dma_get_gem_obj(struct drm_framebuffer *,
    unsigned int);

#endif
