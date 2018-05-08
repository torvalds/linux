/* SPDX-License-Identifier: GPL-2.0
 * XDP user-space packet buffer
 * Copyright(c) 2018 Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#ifndef XDP_UMEM_PROPS_H_
#define XDP_UMEM_PROPS_H_

struct xdp_umem_props {
	u32 frame_size;
	u32 nframes;
};

#endif /* XDP_UMEM_PROPS_H_ */
