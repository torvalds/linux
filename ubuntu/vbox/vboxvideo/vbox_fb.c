/*
 * Copyright (C) 2013-2017 Oracle Corporation
 * This file is based on ast_fb.c
 * Copyright 2012 Red Hat Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS, AUTHORS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * Authors: Dave Airlie <airlied@redhat.com>
 *          Michael Thayer <michael.thayer@oracle.com,
 */
/* Include from most specific to most general to be able to override things. */
#include "vbox_drv.h"
#include "vboxvideo.h"

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/tty.h>
#include <linux/sysrq.h>
#include <linux/delay.h>
#include <linux/fb.h>
#include <linux/init.h>

#include <drm/drmP.h>
#include <drm/drm_crtc.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_crtc_helper.h>
#include "vbox_drv.h"

#define VBOX_DIRTY_DELAY (HZ / 30)
/**
 * Tell the host about dirty rectangles to update.
 */
static void vbox_dirty_update(struct vbox_fbdev *fbdev,
			      int x, int y, int width, int height)
{
	struct drm_gem_object *obj;
	struct vbox_bo *bo;
	int ret = -EBUSY;
	bool store_for_later = false;
	int x2, y2;
	unsigned long flags;
	struct drm_clip_rect rect;

	obj = fbdev->afb.obj;
	bo = gem_to_vbox_bo(obj);

	/*
	 * try and reserve the BO, if we fail with busy
	 * then the BO is being moved and we should
	 * store up the damage until later.
	 */
	if (drm_can_sleep())
		ret = vbox_bo_reserve(bo, true);
	if (ret) {
		if (ret != -EBUSY)
			return;

		store_for_later = true;
	}

	x2 = x + width - 1;
	y2 = y + height - 1;
	spin_lock_irqsave(&fbdev->dirty_lock, flags);

	if (fbdev->y1 < y)
		y = fbdev->y1;
	if (fbdev->y2 > y2)
		y2 = fbdev->y2;
	if (fbdev->x1 < x)
		x = fbdev->x1;
	if (fbdev->x2 > x2)
		x2 = fbdev->x2;

	if (store_for_later) {
		fbdev->x1 = x;
		fbdev->x2 = x2;
		fbdev->y1 = y;
		fbdev->y2 = y2;
		spin_unlock_irqrestore(&fbdev->dirty_lock, flags);
		return;
	}

	fbdev->x1 = INT_MAX;
	fbdev->y1 = INT_MAX;
	fbdev->x2 = 0;
	fbdev->y2 = 0;

	spin_unlock_irqrestore(&fbdev->dirty_lock, flags);

	/*
	 * Not sure why the original code subtracted 1 here, but I will keep
	 * it that way to avoid unnecessary differences.
	 */
	rect.x1 = x;
	rect.x2 = x2 + 1;
	rect.y1 = y;
	rect.y2 = y2 + 1;
	vbox_framebuffer_dirty_rectangles(&fbdev->afb.base, &rect, 1);

	vbox_bo_unreserve(bo);
}

#ifdef CONFIG_FB_DEFERRED_IO
static void vbox_deferred_io(struct fb_info *info, struct list_head *pagelist)
{
	struct vbox_fbdev *fbdev = info->par;
	unsigned long start, end, min, max;
	struct page *page;
	int y1, y2;

	min = ULONG_MAX;
	max = 0;
	list_for_each_entry(page, pagelist, lru) {
		start = page->index << PAGE_SHIFT;
		end = start + PAGE_SIZE - 1;
		min = min(min, start);
		max = max(max, end);
	}

	if (min < max) {
		y1 = min / info->fix.line_length;
		y2 = (max / info->fix.line_length) + 1;
		DRM_INFO("%s: Calling dirty update: 0, %d, %d, %d\n",
			 __func__, y1, info->var.xres, y2 - y1 - 1);
		vbox_dirty_update(fbdev, 0, y1, info->var.xres, y2 - y1 - 1);
	}
}

static struct fb_deferred_io vbox_defio = {
	.delay = VBOX_DIRTY_DELAY,
	.deferred_io = vbox_deferred_io,
};
#endif

static void vbox_fillrect(struct fb_info *info, const struct fb_fillrect *rect)
{
	struct vbox_fbdev *fbdev = info->par;

	sys_fillrect(info, rect);
	vbox_dirty_update(fbdev, rect->dx, rect->dy, rect->width, rect->height);
}

static void vbox_copyarea(struct fb_info *info, const struct fb_copyarea *area)
{
	struct vbox_fbdev *fbdev = info->par;

	sys_copyarea(info, area);
	vbox_dirty_update(fbdev, area->dx, area->dy, area->width, area->height);
}

