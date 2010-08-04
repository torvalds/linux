/*
 * File: include/linux/omapfb.h
 *
 * Framebuffer driver for TI OMAP boards
 *
 * Copyright (C) 2004 Nokia Corporation
 * Author: Imre Deak <imre.deak@nokia.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */

#ifndef __LINUX_OMAPFB_H__
#define __LINUX_OMAPFB_H__

#include <linux/fb.h>
#include <linux/ioctl.h>
#include <linux/types.h>

/* IOCTL commands. */

#define OMAP_IOW(num, dtype)	_IOW('O', num, dtype)
#define OMAP_IOR(num, dtype)	_IOR('O', num, dtype)
#define OMAP_IOWR(num, dtype)	_IOWR('O', num, dtype)
#define OMAP_IO(num)		_IO('O', num)

#define OMAPFB_MIRROR		OMAP_IOW(31, int)
#define OMAPFB_SYNC_GFX		OMAP_IO(37)
#define OMAPFB_VSYNC		OMAP_IO(38)
#define OMAPFB_SET_UPDATE_MODE	OMAP_IOW(40, int)
#define OMAPFB_GET_CAPS		OMAP_IOR(42, struct omapfb_caps)
#define OMAPFB_GET_UPDATE_MODE	OMAP_IOW(43, int)
#define OMAPFB_LCD_TEST		OMAP_IOW(45, int)
#define OMAPFB_CTRL_TEST	OMAP_IOW(46, int)
#define OMAPFB_UPDATE_WINDOW_OLD OMAP_IOW(47, struct omapfb_update_window_old)
#define OMAPFB_SET_COLOR_KEY	OMAP_IOW(50, struct omapfb_color_key)
#define OMAPFB_GET_COLOR_KEY	OMAP_IOW(51, struct omapfb_color_key)
#define OMAPFB_SETUP_PLANE	OMAP_IOW(52, struct omapfb_plane_info)
#define OMAPFB_QUERY_PLANE	OMAP_IOW(53, struct omapfb_plane_info)
#define OMAPFB_UPDATE_WINDOW	OMAP_IOW(54, struct omapfb_update_window)
#define OMAPFB_SETUP_MEM	OMAP_IOW(55, struct omapfb_mem_info)
#define OMAPFB_QUERY_MEM	OMAP_IOW(56, struct omapfb_mem_info)
#define OMAPFB_WAITFORVSYNC	OMAP_IO(57)
#define OMAPFB_MEMORY_READ	OMAP_IOR(58, struct omapfb_memory_read)
#define OMAPFB_GET_OVERLAY_COLORMODE OMAP_IOR(59, struct omapfb_ovl_colormode)
#define OMAPFB_WAITFORGO	OMAP_IO(60)
#define OMAPFB_GET_VRAM_INFO	OMAP_IOR(61, struct omapfb_vram_info)
#define OMAPFB_SET_TEARSYNC	OMAP_IOW(62, struct omapfb_tearsync_info)
#define OMAPFB_GET_DISPLAY_INFO	OMAP_IOR(63, struct omapfb_display_info)

#define OMAPFB_CAPS_GENERIC_MASK	0x00000fff
#define OMAPFB_CAPS_LCDC_MASK		0x00fff000
#define OMAPFB_CAPS_PANEL_MASK		0xff000000

#define OMAPFB_CAPS_MANUAL_UPDATE	0x00001000
#define OMAPFB_CAPS_TEARSYNC		0x00002000
#define OMAPFB_CAPS_PLANE_RELOCATE_MEM	0x00004000
#define OMAPFB_CAPS_PLANE_SCALE		0x00008000
#define OMAPFB_CAPS_WINDOW_PIXEL_DOUBLE	0x00010000
#define OMAPFB_CAPS_WINDOW_SCALE	0x00020000
#define OMAPFB_CAPS_WINDOW_OVERLAY	0x00040000
#define OMAPFB_CAPS_WINDOW_ROTATE	0x00080000
#define OMAPFB_CAPS_SET_BACKLIGHT	0x01000000

/* Values from DSP must map to lower 16-bits */
#define OMAPFB_FORMAT_MASK		0x00ff
#define OMAPFB_FORMAT_FLAG_DOUBLE	0x0100
#define OMAPFB_FORMAT_FLAG_TEARSYNC	0x0200
#define OMAPFB_FORMAT_FLAG_FORCE_VSYNC	0x0400
#define OMAPFB_FORMAT_FLAG_ENABLE_OVERLAY	0x0800
#define OMAPFB_FORMAT_FLAG_DISABLE_OVERLAY	0x1000

#define OMAPFB_MEMTYPE_SDRAM		0
#define OMAPFB_MEMTYPE_SRAM		1
#define OMAPFB_MEMTYPE_MAX		1

