/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (C) 2024-2025 Intel Corporation */

#ifndef __LIBETH_TYPES_H
#define __LIBETH_TYPES_H

#include <linux/workqueue.h>

/* Stats */

/**
 * struct libeth_rq_napi_stats - "hot" counters to update in Rx polling loop
 * @packets: received frames counter
 * @bytes: sum of bytes of received frames above
 * @fragments: sum of fragments of received S/G frames
 * @hsplit: number of frames the device performed the header split for
 * @raw: alias to access all the fields as an array
 */
struct libeth_rq_napi_stats {
	union {
		struct {
							u32 packets;
							u32 bytes;
							u32 fragments;
							u32 hsplit;
		};
		DECLARE_FLEX_ARRAY(u32, raw);
	};
};

/**
 * struct libeth_sq_napi_stats - "hot" counters to update in Tx completion loop
 * @packets: completed frames counter
 * @bytes: sum of bytes of completed frames above
 * @raw: alias to access all the fields as an array
 */
struct libeth_sq_napi_stats {
	union {
		struct {
							u32 packets;
							u32 bytes;
		};
		DECLARE_FLEX_ARRAY(u32, raw);
	};
};

/**
 * struct libeth_xdpsq_napi_stats - "hot" counters to update in XDP Tx
 *				    completion loop
 * @packets: completed frames counter
 * @bytes: sum of bytes of completed frames above
 * @fragments: sum of fragments of completed S/G frames
 * @raw: alias to access all the fields as an array
 */
struct libeth_xdpsq_napi_stats {
	union {
		struct {
							u32 packets;
							u32 bytes;
							u32 fragments;
		};
		DECLARE_FLEX_ARRAY(u32, raw);
	};
};

/* XDP */

/*
 * The following structures should be embedded into driver's queue structure
 * and passed to the libeth_xdp helpers, never used directly.
 */

/* XDPSQ sharing */

/**
 * struct libeth_xdpsq_lock - locking primitive for sharing XDPSQs
 * @lock: spinlock for locking the queue
 * @share: whether this particular queue is shared
 */
struct libeth_xdpsq_lock {
	spinlock_t			lock;
	bool				share;
};

/* XDPSQ clean-up timers */

/**
 * struct libeth_xdpsq_timer - timer for cleaning up XDPSQs w/o interrupts
 * @xdpsq: queue this timer belongs to
 * @lock: lock for the queue
 * @dwork: work performing cleanups
 *
 * XDPSQs not using interrupts but lazy cleaning, i.e. only when there's no
 * space for sending the current queued frame/bulk, must fire up timers to
 * make sure there are no stale buffers to free.
 */
struct libeth_xdpsq_timer {
	void				*xdpsq;
	struct libeth_xdpsq_lock	*lock;

	struct delayed_work		dwork;
};

/* Rx polling path */

/**
 * struct libeth_xdp_buff_stash - struct for stashing &xdp_buff onto a queue
 * @data: pointer to the start of the frame, xdp_buff.data
 * @headroom: frame headroom, xdp_buff.data - xdp_buff.data_hard_start
 * @len: frame linear space length, xdp_buff.data_end - xdp_buff.data
 * @frame_sz: truesize occupied by the frame, xdp_buff.frame_sz
 * @flags: xdp_buff.flags
 *
 * &xdp_buff is 56 bytes long on x64, &libeth_xdp_buff is 64 bytes. This
 * structure carries only necessary fields to save/restore a partially built
 * frame on the queue structure to finish it during the next NAPI poll.
 */
struct libeth_xdp_buff_stash {
	void				*data;
	u16				headroom;
	u16				len;

	u32				frame_sz:24;
	u32				flags:8;
} __aligned_largest;

#endif /* __LIBETH_TYPES_H */
