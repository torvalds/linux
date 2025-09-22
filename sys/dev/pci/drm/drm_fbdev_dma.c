// SPDX-License-Identifier: MIT

#include <linux/fb.h>
#include <linux/vmalloc.h>

#include <drm/drm_crtc_helper.h>
#include <drm/drm_drv.h>
#include <drm/drm_fb_dma_helper.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_gem_dma_helper.h>

#include <drm/drm_fbdev_dma.h>

/*
 * struct fb_ops
 */

#ifdef __linux__

static int drm_fbdev_dma_fb_open(struct fb_info *info, int user)
{
	struct drm_fb_helper *fb_helper = info->par;

	/* No need to take a ref for fbcon because it unbinds on unregister */
	if (user && !try_module_get(fb_helper->dev->driver->fops->owner))
		return -ENODEV;

	return 0;
}

static int drm_fbdev_dma_fb_release(struct fb_info *info, int user)
{
	struct drm_fb_helper *fb_helper = info->par;

	if (user)
		module_put(fb_helper->dev->driver->fops->owner);

	return 0;
}

static int drm_fbdev_dma_fb_mmap(struct fb_info *info, struct vm_area_struct *vma)
{
	struct drm_fb_helper *fb_helper = info->par;

	return drm_gem_prime_mmap(fb_helper->buffer->gem, vma);
}

static void drm_fbdev_dma_fb_destroy(struct fb_info *info)
{
	struct drm_fb_helper *fb_helper = info->par;

	if (!fb_helper->dev)
		return;

	if (info->fbdefio)
		fb_deferred_io_cleanup(info);
	drm_fb_helper_fini(fb_helper);

	drm_client_buffer_vunmap(fb_helper->buffer);
	drm_client_framebuffer_delete(fb_helper->buffer);
	drm_client_release(&fb_helper->client);
	drm_fb_helper_unprepare(fb_helper);
	kfree(fb_helper);
}

#endif /* __linux__ */

static const struct fb_ops drm_fbdev_dma_fb_ops = {
#ifdef notyet
	.owner = THIS_MODULE,
	.fb_open = drm_fbdev_dma_fb_open,
	.fb_release = drm_fbdev_dma_fb_release,
	__FB_DEFAULT_DMAMEM_OPS_RDWR,
	DRM_FB_HELPER_DEFAULT_OPS,
	__FB_DEFAULT_DMAMEM_OPS_DRAW,
	.fb_mmap = drm_fbdev_dma_fb_mmap,
	.fb_destroy = drm_fbdev_dma_fb_destroy,
#else
	DRM_FB_HELPER_DEFAULT_OPS,
#endif
};

#ifdef __linux__
FB_GEN_DEFAULT_DEFERRED_DMAMEM_OPS(drm_fbdev_dma_shadowed,
				   drm_fb_helper_damage_range,
				   drm_fb_helper_damage_area);

static void drm_fbdev_dma_shadowed_fb_destroy(struct fb_info *info)
{
	struct drm_fb_helper *fb_helper = info->par;
	void *shadow = info->screen_buffer;

	if (!fb_helper->dev)
		return;

	if (info->fbdefio)
		fb_deferred_io_cleanup(info);
	drm_fb_helper_fini(fb_helper);
	vfree(shadow);

	drm_client_buffer_vunmap(fb_helper->buffer);
	drm_client_framebuffer_delete(fb_helper->buffer);
	drm_client_release(&fb_helper->client);
	drm_fb_helper_unprepare(fb_helper);
	kfree(fb_helper);
}
#endif /* __linux__ */

static const struct fb_ops drm_fbdev_dma_shadowed_fb_ops = {
#ifdef notyet
	.owner = THIS_MODULE,
	.fb_open = drm_fbdev_dma_fb_open,
	.fb_release = drm_fbdev_dma_fb_release,
	FB_DEFAULT_DEFERRED_OPS(drm_fbdev_dma_shadowed),
	DRM_FB_HELPER_DEFAULT_OPS,
	.fb_destroy = drm_fbdev_dma_shadowed_fb_destroy,
#else
	DRM_FB_HELPER_DEFAULT_OPS,
#endif
};

/*
 * struct drm_fb_helper
 */

static void drm_fbdev_dma_damage_blit_real(struct drm_fb_helper *fb_helper,
					   struct drm_clip_rect *clip,
					   struct iosys_map *dst)
{
	struct drm_framebuffer *fb = fb_helper->fb;
	size_t offset = clip->y1 * fb->pitches[0];
	size_t len = clip->x2 - clip->x1;
	unsigned int y;
	void *src;

