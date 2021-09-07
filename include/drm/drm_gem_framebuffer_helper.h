#ifndef __DRM_GEM_FB_HELPER_H__
#define __DRM_GEM_FB_HELPER_H__

#include <linux/dma-buf.h>
#include <linux/dma-buf-map.h>

#include <drm/drm_fourcc.h>

struct drm_afbc_framebuffer;
struct drm_device;
struct drm_fb_helper_surface_size;
struct drm_file;
struct drm_framebuffer;
struct drm_framebuffer_funcs;
struct drm_gem_object;
struct drm_mode_fb_cmd2;

#define AFBC_VENDOR_AND_TYPE_MASK	GENMASK_ULL(63, 52)

struct drm_gem_object *drm_gem_fb_get_obj(struct drm_framebuffer *fb,
					  unsigned int plane);
void drm_gem_fb_destroy(struct drm_framebuffer *fb);
int drm_gem_fb_create_handle(struct drm_framebuffer *fb, struct drm_file *file,
			     unsigned int *handle);

int drm_gem_fb_init_with_funcs(struct drm_device *dev,
			       struct drm_framebuffer *fb,
			       struct drm_file *file,
			       const struct drm_mode_fb_cmd2 *mode_cmd,
			       const struct drm_framebuffer_funcs *funcs);
struct drm_framebuffer *
drm_gem_fb_create_with_funcs(struct drm_device *dev, struct drm_file *file,
			     const struct drm_mode_fb_cmd2 *mode_cmd,
			     const struct drm_framebuffer_funcs *funcs);
struct drm_framebuffer *
drm_gem_fb_create(struct drm_device *dev, struct drm_file *file,
		  const struct drm_mode_fb_cmd2 *mode_cmd);
struct drm_framebuffer *
drm_gem_fb_create_with_dirty(struct drm_device *dev, struct drm_file *file,
			     const struct drm_mode_fb_cmd2 *mode_cmd);

int drm_gem_fb_vmap(struct drm_framebuffer *fb,
		    struct dma_buf_map map[static DRM_FORMAT_MAX_PLANES],
		    struct dma_buf_map data[DRM_FORMAT_MAX_PLANES]);
void drm_gem_fb_vunmap(struct drm_framebuffer *fb,
		       struct dma_buf_map map[static DRM_FORMAT_MAX_PLANES]);
int drm_gem_fb_begin_cpu_access(struct drm_framebuffer *fb, enum dma_data_direction dir);
void drm_gem_fb_end_cpu_access(struct drm_framebuffer *fb, enum dma_data_direction dir);

#define drm_is_afbc(modifier) \
	(((modifier) & AFBC_VENDOR_AND_TYPE_MASK) == DRM_FORMAT_MOD_ARM_AFBC(0))

int drm_gem_fb_afbc_init(struct drm_device *dev,
			 const struct drm_mode_fb_cmd2 *mode_cmd,
			 struct drm_afbc_framebuffer *afbc_fb);

#endif
