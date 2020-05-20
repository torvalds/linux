/* SPDX-License-Identifier: GPL-2.0 */
/* AF_XDP internal functions
 * Copyright(c) 2018 Intel Corporation.
 */

#ifndef _LINUX_XDP_SOCK_H
#define _LINUX_XDP_SOCK_H

#include <linux/workqueue.h>
#include <linux/if_xdp.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/mm.h>
#include <net/sock.h>

struct net_device;
struct xsk_queue;
struct xdp_buff;

struct xdp_umem_page {
	void *addr;
	dma_addr_t dma;
};

struct xdp_umem_fq_reuse {
	u32 nentries;
	u32 length;
	u64 handles[];
};

struct xdp_umem {
	struct xsk_queue *fq;
	struct xsk_queue *cq;
	struct xdp_umem_page *pages;
	u64 chunk_mask;
	u64 size;
	u32 headroom;
	u32 chunk_size_nohr;
	struct user_struct *user;
	refcount_t users;
	struct work_struct work;
	struct page **pgs;
	u32 npgs;
	u16 queue_id;
	u8 need_wakeup;
	u8 flags;
	int id;
	struct net_device *dev;
	struct xdp_umem_fq_reuse *fq_reuse;
	bool zc;
	spinlock_t xsk_tx_list_lock;
	struct list_head xsk_tx_list;
};

struct xsk_map {
	struct bpf_map map;
	spinlock_t lock; /* Synchronize map updates */
	struct xdp_sock *xsk_map[];
};

struct xdp_sock {
	/* struct sock must be the first member of struct xdp_sock */
	struct sock sk;
	struct xsk_queue *rx;
	struct net_device *dev;
	struct xdp_umem *umem;
	struct list_head flush_node;
	u16 queue_id;
	bool zc;
	enum {
		XSK_READY = 0,
		XSK_BOUND,
		XSK_UNBOUND,
	} state;
	/* Protects multiple processes in the control path */
	struct mutex mutex;
	struct xsk_queue *tx ____cacheline_aligned_in_smp;
	struct list_head list;
	/* Mutual exclusion of NAPI TX thread and sendmsg error paths
	 * in the SKB destructor callback.
	 */
	spinlock_t tx_completion_lock;
	/* Protects generic receive. */
	spinlock_t rx_lock;
	u64 rx_dropped;
	struct list_head map_list;
	/* Protects map_list */
	spinlock_t map_list_lock;
};

#ifdef CONFIG_XDP_SOCKETS

int xsk_generic_rcv(struct xdp_sock *xs, struct xdp_buff *xdp);
int __xsk_map_redirect(struct xdp_sock *xs, struct xdp_buff *xdp);
void __xsk_map_flush(void);

static inline struct xdp_sock *__xsk_map_lookup_elem(struct bpf_map *map,
						     u32 key)
{
	struct xsk_map *m = container_of(map, struct xsk_map, map);
	struct xdp_sock *xs;

	if (key >= map->max_entries)
		return NULL;

	xs = READ_ONCE(m->xsk_map[key]);
	return xs;
}

static inline u64 xsk_umem_extract_addr(u64 addr)
{
	return addr & XSK_UNALIGNED_BUF_ADDR_MASK;
}

static inline u64 xsk_umem_extract_offset(u64 addr)
{
	return addr >> XSK_UNALIGNED_BUF_OFFSET_SHIFT;
}

static inline u64 xsk_umem_add_offset_to_addr(u64 addr)
{
	return xsk_umem_extract_addr(addr) + xsk_umem_extract_offset(addr);
}

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

static inline struct xdp_sock *__xsk_map_lookup_elem(struct bpf_map *map,
						     u32 key)
{
	return NULL;
}

static inline u64 xsk_umem_extract_addr(u64 addr)
{
	return 0;
}

static inline u64 xsk_umem_extract_offset(u64 addr)
{
	return 0;
}

static inline u64 xsk_umem_add_offset_to_addr(u64 addr)
{
	return 0;
}

#endif /* CONFIG_XDP_SOCKETS */

#endif /* _LINUX_XDP_SOCK_H */
