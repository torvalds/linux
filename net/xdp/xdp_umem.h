/* SPDX-License-Identifier: GPL-2.0 */
/* XDP user-space packet buffer
 * Copyright(c) 2018 Intel Corporation.
 */

#ifndef XDP_UMEM_H_
#define XDP_UMEM_H_

#include <net/xdp_sock.h>

static inline char *xdp_umem_get_data(struct xdp_umem *umem, u64 addr)
{
	return umem->pages[addr >> PAGE_SHIFT].addr + (addr & (PAGE_SIZE - 1));
}

static inline dma_addr_t xdp_umem_get_dma(struct xdp_umem *umem, u64 addr)
{
	return umem->pages[addr >> PAGE_SHIFT].dma + (addr & (PAGE_SIZE - 1));
}

int xdp_umem_assign_dev(struct xdp_umem *umem, struct net_device *dev,
			u32 queue_id, u16 flags);
bool xdp_umem_validate_queues(struct xdp_umem *umem);
void xdp_get_umem(struct xdp_umem *umem);
void xdp_put_umem(struct xdp_umem *umem);
void xdp_add_sk_umem(struct xdp_umem *umem, struct xdp_sock *xs);
void xdp_del_sk_umem(struct xdp_umem *umem, struct xdp_sock *xs);
struct xdp_umem *xdp_umem_create(struct xdp_umem_reg *mr);

#endif /* XDP_UMEM_H_ */
