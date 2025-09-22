/* Public domain. */

#include <drm/drm_gem.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_modeset_helper.h>

void
drm_gem_fb_destroy(struct drm_framebuffer *fb)
{
	int i;

	for (i = 0; i < 4; i++)
		drm_gem_object_put(fb->obj[i]);
	drm_framebuffer_cleanup(fb);
	free(fb, M_DRM, 0);
}

int
drm_gem_fb_create_handle(struct drm_framebuffer *fb, struct drm_file *file,
    unsigned int *handle)
{
	return drm_gem_handle_create(file, fb->obj[0], handle);
}

const struct drm_framebuffer_funcs drm_gem_fb_funcs = {
	.create_handle = drm_gem_fb_create_handle,
	.destroy = drm_gem_fb_destroy,
};

struct drm_framebuffer *
drm_gem_fb_create(struct drm_device *dev, struct drm_file *file,
		  const struct drm_mode_fb_cmd2 *cmd)
{
	struct drm_framebuffer *fb;
	const struct drm_format_info *info;
	struct drm_gem_object *gem_obj;
	int error;

	info = drm_get_format_info(dev, cmd);
	if (!info)
		return ERR_PTR(-EINVAL);

	KASSERT(info->num_planes == 1);

	gem_obj = drm_gem_object_lookup(file, cmd->handles[0]);
	if (gem_obj == NULL)
		return ERR_PTR(-ENOENT);

	fb = malloc(sizeof(*fb), M_DRM, M_ZERO | M_WAITOK);

	drm_helper_mode_fill_fb_struct(dev, fb, cmd);
	fb->obj[0] = gem_obj;

	error = drm_framebuffer_init(dev, fb, &drm_gem_fb_funcs);
	if (error != 0)
		goto dealloc;

	return fb;

dealloc:
	drm_framebuffer_cleanup(fb);
	free(fb, M_DRM, sizeof(*fb));
	drm_gem_object_put(gem_obj);

	return ERR_PTR(error);
}

struct drm_gem_object *
drm_gem_fb_get_obj(struct drm_framebuffer *fb, unsigned int plane)
{
	if (plane < nitems(fb->obj))
		return fb->obj[plane];
	return NULL;
}
