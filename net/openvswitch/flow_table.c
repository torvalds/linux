/*
 * Copyright (c) 2007-2014 Nicira, Inc.
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

#include "flow.h"
#include "datapath.h"
#include <linux/uaccess.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/if_ether.h>
#include <linux/if_vlan.h>
#include <net/llc_pdu.h>
#include <linux/kernel.h>
#include <linux/jhash.h>
#include <linux/jiffies.h>
#include <linux/llc.h>
#include <linux/module.h>
#include <linux/in.h>
#include <linux/rcupdate.h>
#include <linux/if_arp.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <linux/sctp.h>
#include <linux/tcp.h>
#include <linux/udp.h>
#include <linux/icmp.h>
#include <linux/icmpv6.h>
#include <linux/rculist.h>
#include <net/ip.h>
#include <net/ipv6.h>
#include <net/ndisc.h>

#define TBL_MIN_BUCKETS		1024
#define REHASH_INTERVAL		(10 * 60 * HZ)

static struct kmem_cache *flow_cache;
struct kmem_cache *flow_stats_cache __read_mostly;

static u16 range_n_bytes(const struct sw_flow_key_range *range)
{
	return range->end - range->start;
}

void ovs_flow_mask_key(struct sw_flow_key *dst, const struct sw_flow_key *src,
		       const struct sw_flow_mask *mask)
{
	const long *m = (const long *)((const u8 *)&mask->key +
				mask->range.start);
	const long *s = (const long *)((const u8 *)src +
				mask->range.start);
	long *d = (long *)((u8 *)dst + mask->range.start);
	int i;

	/* The memory outside of the 'mask->range' are not set since
	 * further operations on 'dst' only uses contents within
	 * 'mask->range'.
	 */
	for (i = 0; i < range_n_bytes(&mask->range); i += sizeof(long))
		*d++ = *s++ & *m++;
}

struct sw_flow *ovs_flow_alloc(void)
{
	struct sw_flow *flow;
	struct flow_stats *stats;
	int node;

	flow = kmem_cache_alloc(flow_cache, GFP_KERNEL);
	if (!flow)
		return ERR_PTR(-ENOMEM);

	flow->sf_acts = NULL;
	flow->mask = NULL;
	flow->stats_last_writer = NUMA_NO_NODE;

	/* Initialize the default stat node. */
	stats = kmem_cache_alloc_node(flow_stats_cache,
				      GFP_KERNEL | __GFP_ZERO, 0);
	if (!stats)
		goto err;

	spin_lock_init(&stats->lock);

	RCU_INIT_POINTER(flow->stats[0], stats);

	for_each_node(node)
		if (node != 0)
			RCU_INIT_POINTER(flow->stats[node], NULL);

	return flow;
err:
	kmem_cache_free(flow_cache, flow);
	return ERR_PTR(-ENOMEM);
}

int ovs_flow_tbl_count(const struct flow_table *table)
{
	return table->count;
}

static struct flex_array *alloc_buckets(unsigned int n_buckets)
{
	struct flex_array *buckets;
	int i, err;

	buckets = flex_array_alloc(sizeof(struct hlist_head),
				   n_buckets, GFP_KERNEL);
	if (!buckets)
		return NULL;

	err = flex_array_prealloc(buckets, 0, n_buckets, GFP_KERNEL);
	if (err) {
		flex_array_free(buckets);
		return NULL;
	}

	for (i = 0; i < n_buckets; i++)
		INIT_HLIST_HEAD((struct hlist_head *)
					flex_array_get(buckets, i));

	return buckets;
}

static void flow_free(struct sw_flow *flow)
{
	int node;

	kfree((struct sw_flow_actions __force *)flow->sf_acts);
	for_each_node(node)
		if (flow->stats[node])
			kmem_cache_free(flow_stats_cache,
					(struct flow_stats __force *)flow->stats[node]);
	kmem_cache_free(flow_cache, flow);
}

static void rcu_free_flow_callback(struct rcu_head *rcu)
{
	struct sw_flow *flow = container_of(rcu, struct sw_flow, rcu);

	flow_free(flow);
}

void ovs_flow_free(struct sw_flow *flow, bool deferred)
{
	if (!flow)
		return;

	if (deferred)
		call_rcu(&flow->rcu, rcu_free_flow_callback);
	else
		flow_free(flow);
}

static void free_buckets(struct flex_array *buckets)
{
	flex_array_free(buckets);
}


static void __table_instance_destroy(struct table_instance *ti)
{
	free_buckets(ti->buckets);
	kfree(ti);
}

static struct table_instance *table_instance_alloc(int new_size)
{
	struct table_instance *ti = kmalloc(sizeof(*ti), GFP_KERNEL);

