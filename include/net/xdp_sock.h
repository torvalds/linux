/* SPDX-License-Identifier: GPL-2.0 */
/* AF_XDP internal functions
 * Copyright(c) 2018 Intel Corporation.
 */

#ifndef _LINUX_XDP_SOCK_H
#define _LINUX_XDP_SOCK_H

#include <linux/workqueue.h>
#include <linux/if_xdp.h>
#include <linux/mutex.h>
#include <linux/mm.h>
#include <net/sock.h>

struct net_device;
struct xsk_queue;

struct xdp_umem_props {
	u64 chunk_mask;
	u64 size;
};

struct xdp_umem_page {
	void *addr;
};

struct xdp_umem {
	struct xsk_queue *fq;
	struct xsk_queue *cq;
	struct xdp_umem_page *pages;
	struct xdp_umem_props props;
	u32 headroom;
	u32 chunk_size_nohr;
	struct user_struct *user;
	struct pid *pid;
	unsigned long address;
	refcount_t users;
	struct work_struct work;
	struct page **pgs;
	u32 npgs;
};

struct xdp_sock {
	/* struct sock must be the first member of struct xdp_sock */
	struct sock sk;
	struct xsk_queue *rx;
	struct net_device *dev;
	struct xdp_umem *umem;
	struct list_head flush_node;
	u16 queue_id;
	struct xsk_queue *tx ____cacheline_aligned_in_smp;
	/* Protects multiple processes in the control path */
	struct mutex mutex;
	u64 rx_dropped;
};

struct xdp_buff;
#ifdef CONFIG_XDP_SOCKETS
int xsk_generic_rcv(struct xdp_sock *xs, struct xdp_buff *xdp);
int xsk_rcv(struct xdp_sock *xs, struct xdp_buff *xdp);
void xsk_flush(struct xdp_sock *xs);
bool xsk_is_setup_for_bpf_map(struct xdp_sock *xs);
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

static inline bool xsk_is_setup_for_bpf_map(struct xdp_sock *xs)
{
	return false;
}
#endif /* CONFIG_XDP_SOCKETS */

#endif /* _LINUX_XDP_SOCK_H */
