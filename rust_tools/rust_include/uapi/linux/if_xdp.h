/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * if_xdp: XDP socket user-space interface
 * Copyright(c) 2018 Intel Corporation.
 *
 * Author(s): Björn Töpel <bjorn.topel@intel.com>
 *	      Magnus Karlsson <magnus.karlsson@intel.com>
 */

#ifndef _UAPI_LINUX_IF_XDP_H
#define _UAPI_LINUX_IF_XDP_H

#include <linux/types.h>

/* Options for the sxdp_flags field */
#define XDP_SHARED_UMEM	(1 << 0)
#define XDP_COPY	(1 << 1) /* Force copy-mode */
#define XDP_ZEROCOPY	(1 << 2) /* Force zero-copy mode */
/* If this option is set, the driver might go sleep and in that case
 * the XDP_RING_NEED_WAKEUP flag in the fill and/or Tx rings will be
 * set. If it is set, the application need to explicitly wake up the
 * driver with a poll() (Rx and Tx) or sendto() (Tx only). If you are
 * running the driver and the application on the same core, you should
 * use this option so that the kernel will yield to the user space
 * application.
 */
#define XDP_USE_NEED_WAKEUP (1 << 3)
/* By setting this option, userspace application indicates that it can
 * handle multiple descriptors per packet thus enabling AF_XDP to split
 * multi-buffer XDP frames into multiple Rx descriptors. Without this set
 * such frames will be dropped.
 */
#define XDP_USE_SG	(1 << 4)

/* Flags for xsk_umem_config flags */
#define XDP_UMEM_UNALIGNED_CHUNK_FLAG	(1 << 0)

/* Force checksum calculation in software. Can be used for testing or
 * working around potential HW issues. This option causes performance
 * degradation and only works in XDP_COPY mode.
 */
#define XDP_UMEM_TX_SW_CSUM		(1 << 1)

/* Request to reserve tx_metadata_len bytes of per-chunk metadata.
 */
#define XDP_UMEM_TX_METADATA_LEN	(1 << 2)

struct sockaddr_xdp {
	__u16 sxdp_family;
	__u16 sxdp_flags;
	__u32 sxdp_ifindex;
	__u32 sxdp_queue_id;
	__u32 sxdp_shared_umem_fd;
};

/* XDP_RING flags */
#define XDP_RING_NEED_WAKEUP (1 << 0)

struct xdp_ring_offset {
	__u64 producer;
	__u64 consumer;
	__u64 desc;
	__u64 flags;
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
#define XDP_OPTIONS			8
#define XDP_MAX_TX_SKB_BUDGET		9

struct xdp_umem_reg {
	__u64 addr; /* Start of packet data area */
	__u64 len; /* Length of packet data area */
	__u32 chunk_size;
	__u32 headroom;
	__u32 flags;
	__u32 tx_metadata_len;
};

struct xdp_statistics {
	__u64 rx_dropped; /* Dropped for other reasons */
	__u64 rx_invalid_descs; /* Dropped due to invalid descriptor */
	__u64 tx_invalid_descs; /* Dropped due to invalid descriptor */
	__u64 rx_ring_full; /* Dropped due to rx ring being full */
	__u64 rx_fill_ring_empty_descs; /* Failed to retrieve item from fill ring */
	__u64 tx_ring_empty_descs; /* Failed to retrieve item from tx ring */
};

struct xdp_options {
	__u32 flags;
};

/* Flags for the flags field of struct xdp_options */
#define XDP_OPTIONS_ZEROCOPY (1 << 0)

/* Pgoff for mmaping the rings */
#define XDP_PGOFF_RX_RING			  0
#define XDP_PGOFF_TX_RING		 0x80000000
#define XDP_UMEM_PGOFF_FILL_RING	0x100000000ULL
#define XDP_UMEM_PGOFF_COMPLETION_RING	0x180000000ULL

/* Masks for unaligned chunks mode */
#define XSK_UNALIGNED_BUF_OFFSET_SHIFT 48
#define XSK_UNALIGNED_BUF_ADDR_MASK \
	((1ULL << XSK_UNALIGNED_BUF_OFFSET_SHIFT) - 1)

/* Request transmit timestamp. Upon completion, put it into tx_timestamp
 * field of struct xsk_tx_metadata.
 */
#define XDP_TXMD_FLAGS_TIMESTAMP		(1 << 0)

/* Request transmit checksum offload. Checksum start position and offset
 * are communicated via csum_start and csum_offset fields of struct
 * xsk_tx_metadata.
 */
#define XDP_TXMD_FLAGS_CHECKSUM			(1 << 1)

/* Request launch time hardware offload. The device will schedule the packet for
 * transmission at a pre-determined time called launch time. The value of
 * launch time is communicated via launch_time field of struct xsk_tx_metadata.
 */
#define XDP_TXMD_FLAGS_LAUNCH_TIME		(1 << 2)

/* AF_XDP offloads request. 'request' union member is consumed by the driver
 * when the packet is being transmitted. 'completion' union member is
 * filled by the driver when the transmit completion arrives.
 */
struct xsk_tx_metadata {
	__u64 flags;

	union {
		struct {
			/* XDP_TXMD_FLAGS_CHECKSUM */

			/* Offset from desc->addr where checksumming should start. */
			__u16 csum_start;
			/* Offset from csum_start where checksum should be stored. */
			__u16 csum_offset;

			/* XDP_TXMD_FLAGS_LAUNCH_TIME */
			/* Launch time in nanosecond against the PTP HW Clock */
			__u64 launch_time;
		} request;

		struct {
			/* XDP_TXMD_FLAGS_TIMESTAMP */
			__u64 tx_timestamp;
		} completion;
	};
};

/* Rx/Tx descriptor */
struct xdp_desc {
	__u64 addr;
	__u32 len;
	__u32 options;
};

/* UMEM descriptor is __u64 */

/* Flag indicating that the packet continues with the buffer pointed out by the
 * next frame in the ring. The end of the packet is signalled by setting this
 * bit to zero. For single buffer packets, every descriptor has 'options' set
 * to 0 and this maintains backward compatibility.
 */
#define XDP_PKT_CONTD (1 << 0)

/* TX packet carries valid metadata. */
#define XDP_TX_METADATA (1 << 1)

#endif /* _UAPI_LINUX_IF_XDP_H */
