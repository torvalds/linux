/* SPDX-License-Identifier: GPL-2.0 */
/* XDP user-space packet buffer
 * Copyright(c) 2018 Intel Corporation.
 */

#ifndef XDP_UMEM_H_
#define XDP_UMEM_H_

#include <net/xdp_sock.h>

static inline char *xdp_umem_get_data(struct xdp_umem *umem, u64 addr)
{
	return page_address(umem->pgs[addr >> PAGE_SHIFT]) +
		(addr & (PAGE_SIZE - 1));
}

bool xdp_umem_validate_queues(struct xdp_umem *umem);
void xdp_get_umem(struct xdp_umem *umem);
void xdp_put_umem(struct xdp_umem *umem);
struct xdp_umem *xdp_umem_create(struct xdp_umem_reg *mr);

#endif /* XDP_UMEM_H_ */
