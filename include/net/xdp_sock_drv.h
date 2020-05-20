/* SPDX-License-Identifier: GPL-2.0 */
/* Interface for implementing AF_XDP zero-copy support in drivers.
 * Copyright(c) 2020 Intel Corporation.
 */

#ifndef _LINUX_XDP_SOCK_DRV_H
#define _LINUX_XDP_SOCK_DRV_H

#include <net/xdp_sock.h>

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

#endif /* CONFIG_XDP_SOCKETS */

#endif /* _LINUX_XDP_SOCK_DRV_H */
