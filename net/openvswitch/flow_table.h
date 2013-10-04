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

#define TBL_MIN_BUCKETS		1024

struct flow_table {
	struct flex_array *buckets;
	unsigned int count, n_buckets;
	struct rcu_head rcu;
	struct list_head *mask_list;
	int node_ver;
	u32 hash_seed;
	bool keep_flows;
};

int ovs_flow_init(void);
void ovs_flow_exit(void);

struct sw_flow *ovs_flow_alloc(void);
void ovs_flow_free(struct sw_flow *, bool deferred);

static inline int ovs_flow_tbl_count(struct flow_table *table)
{
	return table->count;
}

static inline int ovs_flow_tbl_need_to_expand(struct flow_table *table)
{
	return (table->count > table->n_buckets);
}

struct flow_table *ovs_flow_tbl_alloc(int new_size);
struct flow_table *ovs_flow_tbl_expand(struct flow_table *table);
struct flow_table *ovs_flow_tbl_rehash(struct flow_table *table);
void ovs_flow_tbl_destroy(struct flow_table *table, bool deferred);

void ovs_flow_tbl_insert(struct flow_table *table, struct sw_flow *flow);
void ovs_flow_tbl_remove(struct flow_table *table, struct sw_flow *flow);
struct sw_flow *ovs_flow_tbl_dump_next(struct flow_table *table,
				       u32 *bucket, u32 *idx);
struct sw_flow *ovs_flow_tbl_lookup(struct flow_table *,
				    const struct sw_flow_key *);

bool ovs_flow_cmp_unmasked_key(const struct sw_flow *flow,
			       struct sw_flow_match *match);

struct sw_flow_mask *ovs_sw_flow_mask_alloc(void);
void ovs_sw_flow_mask_add_ref(struct sw_flow_mask *);
void ovs_sw_flow_mask_del_ref(struct sw_flow_mask *, bool deferred);
void ovs_sw_flow_mask_insert(struct flow_table *, struct sw_flow_mask *);
struct sw_flow_mask *ovs_sw_flow_mask_find(const struct flow_table *,
					   const struct sw_flow_mask *);
void ovs_flow_mask_key(struct sw_flow_key *dst, const struct sw_flow_key *src,
		       const struct sw_flow_mask *mask);

#endif /* flow_table.h */
