/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * xdp_diag: interface for query/monitor XDP sockets
 * Copyright(c) 2019 Intel Corporation.
 */

#ifndef _LINUX_XDP_DIAG_H
#define _LINUX_XDP_DIAG_H

#include <linux/types.h>

struct xdp_diag_req {
	__u8	sdiag_family;
	__u8	sdiag_protocol;
	__u16	pad;
	__u32	xdiag_ino;
	__u32	xdiag_show;
	__u32	xdiag_cookie[2];
};

struct xdp_diag_msg {
	__u8	xdiag_family;
	__u8	xdiag_type;
	__u16	pad;
	__u32	xdiag_ino;
	__u32	xdiag_cookie[2];
};

#define XDP_SHOW_INFO		(1 << 0) /* Basic information */
#define XDP_SHOW_RING_CFG	(1 << 1)
#define XDP_SHOW_UMEM		(1 << 2)
#define XDP_SHOW_MEMINFO	(1 << 3)

enum {
	XDP_DIAG_NONE,
	XDP_DIAG_INFO,
	XDP_DIAG_UID,
	XDP_DIAG_RX_RING,
	XDP_DIAG_TX_RING,
	XDP_DIAG_UMEM,
	XDP_DIAG_UMEM_FILL_RING,
	XDP_DIAG_UMEM_COMPLETION_RING,
	XDP_DIAG_MEMINFO,
	__XDP_DIAG_MAX,
};

#define XDP_DIAG_MAX (__XDP_DIAG_MAX - 1)

struct xdp_diag_info {
	__u32	ifindex;
	__u32	queue_id;
};

struct xdp_diag_ring {
	__u32	entries; /*num descs */
};

#define XDP_DU_F_ZEROCOPY (1 << 0)

struct xdp_diag_umem {
	__u64	size;
	__u32	id;
	__u32	num_pages;
	__u32	chunk_size;
	__u32	headroom;
	__u32	ifindex;
	__u32	queue_id;
	__u32	flags;
	__u32	refs;
};

#endif /* _LINUX_XDP_DIAG_H */