	if (!ti)
		return NULL;

	ti->buckets = alloc_buckets(new_size);

	if (!ti->buckets) {
		kfree(ti);
		return NULL;
	}
	ti->n_buckets = new_size;
	ti->node_ver = 0;
	ti->keep_flows = false;
	get_random_bytes(&ti->hash_seed, sizeof(u32));

	return ti;
}

int ovs_flow_tbl_init(struct flow_table *table)
{
	struct table_instance *ti;

	ti = table_instance_alloc(TBL_MIN_BUCKETS);

	if (!ti)
		return -ENOMEM;

	rcu_assign_pointer(table->ti, ti);
	INIT_LIST_HEAD(&table->mask_list);
	table->last_rehash = jiffies;
	table->count = 0;
	return 0;
}

static void flow_tbl_destroy_rcu_cb(struct rcu_head *rcu)
{
	struct table_instance *ti = container_of(rcu, struct table_instance, rcu);

	__table_instance_destroy(ti);
}

static void table_instance_destroy(struct table_instance *ti, bool deferred)
{
	int i;

	if (!ti)
		return;

	if (ti->keep_flows)
		goto skip_flows;

	for (i = 0; i < ti->n_buckets; i++) {
		struct sw_flow *flow;
		struct hlist_head *head = flex_array_get(ti->buckets, i);
		struct hlist_node *n;
		int ver = ti->node_ver;

		hlist_for_each_entry_safe(flow, n, head, hash_node[ver]) {
			hlist_del_rcu(&flow->hash_node[ver]);
			ovs_flow_free(flow, deferred);
		}
	}

skip_flows:
	if (deferred)
		call_rcu(&ti->rcu, flow_tbl_destroy_rcu_cb);
	else
		__table_instance_destroy(ti);
}

/* No need for locking this function is called from RCU callback or
 * error path.
 */
void ovs_flow_tbl_destroy(struct flow_table *table)
{
	struct table_instance *ti = rcu_dereference_raw(table->ti);

	table_instance_destroy(ti, false);
}

struct sw_flow *ovs_flow_tbl_dump_next(struct table_instance *ti,
				       u32 *bucket, u32 *last)
{
	struct sw_flow *flow;
	struct hlist_head *head;
	int ver;
	int i;

	ver = ti->node_ver;
	while (*bucket < ti->n_buckets) {
		i = 0;
		head = flex_array_get(ti->buckets, *bucket);
		hlist_for_each_entry_rcu(flow, head, hash_node[ver]) {
			if (i < *last) {
				i++;
				continue;
			}
			*last = i + 1;
			return flow;
		}
		(*bucket)++;
		*last = 0;
	}

	return NULL;
}

static struct hlist_head *find_bucket(struct table_instance *ti, u32 hash)
{
	hash = jhash_1word(hash, ti->hash_seed);
	return flex_array_get(ti->buckets,
				(hash & (ti->n_buckets - 1)));
}

static void table_instance_insert(struct table_instance *ti, struct sw_flow *flow)
{
	struct hlist_head *head;

	head = find_bucket(ti, flow->hash);
	hlist_add_head_rcu(&flow->hash_node[ti->node_ver], head);
}

static void flow_table_copy_flows(struct table_instance *old,
				  struct table_instance *new)
{
	int old_ver;
	int i;

	old_ver = old->node_ver;
	new->node_ver = !old_ver;

	/* Insert in new table. */
	for (i = 0; i < old->n_buckets; i++) {
		struct sw_flow *flow;
		struct hlist_head *head;

		head = flex_array_get(old->buckets, i);

		hlist_for_each_entry(flow, head, hash_node[old_ver])
			table_instance_insert(new, flow);
	}

	old->keep_flows = true;
}

static struct table_instance *table_instance_rehash(struct table_instance *ti,
					    int n_buckets)
{
	struct table_instance *new_ti;

	new_ti = table_instance_alloc(n_buckets);
	if (!new_ti)
		return NULL;

	flow_table_copy_flows(ti, new_ti);

	return new_ti;
}

int ovs_flow_tbl_flush(struct flow_table *flow_table)
{
	struct table_instance *old_ti;
	struct table_instance *new_ti;

	old_ti = ovsl_dereference(flow_table->ti);
	new_ti = table_instance_alloc(TBL_MIN_BUCKETS);
	if (!new_ti)
		return -ENOMEM;

	rcu_assign_pointer(flow_table->ti, new_ti);
	flow_table->last_rehash = jiffies;
	flow_table->count = 0;

	table_instance_destroy(old_ti, true);
	return 0;
}

