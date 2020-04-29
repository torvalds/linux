// SPDX-License-Identifier: GPL-2.0
#ifndef __NETCNT_COMMON_H
#define __NETCNT_COMMON_H

#include <linux/types.h>

#define MAX_PERCPU_PACKETS 32

struct percpu_net_cnt {
	__u64 packets;
	__u64 bytes;

	__u64 prev_ts;

	__u64 prev_packets;
	__u64 prev_bytes;
};

struct net_cnt {
	__u64 packets;
	__u64 bytes;
};

#endif
