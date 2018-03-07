#ifndef __DRM_GEM_FB_HELPER_H__
#define __DRM_GEM_FB_HELPER_H__

struct drm_device;
struct drm_fb_helper_surface_size;
struct drm_file;
struct drm_framebuffer;
struct drm_framebuffer_funcs;
struct drm_gem_object;
struct drm_mode_fb_cmd2;
struct drm_plane;
struct drm_plane_state;

struct drm_gem_object *drm_gem_fb_get_obj(struct drm_framebuffer *fb,
					  unsigned int plane);
void drm_gem_fb_destroy(struct drm_framebuffer *fb);
int drm_gem_fb_create_handle(struct drm_framebuffer *fb, struct drm_file *file,
			     unsigned int *handle);

struct drm_framebuffer *
drm_gem_fb_create_with_funcs(struct drm_device *dev, struct drm_file *file,
			     const struct drm_mode_fb_cmd2 *mode_cmd,
			     const struct drm_framebuffer_funcs *funcs);
struct drm_framebuffer *
drm_gem_fb_create(struct drm_device *dev, struct drm_file *file,
		  const struct drm_mode_fb_cmd2 *mode_cmd);

int drm_gem_fb_prepare_fb(struct drm_plane *plane,
			  struct drm_plane_state *state);

struct drm_framebuffer *
drm_gem_fbdev_fb_create(struct drm_device *dev,
			struct drm_fb_helper_surface_size *sizes,
			unsigned int pitch_align, struct drm_gem_object *obj,
			const struct drm_framebuffer_funcs *funcs);

#endif
