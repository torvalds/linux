/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __DRM_FB_CMA_HELPER_H__
#define __DRM_FB_CMA_HELPER_H__

struct drm_fbdev_cma;
struct drm_gem_cma_object;

struct drm_fb_helper_surface_size;
struct drm_framebuffer_funcs;
struct drm_fb_helper_funcs;
struct drm_framebuffer;
struct drm_fb_helper;
struct drm_device;
struct drm_file;
struct drm_mode_fb_cmd2;
struct drm_plane;
struct drm_plane_state;

int drm_fb_cma_fbdev_init(struct drm_device *dev, unsigned int preferred_bpp,
			  unsigned int max_conn_count);
void drm_fb_cma_fbdev_fini(struct drm_device *dev);

struct drm_fbdev_cma *drm_fbdev_cma_init(struct drm_device *dev,
	unsigned int preferred_bpp, unsigned int max_conn_count);
void drm_fbdev_cma_fini(struct drm_fbdev_cma *fbdev_cma);

void drm_fbdev_cma_restore_mode(struct drm_fbdev_cma *fbdev_cma);
void drm_fbdev_cma_hotplug_event(struct drm_fbdev_cma *fbdev_cma);
void drm_fbdev_cma_set_suspend_unlocked(struct drm_fbdev_cma *fbdev_cma,
					bool state);

struct drm_gem_cma_object *drm_fb_cma_get_gem_obj(struct drm_framebuffer *fb,
	unsigned int plane);

dma_addr_t drm_fb_cma_get_gem_addr(struct drm_framebuffer *fb,
				   struct drm_plane_state *state,
				   unsigned int plane);

#endif

