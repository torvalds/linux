/* SPDX-License-Identifier: (GPL-2.0 WITH Linux-syscall-note) OR MIT */
/*
 * Header file for the io_uring zerocopy receive (zcrx) interface.
 *
 * Copyright (C) 2026 Pavel Begunkov
 * Copyright (C) 2026 David Wei
 * Copyright (C) Meta Platforms, Inc.
 */
#ifndef LINUX_IO_ZCRX_H
#define LINUX_IO_ZCRX_H

#include <linux/types.h>

/* Zero copy receive refill queue entry */
struct io_uring_zcrx_rqe {
	__u64	off;
	__u32	len;
	__u32	__pad;
};

struct io_uring_zcrx_cqe {
	__u64	off;
	__u64	__pad;
};

/* The bit from which area id is encoded into offsets */
#define IORING_ZCRX_AREA_SHIFT	48
#define IORING_ZCRX_AREA_MASK	(~(((__u64)1 << IORING_ZCRX_AREA_SHIFT) - 1))

struct io_uring_zcrx_offsets {
	__u32	head;
	__u32	tail;
	__u32	rqes;
	__u32	__resv2;
	__u64	__resv[2];
};

enum io_uring_zcrx_area_flags {
	IORING_ZCRX_AREA_DMABUF		= 1,
};

struct io_uring_zcrx_area_reg {
	__u64	addr;
	__u64	len;
	__u64	rq_area_token;
	__u32	flags;
	__u32	dmabuf_fd;
	__u64	__resv2[2];
};

enum zcrx_reg_flags {
	ZCRX_REG_IMPORT		= 1,

	/*
	 * Register a zcrx instance without a net device. All data will be
	 * copied. The refill queue entries might not be automatically
	 * consumed and need to be flushed, see ZCRX_CTRL_FLUSH_RQ.
	 */
	ZCRX_REG_NODEV		= 2,
};

enum zcrx_features {
	/*
	 * The user can ask for the desired rx page size by passing the
	 * value in struct io_uring_zcrx_ifq_reg::rx_buf_len.
	 */
	ZCRX_FEATURE_RX_PAGE_SIZE	= 1 << 0,
	ZCRX_FEATURE_NOTIFICATION	= 1 << 1,
};

enum zcrx_notification_type {
	ZCRX_NOTIF_NO_BUFFERS,
	ZCRX_NOTIF_COPY,

	__ZCRX_NOTIF_TYPE_LAST,
};

enum zcrx_notification_desc_flags {
	/* If set, stats_offset holds a valid offset to a notif_stats struct */
	ZCRX_NOTIF_DESC_FLAG_STATS = 1 << 0,
};

struct zcrx_notif_stats {
	__u64	copy_count;	/* cumulative copy-fallback CQEs */
	__u64	copy_bytes;	/* cumulative bytes copied */
};

struct zcrx_notification_desc {
	__u64	user_data;
	__u32	type_mask;
	__u32	flags; /* see enum zcrx_notification_desc_flags */
	__u64	stats_offset; /* offset from the beginning of refill ring region for stats */
	__u64	__resv2[9];
};

/*
 * Argument for IORING_REGISTER_ZCRX_IFQ
 */
struct io_uring_zcrx_ifq_reg {
	__u32	if_idx;
	__u32	if_rxq;
	__u32	rq_entries;
	__u32	flags;

	__u64	area_ptr; /* pointer to struct io_uring_zcrx_area_reg */
	__u64	region_ptr; /* struct io_uring_region_desc * */

	struct io_uring_zcrx_offsets offsets;
	__u32	zcrx_id;
	__u32	rx_buf_len;
	__u64	notif_desc; /* see struct zcrx_notification_desc */
	__u64	__resv[2];
};

enum zcrx_ctrl_op {
	ZCRX_CTRL_FLUSH_RQ,
	ZCRX_CTRL_EXPORT,
	ZCRX_CTRL_ARM_NOTIFICATION,

	__ZCRX_CTRL_LAST,
};

struct zcrx_ctrl_flush_rq {
	__u64		__resv[6];
};

struct zcrx_ctrl_export {
	__u32		zcrx_fd;
	__u32 		__resv1[11];
};

struct zcrx_ctrl_arm_notif {
	__u32		notif_type;
	__u32		__resv[11];
};

struct zcrx_ctrl {
	__u32	zcrx_id;
	__u32	op; /* see enum zcrx_ctrl_op */
	__u64	__resv[2];

	union {
		struct zcrx_ctrl_export		zc_export;
		struct zcrx_ctrl_flush_rq	zc_flush;
		struct zcrx_ctrl_arm_notif	zc_arm_notif;
	};
};

#endif /* LINUX_IO_ZCRX_H */
