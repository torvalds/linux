/*
 * Copyright (C) 2010 Google, Inc.
 *
 * Author:
 *   Dima Zavin <dima@android.com>
 *
 * Based on original code from NVIDIA, and a partial rewrite by:
 *   Gary King <gking@nvidia.com>
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

#ifndef __LINUX_TEGRA_RPC_H
#define __LINUX_TEGRA_RPC_H

#define TEGRA_RPC_MAX_MSG_LEN		256

/* Note: the actual size of the name in the protocol message is 16 bytes,
 * but that is because the name there is not NUL terminated, only NUL
 * padded. */
#define TEGRA_RPC_MAX_NAME_LEN		17

struct tegra_rpc_port_desc {
	char name[TEGRA_RPC_MAX_NAME_LEN];
	int notify_fd; /* fd representing a trpc_sema to signal when a
			* message has been received */
};

#define TEGRA_RPC_IOCTL_MAGIC		'r'

#define TEGRA_RPC_IOCTL_PORT_CREATE	_IOW(TEGRA_RPC_IOCTL_MAGIC, 0x20, struct tegra_rpc_port_desc)
#define TEGRA_RPC_IOCTL_PORT_GET_NAME	_IOR(TEGRA_RPC_IOCTL_MAGIC, 0x21, char *)
#define TEGRA_RPC_IOCTL_PORT_CONNECT	_IOR(TEGRA_RPC_IOCTL_MAGIC, 0x22, long)
#define TEGRA_RPC_IOCTL_PORT_LISTEN	_IOR(TEGRA_RPC_IOCTL_MAGIC, 0x23, long)

#define TEGRA_RPC_IOCTL_MIN_NR		_IOC_NR(TEGRA_RPC_IOCTL_PORT_CREATE)
#define TEGRA_RPC_IOCTL_MAX_NR		_IOC_NR(TEGRA_RPC_IOCTL_PORT_LISTEN)

#endif
