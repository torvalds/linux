/* SPDX-License-Identifier: GPL-2.0 */
/* Interface for implementing AF_XDP zero-copy support in drivers.
 * Copyright(c) 2020 Intel Corporation.
 */

#ifndef _LINUX_XDP_SOCK_DRV_H
#define _LINUX_XDP_SOCK_DRV_H

#include <net/xdp_sock.h>
#include <net/xsk_buff_pool.h>

#ifdef CONFIG_XDP_SOCKETS

void xsk_umem_complete_tx(struct xdp_umem *umem, u32 nb_entries);
bool xsk_umem_consume_tx(struct xdp_umem *umem, struct xdp_desc *desc);
void xsk_umem_consume_tx_done(struct xdp_umem *umem);
struct xdp_umem *xdp_get_umem_from_qid(struct net_device *dev, u16 queue_id);
void xsk_set_rx_need_wakeup(struct xdp_umem *umem);
void xsk_set_tx_need_wakeup(struct xdp_umem *umem);
void xsk_clear_rx_need_wakeup(struct xdp_umem *umem);
void xsk_clear_tx_need_wakeup(struct xdp_umem *umem);
bool xsk_umem_uses_need_wakeup(struct xdp_umem *umem);

static inline u32 xsk_umem_get_headroom(struct xdp_umem *umem)
{
	return XDP_PACKET_HEADROOM + umem->headroom;
}

static inline u32 xsk_umem_get_chunk_size(struct xdp_umem *umem)
{
	return umem->chunk_size;
}

static inline u32 xsk_umem_get_rx_frame_size(struct xdp_umem *umem)
{
	return xsk_umem_get_chunk_size(umem) - xsk_umem_get_headroom(umem);
}

static inline void xsk_buff_set_rxq_info(struct xdp_umem *umem,
					 struct xdp_rxq_info *rxq)
{
	xp_set_rxq_info(umem->pool, rxq);
}

static inline void xsk_buff_dma_unmap(struct xdp_umem *umem,
				      unsigned long attrs)
{
	xp_dma_unmap(umem->pool, attrs);
}

static inline int xsk_buff_dma_map(struct xdp_umem *umem, struct device *dev,
				   unsigned long attrs)
{
	return xp_dma_map(umem->pool, dev, attrs, umem->pgs, umem->npgs);
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

static inline struct xdp_buff *xsk_buff_alloc(struct xdp_umem *umem)
{
	return xp_alloc(umem->pool);
}

static inline bool xsk_buff_can_alloc(struct xdp_umem *umem, u32 count)
{
	return xp_can_alloc(umem->pool, count);
}

static inline void xsk_buff_free(struct xdp_buff *xdp)
{
	struct xdp_buff_xsk *xskb = container_of(xdp, struct xdp_buff_xsk, xdp);

	xp_free(xskb);
}

static inline dma_addr_t xsk_buff_raw_get_dma(struct xdp_umem *umem, u64 addr)
{
	return xp_raw_get_dma(umem->pool, addr);
}

static inline void *xsk_buff_raw_get_data(struct xdp_umem *umem, u64 addr)
{
	return xp_raw_get_data(umem->pool, addr);
}

static inline void xsk_buff_dma_sync_for_cpu(struct xdp_buff *xdp)
{
	struct xdp_buff_xsk *xskb = container_of(xdp, struct xdp_buff_xsk, xdp);

	xp_dma_sync_for_cpu(xskb);
}

static inline void xsk_buff_raw_dma_sync_for_device(struct xdp_umem *umem,
						    dma_addr_t dma,
						    size_t size)
{
	xp_dma_sync_for_device(umem->pool, dma, size);
}

#else

static inline void xsk_umem_complete_tx(struct xdp_umem *umem, u32 nb_entries)
{
}

static inline bool xsk_umem_consume_tx(struct xdp_umem *umem,
				       struct xdp_desc *desc)
{
	return false;
}

static inline void xsk_umem_consume_tx_done(struct xdp_umem *umem)
{
}

static inline struct xdp_umem *xdp_get_umem_from_qid(struct net_device *dev,
						     u16 queue_id)
{
	return NULL;
}

static inline void xsk_set_rx_need_wakeup(struct xdp_umem *umem)
{
}

static inline void xsk_set_tx_need_wakeup(struct xdp_umem *umem)
{
}

static inline void xsk_clear_rx_need_wakeup(struct xdp_umem *umem)
{
}

static inline void xsk_clear_tx_need_wakeup(struct xdp_umem *umem)
{
}

static inline bool xsk_umem_uses_need_wakeup(struct xdp_umem *umem)
{
	return false;
}

static inline u32 xsk_umem_get_headroom(struct xdp_umem *umem)
{
	return 0;
}

static inline u32 xsk_umem_get_chunk_size(struct xdp_umem *umem)
{
	return 0;
}

static inline u32 xsk_umem_get_rx_frame_size(struct xdp_umem *umem)
{
	return 0;
}

static inline void xsk_buff_set_rxq_info(struct xdp_umem *umem,
					 struct xdp_rxq_info *rxq)
{
}

static inline void xsk_buff_dma_unmap(struct xdp_umem *umem,
				      unsigned long attrs)
{
}

static inline int xsk_buff_dma_map(struct xdp_umem *umem, struct device *dev,
				   unsigned long attrs)
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

static inline struct xdp_buff *xsk_buff_alloc(struct xdp_umem *umem)
{
	return NULL;
}

static inline bool xsk_buff_can_alloc(struct xdp_umem *umem, u32 count)
{
	return false;
}

static inline void xsk_buff_free(struct xdp_buff *xdp)
{
}

static inline dma_addr_t xsk_buff_raw_get_dma(struct xdp_umem *umem, u64 addr)
{
	return 0;
}

static inline void *xsk_buff_raw_get_data(struct xdp_umem *umem, u64 addr)
{
	return NULL;
}

static inline void xsk_buff_dma_sync_for_cpu(struct xdp_buff *xdp)
{
}

static inline void xsk_buff_raw_dma_sync_for_device(struct xdp_umem *umem,
						    dma_addr_t dma,
						    size_t size)
{
}

#endif /* CONFIG_XDP_SOCKETS */

#endif /* _LINUX_XDP_SOCK_DRV_H */
