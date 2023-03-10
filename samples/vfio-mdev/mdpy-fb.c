// SPDX-License-Identifier: GPL-2.0
/*
 * Framebuffer driver for mdpy (mediated virtual pci display device).
 *
 * See mdpy-defs.h for device specs
 *
 *   (c) Gerd Hoffmann <kraxel@redhat.com>
 *
 * Using some code snippets from simplefb and cirrusfb.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */
#include <linux/errno.h>
#include <linux/fb.h>
#include <linux/io.h>
#include <linux/pci.h>
#include <linux/module.h>
#include <drm/drm_fourcc.h>
#include "mdpy-defs.h"

static const struct fb_fix_screeninfo mdpy_fb_fix = {
	.id		= "mdpy-fb",
	.type		= FB_TYPE_PACKED_PIXELS,
	.visual		= FB_VISUAL_TRUECOLOR,
	.accel		= FB_ACCEL_NONE,
};

static const struct fb_var_screeninfo mdpy_fb_var = {
	.height		= -1,
	.width		= -1,
	.activate	= FB_ACTIVATE_NOW,
	.vmode		= FB_VMODE_NONINTERLACED,

	.bits_per_pixel = 32,
	.transp.offset	= 24,
	.red.offset	= 16,
	.green.offset	= 8,
	.blue.offset	= 0,
	.transp.length	= 8,
	.red.length	= 8,
	.green.length	= 8,
	.blue.length	= 8,
};

#define PSEUDO_PALETTE_SIZE 16

struct mdpy_fb_par {
	u32 palette[PSEUDO_PALETTE_SIZE];
};

static int mdpy_fb_setcolreg(u_int regno, u_int red, u_int green, u_int blue,
			      u_int transp, struct fb_info *info)
{
	u32 *pal = info->pseudo_palette;
	u32 cr = red >> (16 - info->var.red.length);
	u32 cg = green >> (16 - info->var.green.length);
	u32 cb = blue >> (16 - info->var.blue.length);
	u32 value, mask;

	if (regno >= PSEUDO_PALETTE_SIZE)
		return -EINVAL;

	value = (cr << info->var.red.offset) |
		(cg << info->var.green.offset) |
		(cb << info->var.blue.offset);
	if (info->var.transp.length > 0) {
		mask = (1 << info->var.transp.length) - 1;
		mask <<= info->var.transp.offset;
		value |= mask;
	}
	pal[regno] = value;

	return 0;
}

static void mdpy_fb_destroy(struct fb_info *info)
{
	if (info->screen_base)
		iounmap(info->screen_base);
}

static const struct fb_ops mdpy_fb_ops = {
	.owner		= THIS_MODULE,
	.fb_destroy	= mdpy_fb_destroy,
	.fb_setcolreg	= mdpy_fb_setcolreg,
	.fb_fillrect	= cfb_fillrect,
	.fb_copyarea	= cfb_copyarea,
	.fb_imageblit	= cfb_imageblit,
};

static int mdpy_fb_probe(struct pci_dev *pdev,
			 const struct pci_device_id *ent)
{
	struct fb_info *info;
	struct mdpy_fb_par *par;
	u32 format, width, height;
	int ret;

	ret = pci_enable_device(pdev);
	if (ret < 0)
		return ret;

	ret = pci_request_regions(pdev, "mdpy-fb");
	if (ret < 0)
		goto err_disable_dev;

	pci_read_config_dword(pdev, MDPY_FORMAT_OFFSET, &format);
	pci_read_config_dword(pdev, MDPY_WIDTH_OFFSET,	&width);
	pci_read_config_dword(pdev, MDPY_HEIGHT_OFFSET, &height);
	if (format != DRM_FORMAT_XRGB8888) {
		pci_err(pdev, "format mismatch (0x%x != 0x%x)\n",
			format, DRM_FORMAT_XRGB8888);
		ret = -EINVAL;
		goto err_release_regions;
	}
	if (width < 100	 || width > 10000) {
		pci_err(pdev, "width (%d) out of range\n", width);
		ret = -EINVAL;
		goto err_release_regions;
	}
	if (height < 100 || height > 10000) {
		pci_err(pdev, "height (%d) out of range\n", height);
		ret = -EINVAL;
		goto err_release_regions;
	}
	pci_info(pdev, "mdpy found: %dx%d framebuffer\n",
		 width, height);

	info = framebuffer_alloc(sizeof(struct mdpy_fb_par), &pdev->dev);
	if (!info) {
		ret = -ENOMEM;
		goto err_release_regions;
	}
	pci_set_drvdata(pdev, info);
	par = info->par;

	info->fix = mdpy_fb_fix;
	info->fix.smem_start = pci_resource_start(pdev, 0);
	info->fix.smem_len = pci_resource_len(pdev, 0);
	info->fix.line_length = width * 4;

	info->var = mdpy_fb_var;
	info->var.xres = width;
	info->var.yres = height;
	info->var.xres_virtual = width;
	info->var.yres_virtual = height;

	info->screen_size = info->fix.smem_len;
	info->screen_base = ioremap(info->fix.smem_start,
				    info->screen_size);
	if (!info->screen_base) {
		pci_err(pdev, "ioremap(pcibar) failed\n");
		ret = -EIO;
		goto err_release_fb;
	}

	info->fbops = &mdpy_fb_ops;
	info->flags = FBINFO_DEFAULT;
	info->pseudo_palette = par->palette;

	ret = register_framebuffer(info);
	if (ret < 0) {
		pci_err(pdev, "mdpy-fb device register failed: %d\n", ret);
		goto err_unmap;
	}

	pci_info(pdev, "fb%d registered\n", info->node);
	return 0;

err_unmap:
	iounmap(info->screen_base);

err_release_fb:
	framebuffer_release(info);

err_release_regions:
	pci_release_regions(pdev);

err_disable_dev:
	pci_disable_device(pdev);

	return ret;
}

static void mdpy_fb_remove(struct pci_dev *pdev)
{
	struct fb_info *info = pci_get_drvdata(pdev);

	unregister_framebuffer(info);
	iounmap(info->screen_base);
	framebuffer_release(info);
	pci_release_regions(pdev);
	pci_disable_device(pdev);
}

static struct pci_device_id mdpy_fb_pci_table[] = {
	{
		.vendor	   = MDPY_PCI_VENDOR_ID,
		.device	   = MDPY_PCI_DEVICE_ID,
		.subvendor = MDPY_PCI_SUBVENDOR_ID,
		.subdevice = MDPY_PCI_SUBDEVICE_ID,
	}, {
		/* end of list */
	}
};

static struct pci_driver mdpy_fb_pci_driver = {
	.name		= "mdpy-fb",
	.id_table	= mdpy_fb_pci_table,
	.probe		= mdpy_fb_probe,
	.remove		= mdpy_fb_remove,
};

static int __init mdpy_fb_init(void)
{
	int ret;

	ret = pci_register_driver(&mdpy_fb_pci_driver);
	if (ret)
		return ret;

	return 0;
}

module_init(mdpy_fb_init);

MODULE_DEVICE_TABLE(pci, mdpy_fb_pci_table);
MODULE_LICENSE("GPL v2");
