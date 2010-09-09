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

#include <linux/types.h>
#include <asm/ioctl.h>

struct tegra_fb_flip_args {
	__u32	buff_id;
	__u32	pre_syncpt_id;
	__u32	pre_syncpt_val;
	__u32	post_syncpt_id;
	__u32	post_syncpt_val;
};

#define FBIO_TEGRA_SET_NVMAP_FD	_IOW('F', 0x40, __u32)
#define FBIO_TEGRA_FLIP		_IOW('F', 0x41, struct tegra_fb_flip_args)

#endif
