/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES */
#ifndef __ENQ_BLOCKED_H
#define __ENQ_BLOCKED_H

#include <linux/ioctl.h>
#include <linux/types.h>

struct enq_blocked_stats {
	__u64 hold_time_ns;
	__u64 wait_time_ns;
	__u64 nr_holds;
	__u64 nr_waits;
};

#define ENQ_BLOCKED_IOCTL_OWNER	_IO('s', 1)
#define ENQ_BLOCKED_IOCTL_DONOR	_IO('s', 2)
#define ENQ_BLOCKED_IOCTL_RESET_STATS	_IO('s', 3)
#define ENQ_BLOCKED_IOCTL_GET_STATS	_IOR('s', 4, struct enq_blocked_stats)
#define ENQ_BLOCKED_IOCTL_PREP_ATTACH	_IO('s', 5)
#define ENQ_BLOCKED_IOCTL_ATTACH_DONE	_IO('s', 6)
#define ENQ_BLOCKED_IOCTL_DONOR_STATE	_IO('s', 7)
#define ENQ_BLOCKED_IOCTL_PROXY_SUPPORTED _IO('s', 8)

#define ENQ_BLOCKED_DONOR_SLEEPING	(1U << 0)
#define ENQ_BLOCKED_DONOR_ON_RQ		(1U << 1)

#endif /* __ENQ_BLOCKED_H */
