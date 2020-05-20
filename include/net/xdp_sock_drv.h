/* SPDX-License-Identifier: GPL-2.0 */
/* Interface for implementing AF_XDP zero-copy support in drivers.
 * Copyright(c) 2020 Intel Corporation.
 */

#ifndef _LINUX_XDP_SOCK_DRV_H
#define _LINUX_XDP_SOCK_DRV_H

#include <net/xdp_sock.h>
#include <net/xsk_buff_pool.h>

#ifdef CONFIG_XDP_SOCKETS

bool xsk_umem_has_addrs(struct xdp_umem *umem, u32 cnt);
bool xsk_umem_peek_addr(struct xdp_umem *umem, u64 *addr);
void xsk_umem_release_addr(struct xdp_umem *umem);
void xsk_umem_complete_tx(struct xdp_umem *umem, u32 nb_entries);
bool xsk_umem_consume_tx(struct xdp_umem *umem, struct xdp_desc *desc);
void xsk_umem_consume_tx_done(struct xdp_umem *umem);
struct xdp_umem_fq_reuse *xsk_reuseq_prepare(u32 nentries);
struct xdp_umem_fq_reuse *xsk_reuseq_swap(struct xdp_umem *umem,
					  struct xdp_umem_fq_reuse *newq);
void xsk_reuseq_free(struct xdp_umem_fq_reuse *rq);
struct xdp_umem *xdp_get_umem_from_qid(struct net_device *dev, u16 queue_id);
void xsk_set_rx_need_wakeup(struct xdp_umem *umem);
void xsk_set_tx_need_wakeup(struct xdp_umem *umem);
void xsk_clear_rx_need_wakeup(struct xdp_umem *umem);
void xsk_clear_tx_need_wakeup(struct xdp_umem *umem);
bool xsk_umem_uses_need_wakeup(struct xdp_umem *umem);

static inline char *xdp_umem_get_data(struct xdp_umem *umem, u64 addr)
{
	unsigned long page_addr;

	addr = xsk_umem_add_offset_to_addr(addr);
	page_addr = (unsigned long)umem->pages[addr >> PAGE_SHIFT].addr;

	return (char *)(page_addr & PAGE_MASK) + (addr & ~PAGE_MASK);
}

static inline dma_addr_t xdp_umem_get_dma(struct xdp_umem *umem, u64 addr)
{
	addr = xsk_umem_add_offset_to_addr(addr);

	return umem->pages[addr >> PAGE_SHIFT].dma + (addr & ~PAGE_MASK);
}

/* Reuse-queue aware version of FILL queue helpers */
static inline bool xsk_umem_has_addrs_rq(struct xdp_umem *umem, u32 cnt)
{
	struct xdp_umem_fq_reuse *rq = umem->fq_reuse;

	if (rq->length >= cnt)
		return true;

	return xsk_umem_has_addrs(umem, cnt - rq->length);
}

static inline bool xsk_umem_peek_addr_rq(struct xdp_umem *umem, u64 *addr)
{
	struct xdp_umem_fq_reuse *rq = umem->fq_reuse;

	if (!rq->length)
		return xsk_umem_peek_addr(umem, addr);

	*addr = rq->handles[rq->length - 1];
	return addr;
}

static inline void xsk_umem_release_addr_rq(struct xdp_umem *umem)
{
	struct xdp_umem_fq_reuse *rq = umem->fq_reuse;

	if (!rq->length)
		xsk_umem_release_addr(umem);
	else
		rq->length--;
}

static inline void xsk_umem_fq_reuse(struct xdp_umem *umem, u64 addr)
{
	struct xdp_umem_fq_reuse *rq = umem->fq_reuse;

	rq->handles[rq->length++] = addr;
}

/* Handle the offset appropriately depending on aligned or unaligned mode.
 * For unaligned mode, we store the offset in the upper 16-bits of the address.
 * For aligned mode, we simply add the offset to the address.
 */
static inline u64 xsk_umem_adjust_offset(struct xdp_umem *umem, u64 address,
					 u64 offset)
{
	if (umem->flags & XDP_UMEM_UNALIGNED_CHUNK_FLAG)
		return address + (offset << XSK_UNALIGNED_BUF_OFFSET_SHIFT);
	else
		return address + offset;
}

static inline u32 xsk_umem_xdp_frame_sz(struct xdp_umem *umem)
{
	return umem->chunk_size_nohr;
}

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

static inline bool xsk_umem_has_addrs(struct xdp_umem *umem, u32 cnt)
{
	return false;
}

static inline u64 *xsk_umem_peek_addr(struct xdp_umem *umem, u64 *addr)
{
	return NULL;
}

static inline void xsk_umem_release_addr(struct xdp_umem *umem)
{
}

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

static inline struct xdp_umem_fq_reuse *xsk_reuseq_prepare(u32 nentries)
{
	return NULL;
}

static inline struct xdp_umem_fq_reuse *xsk_reuseq_swap(
	struct xdp_umem *umem, struct xdp_umem_fq_reuse *newq)
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

static inline void xsk_umem_release_addr_rq(struct xdp_umem *umem)
{
}

static inline void xsk_umem_fq_reuse(struct xdp_umem *umem, u64 addr)
{
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

static inline u64 xsk_umem_adjust_offset(struct xdp_umem *umem, u64 handle,
					 u64 offset)
{
	return 0;
}

static inline u32 xsk_umem_xdp_frame_sz(struct xdp_umem *umem)
{
	return 0;
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