static void vbox_imageblit(struct fb_info *info, const struct fb_image *image)
{
	struct vbox_fbdev *fbdev = info->par;

	sys_imageblit(info, image);
	vbox_dirty_update(fbdev, image->dx, image->dy, image->width,
			  image->height);
}

static struct fb_ops vboxfb_ops = {
	.owner = THIS_MODULE,
	.fb_check_var = drm_fb_helper_check_var,
	.fb_set_par = drm_fb_helper_set_par,
	.fb_fillrect = vbox_fillrect,
	.fb_copyarea = vbox_copyarea,
	.fb_imageblit = vbox_imageblit,
	.fb_pan_display = drm_fb_helper_pan_display,
	.fb_blank = drm_fb_helper_blank,
	.fb_setcmap = drm_fb_helper_setcmap,
	.fb_debug_enter = drm_fb_helper_debug_enter,
	.fb_debug_leave = drm_fb_helper_debug_leave,
};

static int vboxfb_create_object(struct vbox_fbdev *fbdev,
				struct DRM_MODE_FB_CMD *mode_cmd,
				struct drm_gem_object **gobj_p)
{
	struct drm_device *dev = fbdev->helper.dev;
	u32 size;
	struct drm_gem_object *gobj;
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 3, 0)
	u32 pitch = mode_cmd->pitch;
#else
	u32 pitch = mode_cmd->pitches[0];
#endif

	int ret = 0;

	size = pitch * mode_cmd->height;
	ret = vbox_gem_create(dev, size, true, &gobj);
	if (ret)
		return ret;

	*gobj_p = gobj;
	return ret;
}

static int vboxfb_create(struct drm_fb_helper *helper,
			 struct drm_fb_helper_surface_size *sizes)
{
	struct vbox_fbdev *fbdev =
	    container_of(helper, struct vbox_fbdev, helper);
	struct drm_device *dev = fbdev->helper.dev;
	struct DRM_MODE_FB_CMD mode_cmd;
	struct drm_framebuffer *fb;
	struct fb_info *info;
	struct device *device = &dev->pdev->dev;
	struct drm_gem_object *gobj = NULL;
	struct vbox_bo *bo = NULL;
	int size, ret;
	u32 pitch;

	mode_cmd.width = sizes->surface_width;
	mode_cmd.height = sizes->surface_height;
	pitch = mode_cmd.width * ((sizes->surface_bpp + 7) / 8);
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 3, 0)
	mode_cmd.bpp = sizes->surface_bpp;
	mode_cmd.depth = sizes->surface_depth;
	mode_cmd.pitch = pitch;
#else
	mode_cmd.pixel_format = drm_mode_legacy_fb_format(sizes->surface_bpp,
							  sizes->surface_depth);
	mode_cmd.pitches[0] = pitch;
#endif

	size = pitch * mode_cmd.height;

	ret = vboxfb_create_object(fbdev, &mode_cmd, &gobj);
	if (ret) {
		DRM_ERROR("failed to create fbcon backing object %d\n", ret);
		return ret;
	}

	ret = vbox_framebuffer_init(dev, &fbdev->afb, &mode_cmd, gobj);
	if (ret)
		return ret;

	bo = gem_to_vbox_bo(gobj);

	ret = vbox_bo_reserve(bo, false);
	if (ret)
		return ret;

	ret = vbox_bo_pin(bo, TTM_PL_FLAG_VRAM, NULL);
	if (ret) {
		vbox_bo_unreserve(bo);
		return ret;
	}

	ret = ttm_bo_kmap(&bo->bo, 0, bo->bo.num_pages, &bo->kmap);
	vbox_bo_unreserve(bo);
	if (ret) {
		DRM_ERROR("failed to kmap fbcon\n");
		return ret;
	}

	info = framebuffer_alloc(0, device);
	if (!info)
		return -ENOMEM;
	info->par = fbdev;

	fbdev->size = size;

	fb = &fbdev->afb.base;
	fbdev->helper.fb = fb;
	fbdev->helper.fbdev = info;

	strcpy(info->fix.id, "vboxdrmfb");

	/*
	 * The last flag forces a mode set on VT switches even if the kernel
	 * does not think it is needed.
	 */
	info->flags = FBINFO_DEFAULT | FBINFO_CAN_FORCE_OUTPUT |
		      FBINFO_MISC_ALWAYS_SETPAR;
	info->fbops = &vboxfb_ops;

	ret = fb_alloc_cmap(&info->cmap, 256, 0);
	if (ret)
		return -ENOMEM;

	/*
	 * This seems to be done for safety checking that the framebuffer
	 * is not registered twice by different drivers.
	 */
	info->apertures = alloc_apertures(1);
	if (!info->apertures)
		return -ENOMEM;
	info->apertures->ranges[0].base = pci_resource_start(dev->pdev, 0);
	info->apertures->ranges[0].size = pci_resource_len(dev->pdev, 0);
	info->fix.smem_start = 0;
	info->fix.smem_len = size;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 11, 0) || defined(RHEL_75)
	drm_fb_helper_fill_fix(info, fb->pitches[0], fb->format->depth);
