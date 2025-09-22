// SPDX-License-Identifier: MIT

#include <linux/fb.h>

#include <drm/drm_crtc_helper.h>
#include <drm/drm_drv.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_gem_shmem_helper.h>

#include <drm/drm_fbdev_shmem.h>

/*
 * struct fb_ops
 */

static int drm_fbdev_shmem_fb_open(struct fb_info *info, int user)
{
	struct drm_fb_helper *fb_helper = info->par;

	/* No need to take a ref for fbcon because it unbinds on unregister */
	if (user && !try_module_get(fb_helper->dev->driver->fops->owner))
		return -ENODEV;

	return 0;
}

static int drm_fbdev_shmem_fb_release(struct fb_info *info, int user)
{
	struct drm_fb_helper *fb_helper = info->par;

	if (user)
		module_put(fb_helper->dev->driver->fops->owner);

	return 0;
}

FB_GEN_DEFAULT_DEFERRED_SYSMEM_OPS(drm_fbdev_shmem,
				   drm_fb_helper_damage_range,
				   drm_fb_helper_damage_area);

static int drm_fbdev_shmem_fb_mmap(struct fb_info *info, struct vm_area_struct *vma)
{
	struct drm_fb_helper *fb_helper = info->par;
	struct drm_framebuffer *fb = fb_helper->fb;
	struct drm_gem_object *obj = drm_gem_fb_get_obj(fb, 0);
	struct drm_gem_shmem_object *shmem = to_drm_gem_shmem_obj(obj);

	if (shmem->map_wc)
		vma->vm_page_prot = pgprot_writecombine(vma->vm_page_prot);

	return fb_deferred_io_mmap(info, vma);
}

static void drm_fbdev_shmem_fb_destroy(struct fb_info *info)
{
	struct drm_fb_helper *fb_helper = info->par;

	if (!fb_helper->dev)
		return;

	fb_deferred_io_cleanup(info);
	drm_fb_helper_fini(fb_helper);

	drm_client_buffer_vunmap(fb_helper->buffer);
	drm_client_framebuffer_delete(fb_helper->buffer);
	drm_client_release(&fb_helper->client);
	drm_fb_helper_unprepare(fb_helper);
	kfree(fb_helper);
}

static const struct fb_ops drm_fbdev_shmem_fb_ops = {
	.owner = THIS_MODULE,
	.fb_open = drm_fbdev_shmem_fb_open,
	.fb_release = drm_fbdev_shmem_fb_release,
	__FB_DEFAULT_DEFERRED_OPS_RDWR(drm_fbdev_shmem),
	DRM_FB_HELPER_DEFAULT_OPS,
	__FB_DEFAULT_DEFERRED_OPS_DRAW(drm_fbdev_shmem),
	.fb_mmap = drm_fbdev_shmem_fb_mmap,
	.fb_destroy = drm_fbdev_shmem_fb_destroy,
};

static struct page *drm_fbdev_shmem_get_page(struct fb_info *info, unsigned long offset)
{
	struct drm_fb_helper *fb_helper = info->par;
	struct drm_framebuffer *fb = fb_helper->fb;
	struct drm_gem_object *obj = drm_gem_fb_get_obj(fb, 0);
	struct drm_gem_shmem_object *shmem = to_drm_gem_shmem_obj(obj);
	unsigned int i = offset >> PAGE_SHIFT;
	struct page *page;

	if (fb_WARN_ON_ONCE(info, offset > obj->size))
		return NULL;

	page = shmem->pages[i]; // protected by active vmap
	if (page)
		get_page(page);
	fb_WARN_ON_ONCE(info, !page);

	return page;
}

/*
 * struct drm_fb_helper
 */

static int drm_fbdev_shmem_helper_fb_probe(struct drm_fb_helper *fb_helper,
					   struct drm_fb_helper_surface_size *sizes)
{
	struct drm_client_dev *client = &fb_helper->client;
	struct drm_device *dev = fb_helper->dev;
	struct drm_client_buffer *buffer;
	struct drm_gem_shmem_object *shmem;
	struct drm_framebuffer *fb;
	struct fb_info *info;
	u32 format;
	struct iosys_map map;
	int ret;

	drm_dbg_kms(dev, "surface width(%d), height(%d) and bpp(%d)\n",
		    sizes->surface_width, sizes->surface_height,
		    sizes->surface_bpp);

	format = drm_driver_legacy_fb_format(dev, sizes->surface_bpp, sizes->surface_depth);
	buffer = drm_client_framebuffer_create(client, sizes->surface_width,
					       sizes->surface_height, format);
	if (IS_ERR(buffer))
		return PTR_ERR(buffer);
	shmem = to_drm_gem_shmem_obj(buffer->gem);

	fb = buffer->fb;

	ret = drm_client_buffer_vmap(buffer, &map);
	if (ret) {
		goto err_drm_client_buffer_delete;
	} else if (drm_WARN_ON(dev, map.is_iomem)) {
		ret = -ENODEV; /* I/O memory not supported; use generic emulation */
		goto err_drm_client_buffer_delete;
	}

	fb_helper->buffer = buffer;
	fb_helper->fb = fb;

	info = drm_fb_helper_alloc_info(fb_helper);
	if (IS_ERR(info)) {
		ret = PTR_ERR(info);
		goto err_drm_client_buffer_vunmap;
	}

	drm_fb_helper_fill_info(info, fb_helper, sizes);

	info->fbops = &drm_fbdev_shmem_fb_ops;

	/* screen */
	info->flags |= FBINFO_VIRTFB; /* system memory */
	if (!shmem->map_wc)
		info->flags |= FBINFO_READS_FAST; /* signal caching */
	info->screen_size = sizes->surface_height * fb->pitches[0];
	info->screen_buffer = map.vaddr;
	info->fix.smem_len = info->screen_size;

