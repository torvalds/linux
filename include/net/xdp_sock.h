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

struct net_device;
struct xsk_queue;
struct xdp_umem;

struct xdp_sock {
	/* struct sock must be the first member of struct xdp_sock */
	struct sock sk;
	struct xsk_queue *rx;
	struct net_device *dev;
	struct xdp_umem *umem;
	u16 queue_id;
	/* Protects multiple processes in the control path */
	struct mutex mutex;
	u64 rx_dropped;
};

struct xdp_buff;
#ifdef CONFIG_XDP_SOCKETS
int xsk_generic_rcv(struct xdp_sock *xs, struct xdp_buff *xdp);
int xsk_rcv(struct xdp_sock *xs, struct xdp_buff *xdp);
void xsk_flush(struct xdp_sock *xs);
#else
static inline int xsk_generic_rcv(struct xdp_sock *xs, struct xdp_buff *xdp)
{
	return -ENOTSUPP;
}

static inline int xsk_rcv(struct xdp_sock *xs, struct xdp_buff *xdp)
{
	return -ENOTSUPP;
}

static inline void xsk_flush(struct xdp_sock *xs)
{
}
#endif /* CONFIG_XDP_SOCKETS */

#endif /* _LINUX_XDP_SOCK_H */