#else
	drm_fb_helper_fill_fix(info, fb->pitches[0], fb->depth);
#endif
	drm_fb_helper_fill_var(info, &fbdev->helper, sizes->fb_width,
			       sizes->fb_height);

	info->screen_base = bo->kmap.virtual;
	info->screen_size = size;

#ifdef CONFIG_FB_DEFERRED_IO
	info->fbdefio = &vbox_defio;
	fb_deferred_io_init(info);
#endif

	info->pixmap.flags = FB_PIXMAP_SYSTEM;

	DRM_DEBUG_KMS("allocated %dx%d\n", fb->width, fb->height);

	return 0;
}

static struct drm_fb_helper_funcs vbox_fb_helper_funcs = {
	.fb_probe = vboxfb_create,
};

static void vbox_fbdev_destroy(struct drm_device *dev, struct vbox_fbdev *fbdev)
{
	struct fb_info *info;
	struct vbox_framebuffer *afb = &fbdev->afb;

	if (fbdev->helper.fbdev) {
		info = fbdev->helper.fbdev;
		unregister_framebuffer(info);
		if (info->cmap.len)
			fb_dealloc_cmap(&info->cmap);
		framebuffer_release(info);
	}

	if (afb->obj) {
		struct vbox_bo *bo = gem_to_vbox_bo(afb->obj);

		if (!vbox_bo_reserve(bo, false)) {
			if (bo->kmap.virtual)
				ttm_bo_kunmap(&bo->kmap);
			/*
			 * QXL does this, but is it really needed before
			 * freeing?
			 */
			if (bo->pin_count)
				vbox_bo_unpin(bo);
			vbox_bo_unreserve(bo);
		}
		drm_gem_object_unreference_unlocked(afb->obj);
		afb->obj = NULL;
	}
	drm_fb_helper_fini(&fbdev->helper);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 9, 0)
	drm_framebuffer_unregister_private(&afb->base);
#endif
	drm_framebuffer_cleanup(&afb->base);
}

int vbox_fbdev_init(struct drm_device *dev)
{
	struct vbox_private *vbox = dev->dev_private;
	struct vbox_fbdev *fbdev;
	int ret;

	fbdev = kzalloc(sizeof(*fbdev), GFP_KERNEL);
	if (!fbdev)
		return -ENOMEM;

	vbox->fbdev = fbdev;
	spin_lock_init(&fbdev->dirty_lock);

#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 17, 0) && !defined(RHEL_73)
	fbdev->helper.funcs = &vbox_fb_helper_funcs;
#else
	drm_fb_helper_prepare(dev, &fbdev->helper, &vbox_fb_helper_funcs);
#endif
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 11, 0) || defined(RHEL_75)
	ret = drm_fb_helper_init(dev, &fbdev->helper, vbox->num_crtcs);
#else
	ret =
	    drm_fb_helper_init(dev, &fbdev->helper, vbox->num_crtcs,
			       vbox->num_crtcs);
#endif
	if (ret)
		goto free;

	ret = drm_fb_helper_single_add_all_connectors(&fbdev->helper);
	if (ret)
		goto fini;

	/* disable all the possible outputs/crtcs before entering KMS mode */
	drm_helper_disable_unused_functions(dev);

	ret = drm_fb_helper_initial_config(&fbdev->helper, 32);
	if (ret)
		goto fini;

	return 0;

fini:
	drm_fb_helper_fini(&fbdev->helper);
free:
	kfree(fbdev);
	vbox->fbdev = NULL;

	return ret;
}

void vbox_fbdev_fini(struct drm_device *dev)
{
	struct vbox_private *vbox = dev->dev_private;

	if (!vbox->fbdev)
		return;

	vbox_fbdev_destroy(dev, vbox->fbdev);
	kfree(vbox->fbdev);
	vbox->fbdev = NULL;
}

void vbox_fbdev_set_suspend(struct drm_device *dev, int state)
{
	struct vbox_private *vbox = dev->dev_private;

	if (!vbox->fbdev)
		return;

	fb_set_suspend(vbox->fbdev->helper.fbdev, state);
}
