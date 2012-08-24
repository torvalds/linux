/*
 *  VT8500/WM8505 Frame Buffer platform data definitions
 *
 *  Copyright (C) 2010 Ed Spiridonov <edo.rus@gmail.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef _VT8500FB_H
#define _VT8500FB_H

#include <linux/fb.h>

struct vt8500fb_platform_data {
	struct fb_videomode	mode;
	u32			xres_virtual;
	u32			yres_virtual;
	u32			bpp;
	unsigned long		video_mem_phys;
	void			*video_mem_virt;
	unsigned long		video_mem_len;
};

#endif /* _VT8500FB_H */
