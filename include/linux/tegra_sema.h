/*
 * Copyright (C) 2010 Google, Inc.
 *
 * Author:
 *   Dima Zavin <dima@android.com>
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

#ifndef __LINUX_TEGRA_SEMA_H
#define __LINUX_TEGRA_SEMA_H

/* this shares the magic with the tegra RPC and AVP drivers.
 *  See include/linux/tegra_avp.h and include/linux/tegra_rpc.h */
#define TEGRA_SEMA_IOCTL_MAGIC		'r'

/* If IOCTL_WAIT is interrupted by a signal and the timeout was not -1,
 * then the value pointed to by the argument will be updated with the amount
 * of time remaining for the wait. */
#define TEGRA_SEMA_IOCTL_WAIT		_IOW(TEGRA_SEMA_IOCTL_MAGIC, 0x30, long *)
#define TEGRA_SEMA_IOCTL_SIGNAL		_IO(TEGRA_SEMA_IOCTL_MAGIC, 0x31)

#define TEGRA_SEMA_IOCTL_MIN_NR		_IOC_NR(TEGRA_SEMA_IOCTL_WAIT)
#define TEGRA_SEMA_IOCTL_MAX_NR		_IOC_NR(TEGRA_SEMA_IOCTL_SIGNAL)

#endif