	switch (drm_format_info_bpp(fb->format, 0)) {
	case 1:
		offset += clip->x1 / 8;
		len = DIV_ROUND_UP(len + clip->x1 % 8, 8);
		break;
	case 2:
		offset += clip->x1 / 4;
		len = DIV_ROUND_UP(len + clip->x1 % 4, 4);
		break;
	case 4:
		offset += clip->x1 / 2;
		len = DIV_ROUND_UP(len + clip->x1 % 2, 2);
		break;
	default:
		offset += clip->x1 * fb->format->cpp[0];
		len *= fb->format->cpp[0];
		break;
	}

	src = fb_helper->info->screen_buffer + offset;
	iosys_map_incr(dst, offset); /* go to first pixel within clip rect */

	for (y = clip->y1; y < clip->y2; y++) {
		iosys_map_memcpy_to(dst, 0, src, len);
		iosys_map_incr(dst, fb->pitches[0]);
		src += fb->pitches[0];
	}
}

static int drm_fbdev_dma_damage_blit(struct drm_fb_helper *fb_helper,
				     struct drm_clip_rect *clip)
{
	struct drm_client_buffer *buffer = fb_helper->buffer;
	struct iosys_map dst;

	/*
	 * For fbdev emulation, we only have to protect against fbdev modeset
	 * operations. Nothing else will involve the client buffer's BO. So it
	 * is sufficient to acquire struct drm_fb_helper.lock here.
	 */
	mutex_lock(&fb_helper->lock);

	dst = buffer->map;
	drm_fbdev_dma_damage_blit_real(fb_helper, clip, &dst);

	mutex_unlock(&fb_helper->lock);

	return 0;
}

static int drm_fbdev_dma_helper_fb_probe(struct drm_fb_helper *fb_helper,
					 struct drm_fb_helper_surface_size *sizes)
{
	return drm_fbdev_dma_driver_fbdev_probe(fb_helper, sizes);
}
static int drm_fbdev_dma_helper_fb_dirty(struct drm_fb_helper *helper,
					 struct drm_clip_rect *clip)
{
	struct drm_device *dev = helper->dev;
	int ret;

	/* Call damage handlers only if necessary */
	if (!(clip->x1 < clip->x2 && clip->y1 < clip->y2))
		return 0;

	if (helper->fb->funcs->dirty) {
		ret = drm_fbdev_dma_damage_blit(helper, clip);
		if (drm_WARN_ONCE(dev, ret, "Damage blitter failed: ret=%d\n", ret))
			return ret;

		ret = helper->fb->funcs->dirty(helper->fb, NULL, 0, 0, clip, 1);
		if (drm_WARN_ONCE(dev, ret, "Dirty helper failed: ret=%d\n", ret))
			return ret;
	}

	return 0;
}

static const struct drm_fb_helper_funcs drm_fbdev_dma_helper_funcs = {
	.fb_probe = drm_fbdev_dma_helper_fb_probe,
	.fb_dirty = drm_fbdev_dma_helper_fb_dirty,
};

/*
 * struct drm_fb_helper
 */

static int drm_fbdev_dma_driver_fbdev_probe_tail(struct drm_fb_helper *fb_helper,
						 struct drm_fb_helper_surface_size *sizes)
{
	struct drm_device *dev = fb_helper->dev;
	struct drm_client_buffer *buffer = fb_helper->buffer;
	struct drm_gem_dma_object *dma_obj = to_drm_gem_dma_obj(buffer->gem);
	struct drm_framebuffer *fb = fb_helper->fb;
	struct fb_info *info = fb_helper->info;
	struct iosys_map map = buffer->map;

	info->fbops = &drm_fbdev_dma_fb_ops;

	/* screen */
	info->flags |= FBINFO_VIRTFB; /* system memory */
	if (dma_obj->map_noncoherent)
		info->flags |= FBINFO_READS_FAST; /* signal caching */
	info->screen_size = sizes->surface_height * fb->pitches[0];
	info->screen_buffer = map.vaddr;
	if (!(info->flags & FBINFO_HIDE_SMEM_START)) {
		if (!drm_WARN_ON(dev, is_vmalloc_addr(info->screen_buffer)))
			info->fix.smem_start = page_to_phys(virt_to_page(info->screen_buffer));
	}
	info->fix.smem_len = info->screen_size;

	return 0;
}

static int drm_fbdev_dma_driver_fbdev_probe_tail_shadowed(struct drm_fb_helper *fb_helper,
							  struct drm_fb_helper_surface_size *sizes)
{
	struct drm_client_buffer *buffer = fb_helper->buffer;
	struct fb_info *info = fb_helper->info;
	size_t screen_size = buffer->gem->size;
	void *screen_buffer;
#ifdef notyet
	int ret;
#endif

	/*
	 * Deferred I/O requires struct page for framebuffer memory,
	 * which is not guaranteed for all DMA ranges. We thus create
	 * a shadow buffer in system memory.
	 */
	screen_buffer = vzalloc(screen_size);
	if (!screen_buffer)
		return -ENOMEM;

	info->fbops = &drm_fbdev_dma_shadowed_fb_ops;

