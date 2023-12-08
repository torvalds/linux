/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2016 Facebook
 */
#ifndef _SAMPLES_BPF_XDP_TX_IPTNL_COMMON_H
#define _SAMPLES_BPF_XDP_TX_IPTNL_COMMON_H

#include <linux/types.h>

#define MAX_IPTNL_ENTRIES 256U

struct vip {
	union {
		__u32 v6[4];
		__u32 v4;
	} daddr;
	__u16 dport;
	__u16 family;
	__u8 protocol;
};

struct iptnl_info {
	union {
		__u32 v6[4];
		__u32 v4;
	} saddr;
	union {
		__u32 v6[4];
		__u32 v4;
	} daddr;
	__u16 family;
	__u8 dmac[6];
};

#endif
