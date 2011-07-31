/*
 * Copyright (C) 2010 Google, Inc.
 * Author: Dima Zavin <dima@android.com>
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

#ifndef __LINUX_TEGRA_AVP_H
#define __LINUX_TEGRA_AVP_H

#include <linux/ioctl.h>
#include <linux/types.h>

#define TEGRA_AVP_LIB_MAX_NAME		32
#define TEGRA_AVP_LIB_MAX_ARGS		220 /* DO NOT CHANGE THIS! */

struct tegra_avp_lib {
	char		name[TEGRA_AVP_LIB_MAX_NAME];
	void __user	*args;
	size_t		args_len;
	int		greedy;
	unsigned long	handle;
};

#define TEGRA_AVP_IOCTL_MAGIC		'r'

#define TEGRA_AVP_IOCTL_LOAD_LIB	_IOWR(TEGRA_AVP_IOCTL_MAGIC, 0x40, struct tegra_avp_lib)
#define TEGRA_AVP_IOCTL_UNLOAD_LIB	_IOW(TEGRA_AVP_IOCTL_MAGIC, 0x41, unsigned long)

#define TEGRA_AVP_IOCTL_MIN_NR		_IOC_NR(TEGRA_AVP_IOCTL_LOAD_LIB)
#define TEGRA_AVP_IOCTL_MAX_NR		_IOC_NR(TEGRA_AVP_IOCTL_UNLOAD_LIB)

#endif