enum omapfb_color_format {
	OMAPFB_COLOR_RGB565 = 0,
	OMAPFB_COLOR_YUV422,
	OMAPFB_COLOR_YUV420,
	OMAPFB_COLOR_CLUT_8BPP,
	OMAPFB_COLOR_CLUT_4BPP,
	OMAPFB_COLOR_CLUT_2BPP,
	OMAPFB_COLOR_CLUT_1BPP,
	OMAPFB_COLOR_RGB444,
	OMAPFB_COLOR_YUY422,

	OMAPFB_COLOR_ARGB16,
	OMAPFB_COLOR_RGB24U,	/* RGB24, 32-bit container */
	OMAPFB_COLOR_RGB24P,	/* RGB24, 24-bit container */
	OMAPFB_COLOR_ARGB32,
	OMAPFB_COLOR_RGBA32,
	OMAPFB_COLOR_RGBX32,
};

struct omapfb_update_window {
	__u32 x, y;
	__u32 width, height;
	__u32 format;
	__u32 out_x, out_y;
	__u32 out_width, out_height;
	__u32 reserved[8];
};

struct omapfb_update_window_old {
	__u32 x, y;
	__u32 width, height;
	__u32 format;
};

enum omapfb_plane {
	OMAPFB_PLANE_GFX = 0,
	OMAPFB_PLANE_VID1,
	OMAPFB_PLANE_VID2,
};

enum omapfb_channel_out {
	OMAPFB_CHANNEL_OUT_LCD = 0,
	OMAPFB_CHANNEL_OUT_DIGIT,
};

struct omapfb_plane_info {
	__u32 pos_x;
	__u32 pos_y;
	__u8  enabled;
	__u8  channel_out;
	__u8  mirror;
	__u8  reserved1;
	__u32 out_width;
	__u32 out_height;
	__u32 reserved2[12];
};

struct omapfb_mem_info {
	__u32 size;
	__u8  type;
	__u8  reserved[3];
};

struct omapfb_caps {
	__u32 ctrl;
	__u32 plane_color;
	__u32 wnd_color;
};

enum omapfb_color_key_type {
	OMAPFB_COLOR_KEY_DISABLED = 0,
	OMAPFB_COLOR_KEY_GFX_DST,
	OMAPFB_COLOR_KEY_VID_SRC,
};

struct omapfb_color_key {
	__u8  channel_out;
	__u32 background;
	__u32 trans_key;
	__u8  key_type;
};

enum omapfb_update_mode {
	OMAPFB_UPDATE_DISABLED = 0,
	OMAPFB_AUTO_UPDATE,
	OMAPFB_MANUAL_UPDATE
};

struct omapfb_memory_read {
	__u16 x;
	__u16 y;
	__u16 w;
	__u16 h;
	size_t buffer_size;
	void __user *buffer;
};

struct omapfb_ovl_colormode {
	__u8 overlay_idx;
	__u8 mode_idx;
	__u32 bits_per_pixel;
	__u32 nonstd;
	struct fb_bitfield red;
	struct fb_bitfield green;
	struct fb_bitfield blue;
	struct fb_bitfield transp;
};

struct omapfb_vram_info {
	__u32 total;
	__u32 free;
	__u32 largest_free_block;
	__u32 reserved[5];
};

struct omapfb_tearsync_info {
	__u8 enabled;
	__u8 reserved1[3];
	__u16 line;
	__u16 reserved2;
};

struct omapfb_display_info {
	__u16 xres;
	__u16 yres;
	__u32 width;	/* phys width of the display in micrometers */
	__u32 height;	/* phys height of the display in micrometers */
	__u32 reserved[5];
};

#ifdef __KERNEL__

#include <plat/board.h>

#ifdef CONFIG_ARCH_OMAP1
#define OMAPFB_PLANE_NUM		1
#else
#define OMAPFB_PLANE_NUM		3
#endif

struct omapfb_mem_region {
	u32		paddr;
	void __iomem	*vaddr;
	unsigned long	size;
	u8		type;		/* OMAPFB_PLANE_MEM_* */
	enum omapfb_color_format format;/* OMAPFB_COLOR_* */
	unsigned	format_used:1;	/* Must be set when format is set.
					 * Needed b/c of the badly chosen 0
					 * base for OMAPFB_COLOR_* values
					 */
	unsigned	alloc:1;	/* allocated by the driver */
	unsigned	map:1;		/* kernel mapped by the driver */
};

struct omapfb_mem_desc {
	int				region_cnt;
	struct omapfb_mem_region	region[OMAPFB_PLANE_NUM];
};

struct omapfb_platform_data {
	struct omap_lcd_config		lcd;
	struct omapfb_mem_desc		mem_desc;
	void				*ctrl_platform_data;
};

/* in arch/arm/plat-omap/fb.c */
extern void omapfb_set_platform_data(struct omapfb_platform_data *data);
extern void omapfb_set_ctrl_platform_data(void *pdata);
extern void omapfb_reserve_sdram_memblock(void);

#endif

#endif /* __OMAPFB_H */
