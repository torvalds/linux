/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * if_xdp: XDP socket user-space interface
 * Copyright(c) 2018 Intel Corporation.
 *
 * Author(s): Björn Töpel <bjorn.topel@intel.com>
 *	      Magnus Karlsson <magnus.karlsson@intel.com>
 */

#ifndef _LINUX_IF_XDP_H
#define _LINUX_IF_XDP_H

#include <linux/types.h>

/* Options for the sxdp_flags field */
#define XDP_SHARED_UMEM	(1 << 0)
#define XDP_COPY	(1 << 1) /* Force copy-mode */
#define XDP_ZEROCOPY	(1 << 2) /* Force zero-copy mode */

struct sockaddr_xdp {
	__u16 sxdp_family;
	__u16 sxdp_flags;
	__u32 sxdp_ifindex;
	__u32 sxdp_queue_id;
	__u32 sxdp_shared_umem_fd;
};

struct xdp_ring_offset {
	__u64 producer;
	__u64 consumer;
	__u64 desc;
};

struct xdp_mmap_offsets {
	struct xdp_ring_offset rx;
	struct xdp_ring_offset tx;
	struct xdp_ring_offset fr; /* Fill */
	struct xdp_ring_offset cr; /* Completion */
};

/* XDP socket options */
#define XDP_MMAP_OFFSETS		1
#define XDP_RX_RING			2
#define XDP_TX_RING			3
#define XDP_UMEM_REG			4
#define XDP_UMEM_FILL_RING		5
#define XDP_UMEM_COMPLETION_RING	6
#define XDP_STATISTICS			7

struct xdp_umem_reg {
	__u64 addr; /* Start of packet data area */
	__u64 len; /* Length of packet data area */
	__u32 chunk_size;
	__u32 headroom;
};

struct xdp_statistics {
	__u64 rx_dropped; /* Dropped for reasons other than invalid desc */
	__u64 rx_invalid_descs; /* Dropped due to invalid descriptor */
	__u64 tx_invalid_descs; /* Dropped due to invalid descriptor */
};

/* Pgoff for mmaping the rings */
#define XDP_PGOFF_RX_RING			  0
#define XDP_PGOFF_TX_RING		 0x80000000
#define XDP_UMEM_PGOFF_FILL_RING	0x100000000ULL
#define XDP_UMEM_PGOFF_COMPLETION_RING	0x180000000ULL

/* Rx/Tx descriptor */
struct xdp_desc {
	__u64 addr;
	__u32 len;
	__u32 options;
};

/* UMEM descriptor is __u64 */

#endif /* _LINUX_IF_XDP_H */
