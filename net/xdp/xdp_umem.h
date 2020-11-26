/* SPDX-License-Identifier: GPL-2.0 */
/* XDP user-space packet buffer
 * Copyright(c) 2018 Intel Corporation.
 */

#ifndef XDP_UMEM_H_
#define XDP_UMEM_H_

#include <net/xdp_sock_drv.h>

void xdp_get_umem(struct xdp_umem *umem);
void xdp_put_umem(struct xdp_umem *umem);
struct xdp_umem *xdp_umem_create(struct xdp_umem_reg *mr);

#endif /* XDP_UMEM_H_ */
