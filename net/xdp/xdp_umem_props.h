/* SPDX-License-Identifier: GPL-2.0 */
/* XDP user-space packet buffer
 * Copyright(c) 2018 Intel Corporation.
 */

#ifndef XDP_UMEM_PROPS_H_
#define XDP_UMEM_PROPS_H_

struct xdp_umem_props {
	u32 frame_size;
	u32 nframes;
};

#endif /* XDP_UMEM_PROPS_H_ */
