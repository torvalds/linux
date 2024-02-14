/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2017 Nicira, Inc.
 */

#ifndef METER_H
#define METER_H 1

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/netlink.h>
#include <linux/openvswitch.h>
#include <linux/genetlink.h>
#include <linux/skbuff.h>
#include <linux/bits.h>

#include "flow.h"
struct datapath;

#define DP_MAX_BANDS		1
#define DP_METER_ARRAY_SIZE_MIN	BIT_ULL(10)
#define DP_METER_NUM_MAX	(200000UL)

struct dp_meter_band {
	u32 type;
	u32 rate;
	u32 burst_size;
	u64 bucket; /* 1/1000 packets, or in bits */
	struct ovs_flow_stats stats;
};

struct dp_meter {
	spinlock_t lock;    /* Per meter lock */
	struct rcu_head rcu;
	u32 id;
	u16 kbps:1, keep_stats:1;
	u16 n_bands;
	u32 max_delta_t;
	u64 used;
	struct ovs_flow_stats stats;
	struct dp_meter_band bands[];
};

struct dp_meter_instance {
	struct rcu_head rcu;
	u32 n_meters;
	struct dp_meter __rcu *dp_meters[];
};

struct dp_meter_table {
	struct dp_meter_instance __rcu *ti;
	u32 count;
	u32 max_meters_allowed;
};

extern struct genl_family dp_meter_genl_family;
int ovs_meters_init(struct datapath *dp);
void ovs_meters_exit(struct datapath *dp);
bool ovs_meter_execute(struct datapath *dp, struct sk_buff *skb,
		       struct sw_flow_key *key, u32 meter_id);

#endif /* meter.h */
