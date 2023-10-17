/* SPDX-License-Identifier: GPL-2.0 */
/* Interface for implementing AF_XDP zero-copy support in drivers.
 * Copyright(c) 2020 Intel Corporation.
 */

#ifndef _LINUX_XDP_SOCK_DRV_H
#define _LINUX_XDP_SOCK_DRV_H

#include <net/xdp_sock.h>
#include <net/xsk_buff_pool.h>

#define XDP_UMEM_MIN_CHUNK_SHIFT 11
#define XDP_UMEM_MIN_CHUNK_SIZE (1 << XDP_UMEM_MIN_CHUNK_SHIFT)

#ifdef CONFIG_XDP_SOCKETS

void xsk_tx_completed(struct xsk_buff_pool *pool, u32 nb_entries);
bool xsk_tx_peek_desc(struct xsk_buff_pool *pool, struct xdp_desc *desc);
u32 xsk_tx_peek_release_desc_batch(struct xsk_buff_pool *pool, u32 max);
void xsk_tx_release(struct xsk_buff_pool *pool);
struct xsk_buff_pool *xsk_get_pool_from_qid(struct net_device *dev,
					    u16 queue_id);
void xsk_set_rx_need_wakeup(struct xsk_buff_pool *pool);
void xsk_set_tx_need_wakeup(struct xsk_buff_pool *pool);
void xsk_clear_rx_need_wakeup(struct xsk_buff_pool *pool);
void xsk_clear_tx_need_wakeup(struct xsk_buff_pool *pool);
bool xsk_uses_need_wakeup(struct xsk_buff_pool *pool);

static inline u32 xsk_pool_get_headroom(struct xsk_buff_pool *pool)
{
	return XDP_PACKET_HEADROOM + pool->headroom;
}

static inline u32 xsk_pool_get_chunk_size(struct xsk_buff_pool *pool)
{
	return pool->chunk_size;
}

static inline u32 xsk_pool_get_rx_frame_size(struct xsk_buff_pool *pool)
{
	return xsk_pool_get_chunk_size(pool) - xsk_pool_get_headroom(pool);
}

static inline void xsk_pool_set_rxq_info(struct xsk_buff_pool *pool,
					 struct xdp_rxq_info *rxq)
{
	xp_set_rxq_info(pool, rxq);
}

static inline unsigned int xsk_pool_get_napi_id(struct xsk_buff_pool *pool)
{
#ifdef CONFIG_NET_RX_BUSY_POLL
	return pool->heads[0].xdp.rxq->napi_id;
#else
	return 0;
#endif
}

static inline void xsk_pool_dma_unmap(struct xsk_buff_pool *pool,
				      unsigned long attrs)
{
	xp_dma_unmap(pool, attrs);
}

static inline int xsk_pool_dma_map(struct xsk_buff_pool *pool,
				   struct device *dev, unsigned long attrs)
{
	struct xdp_umem *umem = pool->umem;

	return xp_dma_map(pool, dev, attrs, umem->pgs, umem->npgs);
}

static inline dma_addr_t xsk_buff_xdp_get_dma(struct xdp_buff *xdp)
{
	struct xdp_buff_xsk *xskb = container_of(xdp, struct xdp_buff_xsk, xdp);

	return xp_get_dma(xskb);
}

static inline dma_addr_t xsk_buff_xdp_get_frame_dma(struct xdp_buff *xdp)
{
	struct xdp_buff_xsk *xskb = container_of(xdp, struct xdp_buff_xsk, xdp);

	return xp_get_frame_dma(xskb);
}

static inline struct xdp_buff *xsk_buff_alloc(struct xsk_buff_pool *pool)
{
	return xp_alloc(pool);
}

static inline bool xsk_is_eop_desc(struct xdp_desc *desc)
{
	return !xp_mb_desc(desc);
}

/* Returns as many entries as possible up to max. 0 <= N <= max. */
static inline u32 xsk_buff_alloc_batch(struct xsk_buff_pool *pool, struct xdp_buff **xdp, u32 max)
{
	return xp_alloc_batch(pool, xdp, max);
}

static inline bool xsk_buff_can_alloc(struct xsk_buff_pool *pool, u32 count)
{
	return xp_can_alloc(pool, count);
}

static inline void xsk_buff_free(struct xdp_buff *xdp)
{
	struct xdp_buff_xsk *xskb = container_of(xdp, struct xdp_buff_xsk, xdp);
	struct list_head *xskb_list = &xskb->pool->xskb_list;
	struct xdp_buff_xsk *pos, *tmp;

	if (likely(!xdp_buff_has_frags(xdp)))
		goto out;

	list_for_each_entry_safe(pos, tmp, xskb_list, xskb_list_node) {
		list_del(&pos->xskb_list_node);
		xp_free(pos);
	}

	xdp_get_shared_info_from_buff(xdp)->nr_frags = 0;
out:
	xp_free(xskb);
}

static inline void xsk_buff_add_frag(struct xdp_buff *xdp)
{
	struct xdp_buff_xsk *frag = container_of(xdp, struct xdp_buff_xsk, xdp);

	list_add_tail(&frag->xskb_list_node, &frag->pool->xskb_list);
}

