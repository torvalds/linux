/*
 * Copyright (C) 2013 Google, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef _VIDEO_ADF_FBDEV_H_
#define _VIDEO_ADF_FBDEV_H_

#include <linux/fb.h>
#include <linux/mutex.h>
#include <video/adf.h>

struct adf_fbdev {
	struct adf_interface *intf;
	struct adf_overlay_engine *eng;
	struct fb_info *info;
	u32 pseudo_palette[16];

	unsigned int refcount;
	struct mutex refcount_lock;

	struct dma_buf *dma_buf;
	u32 offset;
	u32 pitch;
	void *vaddr;
	u32 format;

	u16 default_xres_virtual;
	u16 default_yres_virtual;
	u32 default_format;
};

#if IS_ENABLED(CONFIG_ADF_FBDEV)
void adf_modeinfo_to_fb_videomode(const struct drm_mode_modeinfo *mode,
		struct fb_videomode *vmode);
void adf_modeinfo_from_fb_videomode(const struct fb_videomode *vmode,
		struct drm_mode_modeinfo *mode);

int adf_fbdev_init(struct adf_fbdev *fbdev, struct adf_interface *interface,
		struct adf_overlay_engine *eng,
		u16 xres_virtual, u16 yres_virtual, u32 format,
		struct fb_ops *fbops, const char *fmt, ...);
void adf_fbdev_destroy(struct adf_fbdev *fbdev);

int adf_fbdev_open(struct fb_info *info, int user);
int adf_fbdev_release(struct fb_info *info, int user);
int adf_fbdev_check_var(struct fb_var_screeninfo *var, struct fb_info *info);
int adf_fbdev_set_par(struct fb_info *info);
int adf_fbdev_blank(int blank, struct fb_info *info);
int adf_fbdev_pan_display(struct fb_var_screeninfo *var, struct fb_info *info);
int adf_fbdev_mmap(struct fb_info *info, struct vm_area_struct *vma);
#else
static inline void adf_modeinfo_to_fb_videomode(const struct drm_mode_modeinfo *mode,
		struct fb_videomode *vmode)
{
	WARN_ONCE(1, "%s: CONFIG_ADF_FBDEV is disabled\n", __func__);
}

static inline void adf_modeinfo_from_fb_videomode(const struct fb_videomode *vmode,
		struct drm_mode_modeinfo *mode)
{
	WARN_ONCE(1, "%s: CONFIG_ADF_FBDEV is disabled\n", __func__);
}

static inline int adf_fbdev_init(struct adf_fbdev *fbdev,
		struct adf_interface *interface,
		struct adf_overlay_engine *eng,
		u16 xres_virtual, u16 yres_virtual, u32 format,
		struct fb_ops *fbops, const char *fmt, ...)
{
	return -ENODEV;
}

static inline void adf_fbdev_destroy(struct adf_fbdev *fbdev) { }

static inline int adf_fbdev_open(struct fb_info *info, int user)
{
	return -ENODEV;
}

static inline int adf_fbdev_release(struct fb_info *info, int user)
{
	return -ENODEV;
}

static inline int adf_fbdev_check_var(struct fb_var_screeninfo *var,
		struct fb_info *info)
{
	return -ENODEV;
}

static inline int adf_fbdev_set_par(struct fb_info *info)
{
	return -ENODEV;
}

static inline int adf_fbdev_blank(int blank, struct fb_info *info)
{
	return -ENODEV;
}

static inline int adf_fbdev_pan_display(struct fb_var_screeninfo *var,
		struct fb_info *info)
{
	return -ENODEV;
}

static inline int adf_fbdev_mmap(struct fb_info *info,
		struct vm_area_struct *vma)
{
	return -ENODEV;
}
#endif

#endif /* _VIDEO_ADF_FBDEV_H_ */
