/*
 * Copyright (c) 2007-2013 Nicira, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA
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
#include <linux/flex_array.h>

#include <net/inet_ecn.h>
#include <net/ip_tunnels.h>

#include "flow.h"

struct table_instance {
	struct flex_array *buckets;
	unsigned int n_buckets;
	struct rcu_head rcu;
	int node_ver;
	u32 hash_seed;
	bool keep_flows;
};

struct flow_table {
	struct table_instance __rcu *ti;
	struct list_head mask_list;
	unsigned long last_rehash;
	unsigned int count;
};

extern struct kmem_cache *flow_stats_cache;

int ovs_flow_init(void);
void ovs_flow_exit(void);

struct sw_flow *ovs_flow_alloc(void);
void ovs_flow_free(struct sw_flow *, bool deferred);

int ovs_flow_tbl_init(struct flow_table *);
int ovs_flow_tbl_count(struct flow_table *table);
void ovs_flow_tbl_destroy(struct flow_table *table, bool deferred);
int ovs_flow_tbl_flush(struct flow_table *flow_table);

int ovs_flow_tbl_insert(struct flow_table *table, struct sw_flow *flow,
			struct sw_flow_mask *mask);
void ovs_flow_tbl_remove(struct flow_table *table, struct sw_flow *flow);
int  ovs_flow_tbl_num_masks(const struct flow_table *table);
struct sw_flow *ovs_flow_tbl_dump_next(struct table_instance *table,
				       u32 *bucket, u32 *idx);
struct sw_flow *ovs_flow_tbl_lookup_stats(struct flow_table *,
				    const struct sw_flow_key *,
				    u32 *n_mask_hit);
struct sw_flow *ovs_flow_tbl_lookup(struct flow_table *,
				    const struct sw_flow_key *);

bool ovs_flow_cmp_unmasked_key(const struct sw_flow *flow,
			       struct sw_flow_match *match);

void ovs_flow_mask_key(struct sw_flow_key *dst, const struct sw_flow_key *src,
		       const struct sw_flow_mask *mask);
#endif /* flow_table.h */