	/* deferred I/O */
	fb_helper->fbdefio.delay = HZ / 20;
	fb_helper->fbdefio.get_page = drm_fbdev_shmem_get_page;
	fb_helper->fbdefio.deferred_io = drm_fb_helper_deferred_io;

	info->fbdefio = &fb_helper->fbdefio;
	ret = fb_deferred_io_init(info);
	if (ret)
		goto err_drm_fb_helper_release_info;

	return 0;

err_drm_fb_helper_release_info:
	drm_fb_helper_release_info(fb_helper);
err_drm_client_buffer_vunmap:
	fb_helper->fb = NULL;
	fb_helper->buffer = NULL;
	drm_client_buffer_vunmap(buffer);
err_drm_client_buffer_delete:
	drm_client_framebuffer_delete(buffer);
	return ret;
}

static int drm_fbdev_shmem_helper_fb_dirty(struct drm_fb_helper *helper,
					   struct drm_clip_rect *clip)
{
	struct drm_device *dev = helper->dev;
	int ret;

	/* Call damage handlers only if necessary */
	if (!(clip->x1 < clip->x2 && clip->y1 < clip->y2))
		return 0;

	if (helper->fb->funcs->dirty) {
		ret = helper->fb->funcs->dirty(helper->fb, NULL, 0, 0, clip, 1);
		if (drm_WARN_ONCE(dev, ret, "Dirty helper failed: ret=%d\n", ret))
			return ret;
	}

	return 0;
}

static const struct drm_fb_helper_funcs drm_fbdev_shmem_helper_funcs = {
	.fb_probe = drm_fbdev_shmem_helper_fb_probe,
	.fb_dirty = drm_fbdev_shmem_helper_fb_dirty,
};

/*
 * struct drm_client_funcs
 */

static void drm_fbdev_shmem_client_unregister(struct drm_client_dev *client)
{
	struct drm_fb_helper *fb_helper = drm_fb_helper_from_client(client);

	if (fb_helper->info) {
		drm_fb_helper_unregister_info(fb_helper);
	} else {
		drm_client_release(&fb_helper->client);
		drm_fb_helper_unprepare(fb_helper);
		kfree(fb_helper);
	}
}

static int drm_fbdev_shmem_client_restore(struct drm_client_dev *client)
{
	drm_fb_helper_lastclose(client->dev);

	return 0;
}

static int drm_fbdev_shmem_client_hotplug(struct drm_client_dev *client)
{
	struct drm_fb_helper *fb_helper = drm_fb_helper_from_client(client);
	struct drm_device *dev = client->dev;
	int ret;

	if (dev->fb_helper)
		return drm_fb_helper_hotplug_event(dev->fb_helper);

	ret = drm_fb_helper_init(dev, fb_helper);
	if (ret)
		goto err_drm_err;

	if (!drm_drv_uses_atomic_modeset(dev))
		drm_helper_disable_unused_functions(dev);

	ret = drm_fb_helper_initial_config(fb_helper);
	if (ret)
		goto err_drm_fb_helper_fini;

	return 0;

err_drm_fb_helper_fini:
	drm_fb_helper_fini(fb_helper);
err_drm_err:
	drm_err(dev, "fbdev-shmem: Failed to setup emulation (ret=%d)\n", ret);
	return ret;
}

static const struct drm_client_funcs drm_fbdev_shmem_client_funcs = {
	.owner		= THIS_MODULE,
	.unregister	= drm_fbdev_shmem_client_unregister,
	.restore	= drm_fbdev_shmem_client_restore,
	.hotplug	= drm_fbdev_shmem_client_hotplug,
};

/**
 * drm_fbdev_shmem_setup() - Setup fbdev emulation for GEM SHMEM helpers
 * @dev: DRM device
 * @preferred_bpp: Preferred bits per pixel for the device.
 *                 32 is used if this is zero.
 *
 * This function sets up fbdev emulation for GEM DMA drivers that support
 * dumb buffers with a virtual address and that can be mmap'ed.
 * drm_fbdev_shmem_setup() shall be called after the DRM driver registered
 * the new DRM device with drm_dev_register().
 *
 * Restore, hotplug events and teardown are all taken care of. Drivers that do
 * suspend/resume need to call drm_fb_helper_set_suspend_unlocked() themselves.
 * Simple drivers might use drm_mode_config_helper_suspend().
 *
 * This function is safe to call even when there are no connectors present.
 * Setup will be retried on the next hotplug event.
 *
 * The fbdev is destroyed by drm_dev_unregister().
 */
void drm_fbdev_shmem_setup(struct drm_device *dev, unsigned int preferred_bpp)
{
	struct drm_fb_helper *fb_helper;
	int ret;

	drm_WARN(dev, !dev->registered, "Device has not been registered.\n");
	drm_WARN(dev, dev->fb_helper, "fb_helper is already set!\n");

	fb_helper = kzalloc(sizeof(*fb_helper), GFP_KERNEL);
	if (!fb_helper)
		return;
	drm_fb_helper_prepare(dev, fb_helper, preferred_bpp, &drm_fbdev_shmem_helper_funcs);

	ret = drm_client_init(dev, &fb_helper->client, "fbdev", &drm_fbdev_shmem_client_funcs);
	if (ret) {
		drm_err(dev, "Failed to register client: %d\n", ret);
		goto err_drm_client_init;
	}

	drm_client_register(&fb_helper->client);

	return;

err_drm_client_init:
	drm_fb_helper_unprepare(fb_helper);
	kfree(fb_helper);
}
EXPORT_SYMBOL(drm_fbdev_shmem_setup);
