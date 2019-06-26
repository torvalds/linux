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
	unsigned long address;
	refcount_t users;
	struct work_struct work;
	struct page **pgs;
	u32 npgs;
	int id;
	struct net_device *dev;
	struct xdp_umem_fq_reuse *fq_reuse;
	u16 queue_id;
	bool zc;
	spinlock_t xsk_list_lock;
	struct list_head xsk_list;
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
	/* Protects multiple processes in the control path */
	struct mutex mutex;
	struct xsk_queue *tx ____cacheline_aligned_in_smp;
	struct list_head list;
	/* Mutual exclusion of NAPI TX thread and sendmsg error paths
	 * in the SKB destructor callback.
	 */
	spinlock_t tx_completion_lock;
	u64 rx_dropped;
};

struct xdp_buff;
#ifdef CONFIG_XDP_SOCKETS
int xsk_generic_rcv(struct xdp_sock *xs, struct xdp_buff *xdp);
int xsk_rcv(struct xdp_sock *xs, struct xdp_buff *xdp);
void xsk_flush(struct xdp_sock *xs);
bool xsk_is_setup_for_bpf_map(struct xdp_sock *xs);
/* Used from netdev driver */
bool xsk_umem_has_addrs(struct xdp_umem *umem, u32 cnt);
u64 *xsk_umem_peek_addr(struct xdp_umem *umem, u64 *addr);
void xsk_umem_discard_addr(struct xdp_umem *umem);
void xsk_umem_complete_tx(struct xdp_umem *umem, u32 nb_entries);
bool xsk_umem_consume_tx(struct xdp_umem *umem, dma_addr_t *dma, u32 *len);
void xsk_umem_consume_tx_done(struct xdp_umem *umem);
struct xdp_umem_fq_reuse *xsk_reuseq_prepare(u32 nentries);
struct xdp_umem_fq_reuse *xsk_reuseq_swap(struct xdp_umem *umem,
					  struct xdp_umem_fq_reuse *newq);
void xsk_reuseq_free(struct xdp_umem_fq_reuse *rq);
struct xdp_umem *xdp_get_umem_from_qid(struct net_device *dev, u16 queue_id);

static inline char *xdp_umem_get_data(struct xdp_umem *umem, u64 addr)
{
	return umem->pages[addr >> PAGE_SHIFT].addr + (addr & (PAGE_SIZE - 1));
}

static inline dma_addr_t xdp_umem_get_dma(struct xdp_umem *umem, u64 addr)
{
	return umem->pages[addr >> PAGE_SHIFT].dma + (addr & (PAGE_SIZE - 1));
}

/* Reuse-queue aware version of FILL queue helpers */
static inline bool xsk_umem_has_addrs_rq(struct xdp_umem *umem, u32 cnt)
{
	struct xdp_umem_fq_reuse *rq = umem->fq_reuse;

	if (rq->length >= cnt)
		return true;

	return xsk_umem_has_addrs(umem, cnt - rq->length);
}

static inline u64 *xsk_umem_peek_addr_rq(struct xdp_umem *umem, u64 *addr)
{
	struct xdp_umem_fq_reuse *rq = umem->fq_reuse;

	if (!rq->length)
		return xsk_umem_peek_addr(umem, addr);

	*addr = rq->handles[rq->length - 1];
	return addr;
}

static inline void xsk_umem_discard_addr_rq(struct xdp_umem *umem)
{
	struct xdp_umem_fq_reuse *rq = umem->fq_reuse;

	if (!rq->length)
		xsk_umem_discard_addr(umem);
	else
		rq->length--;
}

static inline void xsk_umem_fq_reuse(struct xdp_umem *umem, u64 addr)
{
	struct xdp_umem_fq_reuse *rq = umem->fq_reuse;

	rq->handles[rq->length++] = addr;
}
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

static inline bool xsk_umem_has_addrs(struct xdp_umem *umem, u32 cnt)
{
	return false;
}

static inline u64 *xsk_umem_peek_addr(struct xdp_umem *umem, u64 *addr)
{
	return NULL;
}

static inline void xsk_umem_discard_addr(struct xdp_umem *umem)
{
}

static inline void xsk_umem_complete_tx(struct xdp_umem *umem, u32 nb_entries)
{
}

static inline bool xsk_umem_consume_tx(struct xdp_umem *umem, dma_addr_t *dma,
				       u32 *len)
{
	return false;
}

static inline void xsk_umem_consume_tx_done(struct xdp_umem *umem)
{
}

static inline struct xdp_umem_fq_reuse *xsk_reuseq_prepare(u32 nentries)
{
	return NULL;
}

static inline struct xdp_umem_fq_reuse *xsk_reuseq_swap(
	struct xdp_umem *umem,
	struct xdp_umem_fq_reuse *newq)
{
	return NULL;
}
static inline void xsk_reuseq_free(struct xdp_umem_fq_reuse *rq)
{
}

static inline struct xdp_umem *xdp_get_umem_from_qid(struct net_device *dev,
						     u16 queue_id)
{
	return NULL;
}

static inline char *xdp_umem_get_data(struct xdp_umem *umem, u64 addr)
{
	return NULL;
}

static inline dma_addr_t xdp_umem_get_dma(struct xdp_umem *umem, u64 addr)
{
	return 0;
}

static inline bool xsk_umem_has_addrs_rq(struct xdp_umem *umem, u32 cnt)
{
	return false;
}

static inline u64 *xsk_umem_peek_addr_rq(struct xdp_umem *umem, u64 *addr)
{
	return NULL;
}

static inline void xsk_umem_discard_addr_rq(struct xdp_umem *umem)
{
}

static inline void xsk_umem_fq_reuse(struct xdp_umem *umem, u64 addr)
{
}

#endif /* CONFIG_XDP_SOCKETS */

#endif /* _LINUX_XDP_SOCK_H */
