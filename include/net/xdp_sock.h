/* SPDX-License-Identifier: GPL-2.0
 * AF_XDP internal functions
 * Copyright(c) 2018 Intel Corporation.
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

#ifndef _LINUX_XDP_SOCK_H
#define _LINUX_XDP_SOCK_H

#include <linux/mutex.h>
#include <net/sock.h>

struct xdp_umem;

struct xdp_sock {
	/* struct sock must be the first member of struct xdp_sock */
	struct sock sk;
	struct xdp_umem *umem;
	/* Protects multiple processes in the control path */
	struct mutex mutex;
};

#endif /* _LINUX_XDP_SOCK_H */