static u32 flow_hash(const struct sw_flow_key *key, int key_start,
		     int key_end)
{
	const u32 *hash_key = (const u32 *)((const u8 *)key + key_start);
	int hash_u32s = (key_end - key_start) >> 2;

	/* Make sure number of hash bytes are multiple of u32. */
	BUILD_BUG_ON(sizeof(long) % sizeof(u32));

	return jhash2(hash_key, hash_u32s, 0);
}

static int flow_key_start(const struct sw_flow_key *key)
{
	if (key->tun_key.ipv4_dst)
		return 0;
	else
		return rounddown(offsetof(struct sw_flow_key, phy),
					  sizeof(long));
}

static bool cmp_key(const struct sw_flow_key *key1,
		    const struct sw_flow_key *key2,
		    int key_start, int key_end)
{
	const long *cp1 = (const long *)((const u8 *)key1 + key_start);
	const long *cp2 = (const long *)((const u8 *)key2 + key_start);
	long diffs = 0;
	int i;

	for (i = key_start; i < key_end;  i += sizeof(long))
		diffs |= *cp1++ ^ *cp2++;

	return diffs == 0;
}

static bool flow_cmp_masked_key(const struct sw_flow *flow,
				const struct sw_flow_key *key,
				int key_start, int key_end)
{
	return cmp_key(&flow->key, key, key_start, key_end);
}

bool ovs_flow_cmp_unmasked_key(const struct sw_flow *flow,
			       const struct sw_flow_match *match)
{
	struct sw_flow_key *key = match->key;
	int key_start = flow_key_start(key);
	int key_end = match->range.end;

	return cmp_key(&flow->unmasked_key, key, key_start, key_end);
}

static struct sw_flow *masked_flow_lookup(struct table_instance *ti,
					  const struct sw_flow_key *unmasked,
					  const struct sw_flow_mask *mask)
{
	struct sw_flow *flow;
	struct hlist_head *head;
	int key_start = mask->range.start;
	int key_end = mask->range.end;
	u32 hash;
	struct sw_flow_key masked_key;

	ovs_flow_mask_key(&masked_key, unmasked, mask);
	hash = flow_hash(&masked_key, key_start, key_end);
	head = find_bucket(ti, hash);
	hlist_for_each_entry_rcu(flow, head, hash_node[ti->node_ver]) {
		if (flow->mask == mask && flow->hash == hash &&
		    flow_cmp_masked_key(flow, &masked_key,
					  key_start, key_end))
			return flow;
	}
	return NULL;
}

struct sw_flow *ovs_flow_tbl_lookup_stats(struct flow_table *tbl,
				    const struct sw_flow_key *key,
				    u32 *n_mask_hit)
{
	struct table_instance *ti = rcu_dereference_ovsl(tbl->ti);
	struct sw_flow_mask *mask;
	struct sw_flow *flow;

	*n_mask_hit = 0;
	list_for_each_entry_rcu(mask, &tbl->mask_list, list) {
		(*n_mask_hit)++;
		flow = masked_flow_lookup(ti, key, mask);
		if (flow)  /* Found */
			return flow;
	}
	return NULL;
}

struct sw_flow *ovs_flow_tbl_lookup(struct flow_table *tbl,
				    const struct sw_flow_key *key)
{
	u32 __always_unused n_mask_hit;

	return ovs_flow_tbl_lookup_stats(tbl, key, &n_mask_hit);
}

struct sw_flow *ovs_flow_tbl_lookup_exact(struct flow_table *tbl,
					  const struct sw_flow_match *match)
{
	struct table_instance *ti = rcu_dereference_ovsl(tbl->ti);
	struct sw_flow_mask *mask;
	struct sw_flow *flow;

	/* Always called under ovs-mutex. */
	list_for_each_entry(mask, &tbl->mask_list, list) {
		flow = masked_flow_lookup(ti, match->key, mask);
		if (flow && ovs_flow_cmp_unmasked_key(flow, match))  /* Found */
			return flow;
	}
	return NULL;
}

int ovs_flow_tbl_num_masks(const struct flow_table *table)
{
	struct sw_flow_mask *mask;
	int num = 0;

	list_for_each_entry(mask, &table->mask_list, list)
		num++;

	return num;
}

static struct table_instance *table_instance_expand(struct table_instance *ti)
{
	return table_instance_rehash(ti, ti->n_buckets * 2);
}

/* Remove 'mask' from the mask list, if it is not needed any more. */
static void flow_mask_remove(struct flow_table *tbl, struct sw_flow_mask *mask)
{
	if (mask) {
		/* ovs-lock is required to protect mask-refcount and
		 * mask list.
		 */
		ASSERT_OVSL();
		BUG_ON(!mask->ref_count);
		mask->ref_count--;

		if (!mask->ref_count) {
			list_del_rcu(&mask->list);
			kfree_rcu(mask, rcu);
		}
	}
}

