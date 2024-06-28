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
	u8 tx_metadata_len;
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

/*
 * AF_XDP TX metadata hooks for network devices.
 * The following hooks can be defined; unless noted otherwise, they are
 * optional and can be filled with a null pointer.
 *
 * void (*tmo_request_timestamp)(void *priv)
 *     Called when AF_XDP frame requested egress timestamp.
 *
 * u64 (*tmo_fill_timestamp)(void *priv)
 *     Called when AF_XDP frame, that had requested egress timestamp,
 *     received a completion. The hook needs to return the actual HW timestamp.
 *
 * void (*tmo_request_checksum)(u16 csum_start, u16 csum_offset, void *priv)
 *     Called when AF_XDP frame requested HW checksum offload. csum_start
 *     indicates position where checksumming should start.
 *     csum_offset indicates position where checksum should be stored.
 *
 */
struct xsk_tx_metadata_ops {
	void	(*tmo_request_timestamp)(void *priv);
	u64	(*tmo_fill_timestamp)(void *priv);
	void	(*tmo_request_checksum)(u16 csum_start, u16 csum_offset, void *priv);
};

#ifdef CONFIG_XDP_SOCKETS

int xsk_generic_rcv(struct xdp_sock *xs, struct xdp_buff *xdp);
int __xsk_map_redirect(struct xdp_sock *xs, struct xdp_buff *xdp);
void __xsk_map_flush(struct list_head *flush_list);

/**
 *  xsk_tx_metadata_to_compl - Save enough relevant metadata information
 *  to perform tx completion in the future.
 *  @meta: pointer to AF_XDP metadata area
 *  @compl: pointer to output struct xsk_tx_metadata_to_compl
 *
 *  This function should be called by the networking device when
 *  it prepares AF_XDP egress packet. The value of @compl should be stored
 *  and passed to xsk_tx_metadata_complete upon TX completion.
 */
static inline void xsk_tx_metadata_to_compl(struct xsk_tx_metadata *meta,
					    struct xsk_tx_metadata_compl *compl)
{
	if (!meta)
		return;

	if (meta->flags & XDP_TXMD_FLAGS_TIMESTAMP)
		compl->tx_timestamp = &meta->completion.tx_timestamp;
	else
		compl->tx_timestamp = NULL;
}

/**
 *  xsk_tx_metadata_request - Evaluate AF_XDP TX metadata at submission
 *  and call appropriate xsk_tx_metadata_ops operation.
 *  @meta: pointer to AF_XDP metadata area
 *  @ops: pointer to struct xsk_tx_metadata_ops
 *  @priv: pointer to driver-private aread
 *
 *  This function should be called by the networking device when
 *  it prepares AF_XDP egress packet.
 */
static inline void xsk_tx_metadata_request(const struct xsk_tx_metadata *meta,
					   const struct xsk_tx_metadata_ops *ops,
					   void *priv)
{
	if (!meta)
		return;

	if (ops->tmo_request_timestamp)
		if (meta->flags & XDP_TXMD_FLAGS_TIMESTAMP)
			ops->tmo_request_timestamp(priv);

	if (ops->tmo_request_checksum)
		if (meta->flags & XDP_TXMD_FLAGS_CHECKSUM)
			ops->tmo_request_checksum(meta->request.csum_start,
						  meta->request.csum_offset, priv);
}

/**
 *  xsk_tx_metadata_complete - Evaluate AF_XDP TX metadata at completion
 *  and call appropriate xsk_tx_metadata_ops operation.
 *  @compl: pointer to completion metadata produced from xsk_tx_metadata_to_compl
 *  @ops: pointer to struct xsk_tx_metadata_ops
 *  @priv: pointer to driver-private aread
 *
 *  This function should be called by the networking device upon
 *  AF_XDP egress completion.
 */
static inline void xsk_tx_metadata_complete(struct xsk_tx_metadata_compl *compl,
					    const struct xsk_tx_metadata_ops *ops,
					    void *priv)
{
	if (!compl)
		return;
	if (!compl->tx_timestamp)
		return;

	*compl->tx_timestamp = ops->tmo_fill_timestamp(priv);
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

static inline void __xsk_map_flush(struct list_head *flush_list)
{
}

static inline void xsk_tx_metadata_to_compl(struct xsk_tx_metadata *meta,
					    struct xsk_tx_metadata_compl *compl)
{
}

static inline void xsk_tx_metadata_request(struct xsk_tx_metadata *meta,
					   const struct xsk_tx_metadata_ops *ops,
					   void *priv)
{
}

static inline void xsk_tx_metadata_complete(struct xsk_tx_metadata_compl *compl,
					    const struct xsk_tx_metadata_ops *ops,
					    void *priv)
{
}

#endif /* CONFIG_XDP_SOCKETS */
#endif /* _LINUX_XDP_SOCK_H */