static inline struct xdp_buff *xsk_buff_get_frag(struct xdp_buff *first)
{
	struct xdp_buff_xsk *xskb = container_of(first, struct xdp_buff_xsk, xdp);
	struct xdp_buff *ret = NULL;
	struct xdp_buff_xsk *frag;

	frag = list_first_entry_or_null(&xskb->pool->xskb_list,
					struct xdp_buff_xsk, xskb_list_node);
	if (frag) {
		list_del(&frag->xskb_list_node);
		ret = &frag->xdp;
	}

	return ret;
}

static inline void xsk_buff_set_size(struct xdp_buff *xdp, u32 size)
{
	xdp->data = xdp->data_hard_start + XDP_PACKET_HEADROOM;
	xdp->data_meta = xdp->data;
	xdp->data_end = xdp->data + size;
}

static inline dma_addr_t xsk_buff_raw_get_dma(struct xsk_buff_pool *pool,
					      u64 addr)
{
	return xp_raw_get_dma(pool, addr);
}

static inline void *xsk_buff_raw_get_data(struct xsk_buff_pool *pool, u64 addr)
{
	return xp_raw_get_data(pool, addr);
}

static inline void xsk_buff_dma_sync_for_cpu(struct xdp_buff *xdp, struct xsk_buff_pool *pool)
{
	struct xdp_buff_xsk *xskb = container_of(xdp, struct xdp_buff_xsk, xdp);

	if (!pool->dma_need_sync)
		return;

	xp_dma_sync_for_cpu(xskb);
}

static inline void xsk_buff_raw_dma_sync_for_device(struct xsk_buff_pool *pool,
						    dma_addr_t dma,
						    size_t size)
{
	xp_dma_sync_for_device(pool, dma, size);
}

#else

static inline void xsk_tx_completed(struct xsk_buff_pool *pool, u32 nb_entries)
{
}

static inline bool xsk_tx_peek_desc(struct xsk_buff_pool *pool,
				    struct xdp_desc *desc)
{
	return false;
}

static inline u32 xsk_tx_peek_release_desc_batch(struct xsk_buff_pool *pool, u32 max)
{
	return 0;
}

static inline void xsk_tx_release(struct xsk_buff_pool *pool)
{
}

static inline struct xsk_buff_pool *
xsk_get_pool_from_qid(struct net_device *dev, u16 queue_id)
{
	return NULL;
}

static inline void xsk_set_rx_need_wakeup(struct xsk_buff_pool *pool)
{
}

static inline void xsk_set_tx_need_wakeup(struct xsk_buff_pool *pool)
{
}

static inline void xsk_clear_rx_need_wakeup(struct xsk_buff_pool *pool)
{
}

static inline void xsk_clear_tx_need_wakeup(struct xsk_buff_pool *pool)
{
}

static inline bool xsk_uses_need_wakeup(struct xsk_buff_pool *pool)
{
	return false;
}

static inline u32 xsk_pool_get_headroom(struct xsk_buff_pool *pool)
{
	return 0;
}

static inline u32 xsk_pool_get_chunk_size(struct xsk_buff_pool *pool)
{
	return 0;
}

static inline u32 xsk_pool_get_rx_frame_size(struct xsk_buff_pool *pool)
{
	return 0;
}

static inline void xsk_pool_set_rxq_info(struct xsk_buff_pool *pool,
					 struct xdp_rxq_info *rxq)
{
}

static inline unsigned int xsk_pool_get_napi_id(struct xsk_buff_pool *pool)
{
	return 0;
}

static inline void xsk_pool_dma_unmap(struct xsk_buff_pool *pool,
				      unsigned long attrs)
{
}

static inline int xsk_pool_dma_map(struct xsk_buff_pool *pool,
				   struct device *dev, unsigned long attrs)
{
	return 0;
}

static inline dma_addr_t xsk_buff_xdp_get_dma(struct xdp_buff *xdp)
{
	return 0;
}

static inline dma_addr_t xsk_buff_xdp_get_frame_dma(struct xdp_buff *xdp)
{
	return 0;
}

static inline struct xdp_buff *xsk_buff_alloc(struct xsk_buff_pool *pool)
{
	return NULL;
}

static inline bool xsk_is_eop_desc(struct xdp_desc *desc)
{
	return false;
}

static inline u32 xsk_buff_alloc_batch(struct xsk_buff_pool *pool, struct xdp_buff **xdp, u32 max)
{
	return 0;
}

static inline bool xsk_buff_can_alloc(struct xsk_buff_pool *pool, u32 count)
{
	return false;
}

static inline void xsk_buff_free(struct xdp_buff *xdp)
{
}

static inline void xsk_buff_add_frag(struct xdp_buff *xdp)
{
}

static inline struct xdp_buff *xsk_buff_get_frag(struct xdp_buff *first)
{
	return NULL;
}

static inline void xsk_buff_set_size(struct xdp_buff *xdp, u32 size)
{
}

static inline dma_addr_t xsk_buff_raw_get_dma(struct xsk_buff_pool *pool,
					      u64 addr)
{
	return 0;
}

static inline void *xsk_buff_raw_get_data(struct xsk_buff_pool *pool, u64 addr)
{
	return NULL;
}

static inline void xsk_buff_dma_sync_for_cpu(struct xdp_buff *xdp, struct xsk_buff_pool *pool)
{
}

static inline void xsk_buff_raw_dma_sync_for_device(struct xsk_buff_pool *pool,
						    dma_addr_t dma,
						    size_t size)
{
}

#endif /* CONFIG_XDP_SOCKETS */

#endif /* _LINUX_XDP_SOCK_DRV_H */
