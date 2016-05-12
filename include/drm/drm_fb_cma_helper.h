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

struct drm_fbdev_cma *drm_fbdev_cma_init_with_funcs(struct drm_device *dev,
	unsigned int preferred_bpp, unsigned int num_crtc,
	unsigned int max_conn_count, const struct drm_fb_helper_funcs *funcs);
struct drm_fbdev_cma *drm_fbdev_cma_init(struct drm_device *dev,
	unsigned int preferred_bpp, unsigned int num_crtc,
	unsigned int max_conn_count);
void drm_fbdev_cma_fini(struct drm_fbdev_cma *fbdev_cma);

void drm_fbdev_cma_restore_mode(struct drm_fbdev_cma *fbdev_cma);
void drm_fbdev_cma_hotplug_event(struct drm_fbdev_cma *fbdev_cma);
int drm_fbdev_cma_create_with_funcs(struct drm_fb_helper *helper,
	struct drm_fb_helper_surface_size *sizes,
	const struct drm_framebuffer_funcs *funcs);

void drm_fb_cma_destroy(struct drm_framebuffer *fb);
int drm_fb_cma_create_handle(struct drm_framebuffer *fb,
	struct drm_file *file_priv, unsigned int *handle);

struct drm_framebuffer *drm_fb_cma_create(struct drm_device *dev,
	struct drm_file *file_priv, const struct drm_mode_fb_cmd2 *mode_cmd);

struct drm_gem_cma_object *drm_fb_cma_get_gem_obj(struct drm_framebuffer *fb,
	unsigned int plane);

#ifdef CONFIG_DEBUG_FS
struct seq_file;

int drm_fb_cma_debugfs_show(struct seq_file *m, void *arg);
#endif

#endif

