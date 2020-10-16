/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2007-2013 Nicira, Inc.
 */

#ifndef FLOW_TABLE_H
#define FLOW_TABLE_H 1

#include <linux/kernel.h>
#include <linux/netlink.h>
#include <linux/openvswitch.h>
#include <linux/spinlock.h>
#include <linux/types.h>
#include <linux/rcupdate.h>
#include <linux/if_ether.h>
#include <linux/in6.h>
#include <linux/jiffies.h>
#include <linux/time.h>

#include <net/inet_ecn.h>
#include <net/ip_tunnels.h>

#include "flow.h"

struct mask_cache_entry {
	u32 skb_hash;
	u32 mask_index;
};

struct mask_cache {
	struct rcu_head rcu;
	u32 cache_size;  /* Must be ^2 value. */
	struct mask_cache_entry __percpu *mask_cache;
};

struct mask_count {
	int index;
	u64 counter;
};

struct mask_array {
	struct rcu_head rcu;
	int count, max;
	u64 __percpu *masks_usage_cntr;
	u64 *masks_usage_zero_cntr;
	struct u64_stats_sync syncp;
	struct sw_flow_mask __rcu *masks[];
};

struct table_instance {
	struct hlist_head *buckets;
	unsigned int n_buckets;
	struct rcu_head rcu;
	int node_ver;
	u32 hash_seed;
};

struct flow_table {
	struct table_instance __rcu *ti;
	struct table_instance __rcu *ufid_ti;
	struct mask_cache __rcu *mask_cache;
	struct mask_array __rcu *mask_array;
	unsigned long last_rehash;
	unsigned int count;
	unsigned int ufid_count;
};

extern struct kmem_cache *flow_stats_cache;

int ovs_flow_init(void);
void ovs_flow_exit(void);

struct sw_flow *ovs_flow_alloc(void);
void ovs_flow_free(struct sw_flow *, bool deferred);

int ovs_flow_tbl_init(struct flow_table *);
int ovs_flow_tbl_count(const struct flow_table *table);
void ovs_flow_tbl_destroy(struct flow_table *table);
int ovs_flow_tbl_flush(struct flow_table *flow_table);

int ovs_flow_tbl_insert(struct flow_table *table, struct sw_flow *flow,
			const struct sw_flow_mask *mask);
void ovs_flow_tbl_remove(struct flow_table *table, struct sw_flow *flow);
int  ovs_flow_tbl_num_masks(const struct flow_table *table);
u32  ovs_flow_tbl_masks_cache_size(const struct flow_table *table);
int  ovs_flow_tbl_masks_cache_resize(struct flow_table *table, u32 size);
struct sw_flow *ovs_flow_tbl_dump_next(struct table_instance *table,
				       u32 *bucket, u32 *idx);
struct sw_flow *ovs_flow_tbl_lookup_stats(struct flow_table *,
					  const struct sw_flow_key *,
					  u32 skb_hash,
					  u32 *n_mask_hit,
					  u32 *n_cache_hit);
struct sw_flow *ovs_flow_tbl_lookup(struct flow_table *,
				    const struct sw_flow_key *);
struct sw_flow *ovs_flow_tbl_lookup_exact(struct flow_table *tbl,
					  const struct sw_flow_match *match);
struct sw_flow *ovs_flow_tbl_lookup_ufid(struct flow_table *,
					 const struct sw_flow_id *);

bool ovs_flow_cmp(const struct sw_flow *, const struct sw_flow_match *);

void ovs_flow_mask_key(struct sw_flow_key *dst, const struct sw_flow_key *src,
		       bool full, const struct sw_flow_mask *mask);

void ovs_flow_masks_rebalance(struct flow_table *table);
void table_instance_flow_flush(struct flow_table *table,
			       struct table_instance *ti,
			       struct table_instance *ufid_ti);

#endif /* flow_table.h */