	/* screen */
	info->flags |= FBINFO_VIRTFB; /* system memory */
	info->flags |= FBINFO_READS_FAST; /* signal caching */
	info->screen_buffer = screen_buffer;
	info->fix.smem_len = screen_size;

#ifdef notyet
	fb_helper->fbdefio.delay = HZ / 20;
	fb_helper->fbdefio.deferred_io = drm_fb_helper_deferred_io;

	info->fbdefio = &fb_helper->fbdefio;
	ret = fb_deferred_io_init(info);
	if (ret)
		goto err_vfree;

	return 0;

err_vfree:
	vfree(screen_buffer);
	return ret;
#else
	return 0;
#endif
}

int drm_fbdev_dma_driver_fbdev_probe(struct drm_fb_helper *fb_helper,
				     struct drm_fb_helper_surface_size *sizes)
{
	struct drm_client_dev *client = &fb_helper->client;
	struct drm_device *dev = fb_helper->dev;
	struct drm_client_buffer *buffer;
	struct drm_framebuffer *fb;
	struct fb_info *info;
	u32 format;
	struct iosys_map map;
	int ret;

	drm_dbg_kms(dev, "surface width(%d), height(%d) and bpp(%d)\n",
		    sizes->surface_width, sizes->surface_height,
		    sizes->surface_bpp);

	format = drm_driver_legacy_fb_format(dev, sizes->surface_bpp,
					     sizes->surface_depth);
	buffer = drm_client_framebuffer_create(client, sizes->surface_width,
					       sizes->surface_height, format);
	if (IS_ERR(buffer))
		return PTR_ERR(buffer);

	fb = buffer->fb;

	ret = drm_client_buffer_vmap(buffer, &map);
	if (ret) {
		goto err_drm_client_buffer_delete;
	} else if (drm_WARN_ON(dev, map.is_iomem)) {
		ret = -ENODEV; /* I/O memory not supported; use generic emulation */
		goto err_drm_client_buffer_delete;
	}

	fb_helper->funcs = &drm_fbdev_dma_helper_funcs;
	fb_helper->buffer = buffer;
	fb_helper->fb = fb;

	info = drm_fb_helper_alloc_info(fb_helper);
	if (IS_ERR(info)) {
		ret = PTR_ERR(info);
		goto err_drm_client_buffer_vunmap;
	}

	drm_fb_helper_fill_info(info, fb_helper, sizes);

	if (fb->funcs->dirty)
		ret = drm_fbdev_dma_driver_fbdev_probe_tail_shadowed(fb_helper, sizes);
	else
		ret = drm_fbdev_dma_driver_fbdev_probe_tail(fb_helper, sizes);
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
EXPORT_SYMBOL(drm_fbdev_dma_driver_fbdev_probe);

/*
 * struct drm_client_funcs
 */

static void drm_fbdev_dma_client_unregister(struct drm_client_dev *client)
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

static int drm_fbdev_dma_client_restore(struct drm_client_dev *client)
{
	drm_fb_helper_lastclose(client->dev);

	return 0;
}

static int drm_fbdev_dma_client_hotplug(struct drm_client_dev *client)
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
	drm_err(dev, "fbdev-dma: Failed to setup generic emulation (ret=%d)\n", ret);
	return ret;
}

static const struct drm_client_funcs drm_fbdev_dma_client_funcs = {
	.owner		= THIS_MODULE,
	.unregister	= drm_fbdev_dma_client_unregister,
	.restore	= drm_fbdev_dma_client_restore,
	.hotplug	= drm_fbdev_dma_client_hotplug,
};

/**
 * drm_fbdev_dma_setup() - Setup fbdev emulation for GEM DMA helpers
 * @dev: DRM device
 * @preferred_bpp: Preferred bits per pixel for the device.
 *                 32 is used if this is zero.
 *
 * This function sets up fbdev emulation for GEM DMA drivers that support
 * dumb buffers with a virtual address and that can be mmap'ed.
 * drm_fbdev_dma_setup() shall be called after the DRM driver registered
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
void drm_fbdev_dma_setup(struct drm_device *dev, unsigned int preferred_bpp)
{
	struct drm_fb_helper *fb_helper;
	int ret;

	drm_WARN(dev, !dev->registered, "Device has not been registered.\n");
	drm_WARN(dev, dev->fb_helper, "fb_helper is already set!\n");

	fb_helper = kzalloc(sizeof(*fb_helper), GFP_KERNEL);
	if (!fb_helper)
		return;
	drm_fb_helper_prepare(dev, fb_helper, preferred_bpp, &drm_fbdev_dma_helper_funcs);

	ret = drm_client_init(dev, &fb_helper->client, "fbdev", &drm_fbdev_dma_client_funcs);
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
EXPORT_SYMBOL(drm_fbdev_dma_setup);