/* Must be called with OVS mutex held. */
void ovs_flow_tbl_remove(struct flow_table *table, struct sw_flow *flow)
{
	struct table_instance *ti = ovsl_dereference(table->ti);

	BUG_ON(table->count == 0);
	hlist_del_rcu(&flow->hash_node[ti->node_ver]);
	table->count--;

	/* RCU delete the mask. 'flow->mask' is not NULLed, as it should be
	 * accessible as long as the RCU read lock is held.
	 */
	flow_mask_remove(table, flow->mask);
}

static struct sw_flow_mask *mask_alloc(void)
{
	struct sw_flow_mask *mask;

	mask = kmalloc(sizeof(*mask), GFP_KERNEL);
	if (mask)
		mask->ref_count = 1;

	return mask;
}

static bool mask_equal(const struct sw_flow_mask *a,
		       const struct sw_flow_mask *b)
{
	const u8 *a_ = (const u8 *)&a->key + a->range.start;
	const u8 *b_ = (const u8 *)&b->key + b->range.start;

	return  (a->range.end == b->range.end)
		&& (a->range.start == b->range.start)
		&& (memcmp(a_, b_, range_n_bytes(&a->range)) == 0);
}

static struct sw_flow_mask *flow_mask_find(const struct flow_table *tbl,
					   const struct sw_flow_mask *mask)
{
	struct list_head *ml;

	list_for_each(ml, &tbl->mask_list) {
		struct sw_flow_mask *m;
		m = container_of(ml, struct sw_flow_mask, list);
		if (mask_equal(mask, m))
			return m;
	}

	return NULL;
}

/* Add 'mask' into the mask list, if it is not already there. */
static int flow_mask_insert(struct flow_table *tbl, struct sw_flow *flow,
			    const struct sw_flow_mask *new)
{
	struct sw_flow_mask *mask;
	mask = flow_mask_find(tbl, new);
	if (!mask) {
		/* Allocate a new mask if none exsits. */
		mask = mask_alloc();
		if (!mask)
			return -ENOMEM;
		mask->key = new->key;
		mask->range = new->range;
		list_add_rcu(&mask->list, &tbl->mask_list);
	} else {
		BUG_ON(!mask->ref_count);
		mask->ref_count++;
	}

	flow->mask = mask;
	return 0;
}

/* Must be called with OVS mutex held. */
int ovs_flow_tbl_insert(struct flow_table *table, struct sw_flow *flow,
			const struct sw_flow_mask *mask)
{
	struct table_instance *new_ti = NULL;
	struct table_instance *ti;
	int err;

	err = flow_mask_insert(table, flow, mask);
	if (err)
		return err;

	flow->hash = flow_hash(&flow->key, flow->mask->range.start,
			flow->mask->range.end);
	ti = ovsl_dereference(table->ti);
	table_instance_insert(ti, flow);
	table->count++;

	/* Expand table, if necessary, to make room. */
	if (table->count > ti->n_buckets)
		new_ti = table_instance_expand(ti);
	else if (time_after(jiffies, table->last_rehash + REHASH_INTERVAL))
		new_ti = table_instance_rehash(ti, ti->n_buckets);

	if (new_ti) {
		rcu_assign_pointer(table->ti, new_ti);
		table_instance_destroy(ti, true);
		table->last_rehash = jiffies;
	}
	return 0;
}

/* Initializes the flow module.
 * Returns zero if successful or a negative error code. */
int ovs_flow_init(void)
{
	BUILD_BUG_ON(__alignof__(struct sw_flow_key) % __alignof__(long));
	BUILD_BUG_ON(sizeof(struct sw_flow_key) % sizeof(long));

	flow_cache = kmem_cache_create("sw_flow", sizeof(struct sw_flow)
				       + (num_possible_nodes()
					  * sizeof(struct flow_stats *)),
				       0, 0, NULL);
	if (flow_cache == NULL)
		return -ENOMEM;

	flow_stats_cache
		= kmem_cache_create("sw_flow_stats", sizeof(struct flow_stats),
				    0, SLAB_HWCACHE_ALIGN, NULL);
	if (flow_stats_cache == NULL) {
		kmem_cache_destroy(flow_cache);
		flow_cache = NULL;
		return -ENOMEM;
	}

	return 0;
}

/* Uninitializes the flow module. */
void ovs_flow_exit(void)
{
	kmem_cache_destroy(flow_stats_cache);
	kmem_cache_destroy(flow_cache);
}
