/* SPDX-License-Identifier: GPL-2.0 */
/* XDP user-space packet buffer
 * Copyright(c) 2018 Intel Corporation.
 */

#ifndef XDP_UMEM_PROPS_H_
#define XDP_UMEM_PROPS_H_

struct xdp_umem_props {
	u64 chunk_mask;
	u64 size;
};

#endif /* XDP_UMEM_PROPS_H_ */
