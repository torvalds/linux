/* SPDX-License-Identifier: GPL-2.0 */
/* AF_XDP internal functions
 * Copyright(c) 2018 Intel Corporation.
 */

#ifndef _LINUX_XDP_SOCK_H
#define _LINUX_XDP_SOCK_H

#include <linux/bpf.h>
#include <linux/workqueue.h>
#include <linux/if_xdp.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/mm.h>
#include <net/sock.h>

#define XDP_UMEM_SG_FLAG (1 << 1)

struct net_device;
struct xsk_queue;
struct xdp_buff;

struct xdp_umem {
	void *addrs;
	u64 size;
	u32 headroom;
	u32 chunk_size;
	u32 chunks;
	u32 npgs;
	struct user_struct *user;
	refcount_t users;
	u8 flags;
	bool zc;
	struct page **pgs;
	int id;
	struct list_head xsk_dma_list;
	struct work_struct work;
};

struct xsk_map {
	struct bpf_map map;
	spinlock_t lock; /* Synchronize map updates */
	atomic_t count;
	struct xdp_sock __rcu *xsk_map[];
};

struct xdp_sock {
	/* struct sock must be the first member of struct xdp_sock */
	struct sock sk;
	struct xsk_queue *rx ____cacheline_aligned_in_smp;
	struct net_device *dev;
	struct xdp_umem *umem;
	struct list_head flush_node;
	struct xsk_buff_pool *pool;
	u16 queue_id;
	bool zc;
	bool sg;
	enum {
		XSK_READY = 0,
		XSK_BOUND,
		XSK_UNBOUND,
	} state;

	struct xsk_queue *tx ____cacheline_aligned_in_smp;
	struct list_head tx_list;
	/* record the number of tx descriptors sent by this xsk and
	 * when it exceeds MAX_PER_SOCKET_BUDGET, an opportunity needs
	 * to be given to other xsks for sending tx descriptors, thereby
	 * preventing other XSKs from being starved.
	 */
	u32 tx_budget_spent;

	/* Protects generic receive. */
	spinlock_t rx_lock;

	/* Statistics */
	u64 rx_dropped;
	u64 rx_queue_full;

	/* When __xsk_generic_xmit() must return before it sees the EOP descriptor for the current
	 * packet, the partially built skb is saved here so that packet building can resume in next
	 * call of __xsk_generic_xmit().
	 */
	struct sk_buff *skb;

	struct list_head map_list;
	/* Protects map_list */
	spinlock_t map_list_lock;
	/* Protects multiple processes in the control path */
	struct mutex mutex;
	struct xsk_queue *fq_tmp; /* Only as tmp storage before bind */
	struct xsk_queue *cq_tmp; /* Only as tmp storage before bind */
};

#ifdef CONFIG_XDP_SOCKETS

int xsk_generic_rcv(struct xdp_sock *xs, struct xdp_buff *xdp);
int __xsk_map_redirect(struct xdp_sock *xs, struct xdp_buff *xdp);
void __xsk_map_flush(void);

#else

static inline int xsk_generic_rcv(struct xdp_sock *xs, struct xdp_buff *xdp)
{
	return -ENOTSUPP;
}

static inline int __xsk_map_redirect(struct xdp_sock *xs, struct xdp_buff *xdp)
{
	return -EOPNOTSUPP;
}

static inline void __xsk_map_flush(void)
{
}

#endif /* CONFIG_XDP_SOCKETS */

#if defined(CONFIG_XDP_SOCKETS) && defined(CONFIG_DEBUG_NET)
bool xsk_map_check_flush(void);
#else
static inline bool xsk_map_check_flush(void)
{
	return false;
}
#endif

#endif /* _LINUX_XDP_SOCK_H */
