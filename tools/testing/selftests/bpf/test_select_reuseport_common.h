/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2018 Facebook */

#ifndef __TEST_SELECT_REUSEPORT_COMMON_H
#define __TEST_SELECT_REUSEPORT_COMMON_H

#include <linux/types.h>

enum result {
	DROP_ERR_INNER_MAP,
	DROP_ERR_SKB_DATA,
	DROP_ERR_SK_SELECT_REUSEPORT,
	DROP_MISC,
	PASS,
	PASS_ERR_SK_SELECT_REUSEPORT,
	NR_RESULTS,
};

struct cmd {
	__u32 reuseport_index;
	__u32 pass_on_failure;
};

struct data_check {
	__u32 ip_protocol;
	__u32 skb_addrs[8];
	__u16 skb_ports[2];
	__u16 eth_protocol;
	__u8  bind_inany;
	__u8  equal_check_end[0];

	__u32 len;
	__u32 hash;
};

#endif
