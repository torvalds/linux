/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note
 *
 * if_xdp: XDP socket user-space interface
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
 *
 * Author(s): Björn Töpel <bjorn.topel@intel.com>
 *	      Magnus Karlsson <magnus.karlsson@intel.com>
 */

#ifndef _LINUX_IF_XDP_H
#define _LINUX_IF_XDP_H

#include <linux/types.h>

/* Options for the sxdp_flags field */
#define XDP_SHARED_UMEM 1

struct sockaddr_xdp {
	__u16 sxdp_family;
	__u32 sxdp_ifindex;
	__u32 sxdp_queue_id;
	__u32 sxdp_shared_umem_fd;
	__u16 sxdp_flags;
};

/* XDP socket options */
#define XDP_RX_RING			1
#define XDP_UMEM_REG			3
#define XDP_UMEM_FILL_RING		4
#define XDP_UMEM_COMPLETION_RING	5

struct xdp_umem_reg {
	__u64 addr; /* Start of packet data area */
	__u64 len; /* Length of packet data area */
	__u32 frame_size; /* Frame size */
	__u32 frame_headroom; /* Frame head room */
};

/* Pgoff for mmaping the rings */
#define XDP_PGOFF_RX_RING			  0
#define XDP_UMEM_PGOFF_FILL_RING	0x100000000
#define XDP_UMEM_PGOFF_COMPLETION_RING	0x180000000

struct xdp_desc {
	__u32 idx;
	__u32 len;
	__u16 offset;
	__u8 flags;
	__u8 padding[5];
};

struct xdp_ring {
	__u32 producer __attribute__((aligned(64)));
	__u32 consumer __attribute__((aligned(64)));
};

/* Used for the RX and TX queues for packets */
struct xdp_rxtx_ring {
	struct xdp_ring ptrs;
	struct xdp_desc desc[0] __attribute__((aligned(64)));
};

/* Used for the fill and completion queues for buffers */
struct xdp_umem_ring {
	struct xdp_ring ptrs;
	__u32 desc[0] __attribute__((aligned(64)));
};

#endif /* _LINUX_IF_XDP_H */
