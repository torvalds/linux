/*
 * include/video/tegrafb.h
 *
 * Copyright (C) 2010 Google, Inc.
 * Author: Erik Gilling <konkers@android.com>
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

#ifndef _LINUX_TEGRAFB_H_
#define _LINUX_TEGRAFB_H_

#include <linux/fb.h>
#include <linux/types.h>
#include <asm/ioctl.h>

#define TEGRA_FB_WIN_FMT_P1		0
#define TEGRA_FB_WIN_FMT_P2		1
#define TEGRA_FB_WIN_FMT_P4		2
#define TEGRA_FB_WIN_FMT_P8		3
#define TEGRA_FB_WIN_FMT_B4G4R4A4	4
#define TEGRA_FB_WIN_FMT_B5G5R5A	5
#define TEGRA_FB_WIN_FMT_B5G6R5		6
#define TEGRA_FB_WIN_FMT_AB5G5R5	7
#define TEGRA_FB_WIN_FMT_B8G8R8A8	12
#define TEGRA_FB_WIN_FMT_R8G8B8A8	13
#define TEGRA_FB_WIN_FMT_B6x2G6x2R6x2A8	14
#define TEGRA_FB_WIN_FMT_R6x2G6x2B6x2A8	15
#define TEGRA_FB_WIN_FMT_YCbCr422	16
#define TEGRA_FB_WIN_FMT_YUV422		17
#define TEGRA_FB_WIN_FMT_YCbCr420P	18
#define TEGRA_FB_WIN_FMT_YUV420P	19
#define TEGRA_FB_WIN_FMT_YCbCr422P	20
#define TEGRA_FB_WIN_FMT_YUV422P	21
#define TEGRA_FB_WIN_FMT_YCbCr422R	22
#define TEGRA_FB_WIN_FMT_YUV422R	23
#define TEGRA_FB_WIN_FMT_YCbCr422RA	24
#define TEGRA_FB_WIN_FMT_YUV422RA	25

#define TEGRA_FB_WIN_BLEND_NONE		0
#define TEGRA_FB_WIN_BLEND_PREMULT	1
#define TEGRA_FB_WIN_BLEND_COVERAGE	2

#define TEGRA_FB_WIN_FLAG_INVERT_H	(1 << 0)
#define TEGRA_FB_WIN_FLAG_INVERT_V	(1 << 1)
#define TEGRA_FB_WIN_FLAG_TILED		(1 << 2)

/* set index to -1 to ignore window data */
struct tegra_fb_windowattr {
	__s32	index;
	__u32	buff_id;
	__u32	flags;
	__u32	blend;
	__u32	offset;
	__u32	offset_u;
	__u32	offset_v;
	__u32	stride;
	__u32	stride_uv;
	__u32	pixformat;
	__u32	x;
	__u32	y;
	__u32	w;
	__u32	h;
	__u32	out_x;
	__u32	out_y;
	__u32	out_w;
	__u32	out_h;
	__u32	z;
	__u32	pre_syncpt_id;
	__u32	pre_syncpt_val;
};

#define TEGRA_FB_FLIP_N_WINDOWS		3

struct tegra_fb_flip_args {
	struct tegra_fb_windowattr win[TEGRA_FB_FLIP_N_WINDOWS];
	__u32 post_syncpt_id;
	__u32 post_syncpt_val;
};

struct tegra_fb_modedb {
	struct fb_var_screeninfo *modedb;
	__u32 modedb_len;
};

#define FBIO_TEGRA_SET_NVMAP_FD	_IOW('F', 0x40, __u32)
#define FBIO_TEGRA_FLIP		_IOW('F', 0x41, struct tegra_fb_flip_args)
#define FBIO_TEGRA_GET_MODEDB	_IOWR('F', 0x42, struct tegra_fb_modedb)

#endif
