// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2007-2014 Nicira, Inc.
 */

#include "flow.h"
#include "datapath.h"
#include "flow_netlink.h"
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
#include <linux/cpumask.h>
#include <linux/if_arp.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <linux/sctp.h>
#include <linux/tcp.h>
#include <linux/udp.h>
#include <linux/icmp.h>
#include <linux/icmpv6.h>
#include <linux/rculist.h>
#include <linux/sort.h>
#include <net/ip.h>
#include <net/ipv6.h>
#include <net/ndisc.h>

#define TBL_MIN_BUCKETS		1024
#define MASK_ARRAY_SIZE_MIN	16
#define REHASH_INTERVAL		(10 * 60 * HZ)

#define MC_DEFAULT_HASH_ENTRIES	256
#define MC_HASH_SHIFT		8
#define MC_HASH_SEGS		((sizeof(uint32_t) * 8) / MC_HASH_SHIFT)

static struct kmem_cache *flow_cache;
struct kmem_cache *flow_stats_cache __read_mostly;

static u16 range_n_bytes(const struct sw_flow_key_range *range)
{
	return range->end - range->start;
}

void ovs_flow_mask_key(struct sw_flow_key *dst, const struct sw_flow_key *src,
		       bool full, const struct sw_flow_mask *mask)
{
	int start = full ? 0 : mask->range.start;
	int len = full ? sizeof *dst : range_n_bytes(&mask->range);
	const long *m = (const long *)((const u8 *)&mask->key + start);
	const long *s = (const long *)((const u8 *)src + start);
	long *d = (long *)((u8 *)dst + start);
	int i;

	/* If 'full' is true then all of 'dst' is fully initialized. Otherwise,
	 * if 'full' is false the memory outside of the 'mask->range' is left
	 * uninitialized. This can be used as an optimization when further
	 * operations on 'dst' only use contents within 'mask->range'.
	 */
	for (i = 0; i < len; i += sizeof(long))
		*d++ = *s++ & *m++;
}

struct sw_flow *ovs_flow_alloc(void)
{
	struct sw_flow *flow;
	struct sw_flow_stats *stats;

	flow = kmem_cache_zalloc(flow_cache, GFP_KERNEL);
	if (!flow)
		return ERR_PTR(-ENOMEM);

	flow->stats_last_writer = -1;

	/* Initialize the default stat node. */
	stats = kmem_cache_alloc_node(flow_stats_cache,
				      GFP_KERNEL | __GFP_ZERO,
				      node_online(0) ? 0 : NUMA_NO_NODE);
	if (!stats)
		goto err;

	spin_lock_init(&stats->lock);

	RCU_INIT_POINTER(flow->stats[0], stats);

	cpumask_set_cpu(0, &flow->cpu_used_mask);

	return flow;
err:
	kmem_cache_free(flow_cache, flow);
	return ERR_PTR(-ENOMEM);
}

int ovs_flow_tbl_count(const struct flow_table *table)
{
	return table->count;
}

static void flow_free(struct sw_flow *flow)
{
	int cpu;

	if (ovs_identifier_is_key(&flow->id))
		kfree(flow->id.unmasked_key);
	if (flow->sf_acts)
		ovs_nla_free_flow_actions((struct sw_flow_actions __force *)flow->sf_acts);
	/* We open code this to make sure cpu 0 is always considered */
	for (cpu = 0; cpu < nr_cpu_ids; cpu = cpumask_next(cpu, &flow->cpu_used_mask))
		if (flow->stats[cpu])
			kmem_cache_free(flow_stats_cache,
					(struct sw_flow_stats __force *)flow->stats[cpu]);
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

static void __table_instance_destroy(struct table_instance *ti)
{
	kvfree(ti->buckets);
	kfree(ti);
}

static struct table_instance *table_instance_alloc(int new_size)
{
	struct table_instance *ti = kmalloc(sizeof(*ti), GFP_KERNEL);
	int i;

	if (!ti)
		return NULL;

	ti->buckets = kvmalloc_array(new_size, sizeof(struct hlist_head),
				     GFP_KERNEL);
	if (!ti->buckets) {
		kfree(ti);
		return NULL;
	}

	for (i = 0; i < new_size; i++)
		INIT_HLIST_HEAD(&ti->buckets[i]);

	ti->n_buckets = new_size;
	ti->node_ver = 0;
	ti->keep_flows = false;
	get_random_bytes(&ti->hash_seed, sizeof(u32));

	return ti;
}

static void __mask_array_destroy(struct mask_array *ma)
{
	free_percpu(ma->masks_usage_cntr);
	kfree(ma);
}

static void mask_array_rcu_cb(struct rcu_head *rcu)
{
	struct mask_array *ma = container_of(rcu, struct mask_array, rcu);

	__mask_array_destroy(ma);
}

static void tbl_mask_array_reset_counters(struct mask_array *ma)
{
	int i, cpu;

	/* As the per CPU counters are not atomic we can not go ahead and
	 * reset them from another CPU. To be able to still have an approximate
	 * zero based counter we store the value at reset, and subtract it
	 * later when processing.
	 */
	for (i = 0; i < ma->max; i++)  {
		ma->masks_usage_zero_cntr[i] = 0;

		for_each_possible_cpu(cpu) {
			u64 *usage_counters = per_cpu_ptr(ma->masks_usage_cntr,
							  cpu);
			unsigned int start;
			u64 counter;

			do {
				start = u64_stats_fetch_begin_irq(&ma->syncp);
				counter = usage_counters[i];
			} while (u64_stats_fetch_retry_irq(&ma->syncp, start));

			ma->masks_usage_zero_cntr[i] += counter;
		}
	}
}

static struct mask_array *tbl_mask_array_alloc(int size)
{
	struct mask_array *new;

	size = max(MASK_ARRAY_SIZE_MIN, size);
	new = kzalloc(sizeof(struct mask_array) +
		      sizeof(struct sw_flow_mask *) * size +
		      sizeof(u64) * size, GFP_KERNEL);
	if (!new)
		return NULL;

	new->masks_usage_zero_cntr = (u64 *)((u8 *)new +
					     sizeof(struct mask_array) +
					     sizeof(struct sw_flow_mask *) *
					     size);

	new->masks_usage_cntr = __alloc_percpu(sizeof(u64) * size,
					       __alignof__(u64));
	if (!new->masks_usage_cntr) {
		kfree(new);
		return NULL;
	}

	new->count = 0;
	new->max = size;

	return new;
}

static int tbl_mask_array_realloc(struct flow_table *tbl, int size)
{
	struct mask_array *old;
	struct mask_array *new;

	new = tbl_mask_array_alloc(size);
	if (!new)
		return -ENOMEM;

	old = ovsl_dereference(tbl->mask_array);
	if (old) {
		int i;

		for (i = 0; i < old->max; i++) {
			if (ovsl_dereference(old->masks[i]))
				new->masks[new->count++] = old->masks[i];
		}
		call_rcu(&old->rcu, mask_array_rcu_cb);
	}

	rcu_assign_pointer(tbl->mask_array, new);

	return 0;
}

static int tbl_mask_array_add_mask(struct flow_table *tbl,
				   struct sw_flow_mask *new)
{
	struct mask_array *ma = ovsl_dereference(tbl->mask_array);
	int err, ma_count = READ_ONCE(ma->count);

	if (ma_count >= ma->max) {
		err = tbl_mask_array_realloc(tbl, ma->max +
					      MASK_ARRAY_SIZE_MIN);
		if (err)
			return err;

		ma = ovsl_dereference(tbl->mask_array);
	} else {
		/* On every add or delete we need to reset the counters so
		 * every new mask gets a fair chance of being prioritized.
		 */
		tbl_mask_array_reset_counters(ma);
	}

	BUG_ON(ovsl_dereference(ma->masks[ma_count]));

	rcu_assign_pointer(ma->masks[ma_count], new);
	WRITE_ONCE(ma->count, ma_count +1);

	return 0;
}

static void tbl_mask_array_del_mask(struct flow_table *tbl,
				    struct sw_flow_mask *mask)
{
	struct mask_array *ma = ovsl_dereference(tbl->mask_array);
	int i, ma_count = READ_ONCE(ma->count);

	/* Remove the deleted mask pointers from the array */
	for (i = 0; i < ma_count; i++) {
		if (mask == ovsl_dereference(ma->masks[i]))
			goto found;
	}

	BUG();
	return;

found:
	WRITE_ONCE(ma->count, ma_count -1);

	rcu_assign_pointer(ma->masks[i], ma->masks[ma_count -1]);
	RCU_INIT_POINTER(ma->masks[ma_count -1], NULL);

	kfree_rcu(mask, rcu);

	/* Shrink the mask array if necessary. */
	if (ma->max >= (MASK_ARRAY_SIZE_MIN * 2) &&
	    ma_count <= (ma->max / 3))
		tbl_mask_array_realloc(tbl, ma->max / 2);
	else
		tbl_mask_array_reset_counters(ma);

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

		if (!mask->ref_count)
			tbl_mask_array_del_mask(tbl, mask);
	}
}

static void __mask_cache_destroy(struct mask_cache *mc)
{
	free_percpu(mc->mask_cache);
	kfree(mc);
}

static void mask_cache_rcu_cb(struct rcu_head *rcu)
{
	struct mask_cache *mc = container_of(rcu, struct mask_cache, rcu);

	__mask_cache_destroy(mc);
}

static struct mask_cache *tbl_mask_cache_alloc(u32 size)
{
	struct mask_cache_entry __percpu *cache = NULL;
	struct mask_cache *new;

	/* Only allow size to be 0, or a power of 2, and does not exceed
	 * percpu allocation size.
	 */
	if ((!is_power_of_2(size) && size != 0) ||
	    (size * sizeof(struct mask_cache_entry)) > PCPU_MIN_UNIT_SIZE)
		return NULL;

	new = kzalloc(sizeof(*new), GFP_KERNEL);
	if (!new)
		return NULL;

	new->cache_size = size;
	if (new->cache_size > 0) {
		cache = __alloc_percpu(array_size(sizeof(struct mask_cache_entry),
						  new->cache_size),
				       __alignof__(struct mask_cache_entry));
		if (!cache) {
			kfree(new);
			return NULL;
		}
	}

	new->mask_cache = cache;
	return new;
}
int ovs_flow_tbl_masks_cache_resize(struct flow_table *table, u32 size)
{
	struct mask_cache *mc = rcu_dereference(table->mask_cache);
	struct mask_cache *new;

	if (size == mc->cache_size)
		return 0;

	if ((!is_power_of_2(size) && size != 0) ||
	    (size * sizeof(struct mask_cache_entry)) > PCPU_MIN_UNIT_SIZE)
		return -EINVAL;

	new = tbl_mask_cache_alloc(size);
	if (!new)
		return -ENOMEM;

	rcu_assign_pointer(table->mask_cache, new);
	call_rcu(&mc->rcu, mask_cache_rcu_cb);

	return 0;
}

int ovs_flow_tbl_init(struct flow_table *table)
{
	struct table_instance *ti, *ufid_ti;
	struct mask_cache *mc;
	struct mask_array *ma;

	mc = tbl_mask_cache_alloc(MC_DEFAULT_HASH_ENTRIES);
	if (!mc)
		return -ENOMEM;

	ma = tbl_mask_array_alloc(MASK_ARRAY_SIZE_MIN);
	if (!ma)
		goto free_mask_cache;

	ti = table_instance_alloc(TBL_MIN_BUCKETS);
	if (!ti)
		goto free_mask_array;

	ufid_ti = table_instance_alloc(TBL_MIN_BUCKETS);
	if (!ufid_ti)
		goto free_ti;

	rcu_assign_pointer(table->ti, ti);
	rcu_assign_pointer(table->ufid_ti, ufid_ti);
	rcu_assign_pointer(table->mask_array, ma);
	rcu_assign_pointer(table->mask_cache, mc);
	table->last_rehash = jiffies;
	table->count = 0;
	table->ufid_count = 0;
	return 0;

free_ti:
	__table_instance_destroy(ti);
free_mask_array:
	__mask_array_destroy(ma);
free_mask_cache:
	__mask_cache_destroy(mc);
	return -ENOMEM;
}

static void flow_tbl_destroy_rcu_cb(struct rcu_head *rcu)
{
	struct table_instance *ti = container_of(rcu, struct table_instance, rcu);

	__table_instance_destroy(ti);
}

static void table_instance_flow_free(struct flow_table *table,
				  struct table_instance *ti,
				  struct table_instance *ufid_ti,
				  struct sw_flow *flow,
				  bool count)
{
	hlist_del_rcu(&flow->flow_table.node[ti->node_ver]);
	if (count)
		table->count--;

	if (ovs_identifier_is_ufid(&flow->id)) {
		hlist_del_rcu(&flow->ufid_table.node[ufid_ti->node_ver]);

		if (count)
			table->ufid_count--;
	}

	flow_mask_remove(table, flow->mask);
}

/* Must be called with OVS mutex held. */
void table_instance_flow_flush(struct flow_table *table,
			       struct table_instance *ti,
			       struct table_instance *ufid_ti)
{
	int i;

	if (ti->keep_flows)
		return;

	for (i = 0; i < ti->n_buckets; i++) {
		struct sw_flow *flow;
		struct hlist_head *head = &ti->buckets[i];
		struct hlist_node *n;

		hlist_for_each_entry_safe(flow, n, head,
					  flow_table.node[ti->node_ver]) {

			table_instance_flow_free(table, ti, ufid_ti,
						 flow, false);
			ovs_flow_free(flow, true);
		}
	}
}

static void table_instance_destroy(struct table_instance *ti,
				   struct table_instance *ufid_ti)
{
	call_rcu(&ti->rcu, flow_tbl_destroy_rcu_cb);
	call_rcu(&ufid_ti->rcu, flow_tbl_destroy_rcu_cb);
}

/* No need for locking this function is called from RCU callback or
 * error path.
 */
void ovs_flow_tbl_destroy(struct flow_table *table)
{
	struct table_instance *ti = rcu_dereference_raw(table->ti);
	struct table_instance *ufid_ti = rcu_dereference_raw(table->ufid_ti);
	struct mask_cache *mc = rcu_dereference_raw(table->mask_cache);
	struct mask_array *ma = rcu_dereference_raw(table->mask_array);

	call_rcu(&mc->rcu, mask_cache_rcu_cb);
	call_rcu(&ma->rcu, mask_array_rcu_cb);
	table_instance_destroy(ti, ufid_ti);
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
		head = &ti->buckets[*bucket];
		hlist_for_each_entry_rcu(flow, head, flow_table.node[ver]) {
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
	return &ti->buckets[hash & (ti->n_buckets - 1)];
}

static void table_instance_insert(struct table_instance *ti,
				  struct sw_flow *flow)
{
	struct hlist_head *head;

	head = find_bucket(ti, flow->flow_table.hash);
	hlist_add_head_rcu(&flow->flow_table.node[ti->node_ver], head);
}

static void ufid_table_instance_insert(struct table_instance *ti,
				       struct sw_flow *flow)
{
	struct hlist_head *head;

	head = find_bucket(ti, flow->ufid_table.hash);
	hlist_add_head_rcu(&flow->ufid_table.node[ti->node_ver], head);
}

static void flow_table_copy_flows(struct table_instance *old,
				  struct table_instance *new, bool ufid)
{
	int old_ver;
	int i;

	old_ver = old->node_ver;
	new->node_ver = !old_ver;

	/* Insert in new table. */
	for (i = 0; i < old->n_buckets; i++) {
		struct sw_flow *flow;
		struct hlist_head *head = &old->buckets[i];

		if (ufid)
			hlist_for_each_entry_rcu(flow, head,
						 ufid_table.node[old_ver],
						 lockdep_ovsl_is_held())
				ufid_table_instance_insert(new, flow);
		else
			hlist_for_each_entry_rcu(flow, head,
						 flow_table.node[old_ver],
						 lockdep_ovsl_is_held())
				table_instance_insert(new, flow);
	}

	old->keep_flows = true;
}

static struct table_instance *table_instance_rehash(struct table_instance *ti,
						    int n_buckets, bool ufid)
{
	struct table_instance *new_ti;

	new_ti = table_instance_alloc(n_buckets);
	if (!new_ti)
		return NULL;

	flow_table_copy_flows(ti, new_ti, ufid);

	return new_ti;
}

int ovs_flow_tbl_flush(struct flow_table *flow_table)
{
	struct table_instance *old_ti, *new_ti;
	struct table_instance *old_ufid_ti, *new_ufid_ti;

	new_ti = table_instance_alloc(TBL_MIN_BUCKETS);
	if (!new_ti)
		return -ENOMEM;
	new_ufid_ti = table_instance_alloc(TBL_MIN_BUCKETS);
	if (!new_ufid_ti)
		goto err_free_ti;

	old_ti = ovsl_dereference(flow_table->ti);
	old_ufid_ti = ovsl_dereference(flow_table->ufid_ti);

	rcu_assign_pointer(flow_table->ti, new_ti);
	rcu_assign_pointer(flow_table->ufid_ti, new_ufid_ti);
	flow_table->last_rehash = jiffies;
	flow_table->count = 0;
	flow_table->ufid_count = 0;

	table_instance_flow_flush(flow_table, old_ti, old_ufid_ti);
	table_instance_destroy(old_ti, old_ufid_ti);
	return 0;

err_free_ti:
	__table_instance_destroy(new_ti);
	return -ENOMEM;
}

static u32 flow_hash(const struct sw_flow_key *key,
		     const struct sw_flow_key_range *range)
{
	const u32 *hash_key = (const u32 *)((const u8 *)key + range->start);

	/* Make sure number of hash bytes are multiple of u32. */
	int hash_u32s = range_n_bytes(range) >> 2;

	return jhash2(hash_key, hash_u32s, 0);
}

static int flow_key_start(const struct sw_flow_key *key)
{
	if (key->tun_proto)
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
				const struct sw_flow_key_range *range)
{
	return cmp_key(&flow->key, key, range->start, range->end);
}

static bool ovs_flow_cmp_unmasked_key(const struct sw_flow *flow,
				      const struct sw_flow_match *match)
{
	struct sw_flow_key *key = match->key;
	int key_start = flow_key_start(key);
	int key_end = match->range.end;

	BUG_ON(ovs_identifier_is_ufid(&flow->id));
	return cmp_key(flow->id.unmasked_key, key, key_start, key_end);
}

static struct sw_flow *masked_flow_lookup(struct table_instance *ti,
					  const struct sw_flow_key *unmasked,
					  const struct sw_flow_mask *mask,
					  u32 *n_mask_hit)
{
	struct sw_flow *flow;
	struct hlist_head *head;
	u32 hash;
	struct sw_flow_key masked_key;

	ovs_flow_mask_key(&masked_key, unmasked, false, mask);
	hash = flow_hash(&masked_key, &mask->range);
	head = find_bucket(ti, hash);
	(*n_mask_hit)++;

	hlist_for_each_entry_rcu(flow, head, flow_table.node[ti->node_ver],
				lockdep_ovsl_is_held()) {
		if (flow->mask == mask && flow->flow_table.hash == hash &&
		    flow_cmp_masked_key(flow, &masked_key, &mask->range))
			return flow;
	}
	return NULL;
}

/* Flow lookup does full lookup on flow table. It starts with
 * mask from index passed in *index.
 */
static struct sw_flow *flow_lookup(struct flow_table *tbl,
				   struct table_instance *ti,
				   struct mask_array *ma,
				   const struct sw_flow_key *key,
				   u32 *n_mask_hit,
				   u32 *n_cache_hit,
				   u32 *index)
{
	u64 *usage_counters = this_cpu_ptr(ma->masks_usage_cntr);
	struct sw_flow *flow;
	struct sw_flow_mask *mask;
	int i;

	if (likely(*index < ma->max)) {
		mask = rcu_dereference_ovsl(ma->masks[*index]);
		if (mask) {
			flow = masked_flow_lookup(ti, key, mask, n_mask_hit);
			if (flow) {
				u64_stats_update_begin(&ma->syncp);
				usage_counters[*index]++;
				u64_stats_update_end(&ma->syncp);
				(*n_cache_hit)++;
				return flow;
			}
		}
	}

	for (i = 0; i < ma->max; i++)  {

		if (i == *index)
			continue;

		mask = rcu_dereference_ovsl(ma->masks[i]);
		if (unlikely(!mask))
			break;

		flow = masked_flow_lookup(ti, key, mask, n_mask_hit);
		if (flow) { /* Found */
			*index = i;
			u64_stats_update_begin(&ma->syncp);
			usage_counters[*index]++;
			u64_stats_update_end(&ma->syncp);
			return flow;
		}
	}

	return NULL;
}

/*
 * mask_cache maps flow to probable mask. This cache is not tightly
 * coupled cache, It means updates to  mask list can result in inconsistent
 * cache entry in mask cache.
 * This is per cpu cache and is divided in MC_HASH_SEGS segments.
 * In case of a hash collision the entry is hashed in next segment.
 * */
struct sw_flow *ovs_flow_tbl_lookup_stats(struct flow_table *tbl,
					  const struct sw_flow_key *key,
					  u32 skb_hash,
					  u32 *n_mask_hit,
					  u32 *n_cache_hit)
{
	struct mask_cache *mc = rcu_dereference(tbl->mask_cache);
	struct mask_array *ma = rcu_dereference(tbl->mask_array);
	struct table_instance *ti = rcu_dereference(tbl->ti);
	struct mask_cache_entry *entries, *ce;
	struct sw_flow *flow;
	u32 hash;
	int seg;

	*n_mask_hit = 0;
	*n_cache_hit = 0;
	if (unlikely(!skb_hash || mc->cache_size == 0)) {
		u32 mask_index = 0;
		u32 cache = 0;

		return flow_lookup(tbl, ti, ma, key, n_mask_hit, &cache,
				   &mask_index);
	}

	/* Pre and post recirulation flows usually have the same skb_hash
	 * value. To avoid hash collisions, rehash the 'skb_hash' with
	 * 'recirc_id'.  */
	if (key->recirc_id)
		skb_hash = jhash_1word(skb_hash, key->recirc_id);

	ce = NULL;
	hash = skb_hash;
	entries = this_cpu_ptr(mc->mask_cache);

	/* Find the cache entry 'ce' to operate on. */
	for (seg = 0; seg < MC_HASH_SEGS; seg++) {
		int index = hash & (mc->cache_size - 1);
		struct mask_cache_entry *e;

		e = &entries[index];
		if (e->skb_hash == skb_hash) {
			flow = flow_lookup(tbl, ti, ma, key, n_mask_hit,
					   n_cache_hit, &e->mask_index);
			if (!flow)
				e->skb_hash = 0;
			return flow;
		}

		if (!ce || e->skb_hash < ce->skb_hash)
			ce = e;  /* A better replacement cache candidate. */

		hash >>= MC_HASH_SHIFT;
	}

	/* Cache miss, do full lookup. */
	flow = flow_lookup(tbl, ti, ma, key, n_mask_hit, n_cache_hit,
			   &ce->mask_index);
	if (flow)
		ce->skb_hash = skb_hash;

	*n_cache_hit = 0;
	return flow;
}

struct sw_flow *ovs_flow_tbl_lookup(struct flow_table *tbl,
				    const struct sw_flow_key *key)
{
	struct table_instance *ti = rcu_dereference_ovsl(tbl->ti);
	struct mask_array *ma = rcu_dereference_ovsl(tbl->mask_array);
	u32 __always_unused n_mask_hit;
	u32 __always_unused n_cache_hit;
	u32 index = 0;

	return flow_lookup(tbl, ti, ma, key, &n_mask_hit, &n_cache_hit, &index);
}

struct sw_flow *ovs_flow_tbl_lookup_exact(struct flow_table *tbl,
					  const struct sw_flow_match *match)
{
	struct mask_array *ma = ovsl_dereference(tbl->mask_array);
	int i;

	/* Always called under ovs-mutex. */
	for (i = 0; i < ma->max; i++) {
		struct table_instance *ti = rcu_dereference_ovsl(tbl->ti);
		u32 __always_unused n_mask_hit;
		struct sw_flow_mask *mask;
		struct sw_flow *flow;

		mask = ovsl_dereference(ma->masks[i]);
		if (!mask)
			continue;

		flow = masked_flow_lookup(ti, match->key, mask, &n_mask_hit);
		if (flow && ovs_identifier_is_key(&flow->id) &&
		    ovs_flow_cmp_unmasked_key(flow, match)) {
			return flow;
		}
	}

	return NULL;
}

static u32 ufid_hash(const struct sw_flow_id *sfid)
{
	return jhash(sfid->ufid, sfid->ufid_len, 0);
}

static bool ovs_flow_cmp_ufid(const struct sw_flow *flow,
			      const struct sw_flow_id *sfid)
{
	if (flow->id.ufid_len != sfid->ufid_len)
		return false;

	return !memcmp(flow->id.ufid, sfid->ufid, sfid->ufid_len);
}

bool ovs_flow_cmp(const struct sw_flow *flow, const struct sw_flow_match *match)
{
	if (ovs_identifier_is_ufid(&flow->id))
		return flow_cmp_masked_key(flow, match->key, &match->range);

	return ovs_flow_cmp_unmasked_key(flow, match);
}

struct sw_flow *ovs_flow_tbl_lookup_ufid(struct flow_table *tbl,
					 const struct sw_flow_id *ufid)
{
	struct table_instance *ti = rcu_dereference_ovsl(tbl->ufid_ti);
	struct sw_flow *flow;
	struct hlist_head *head;
	u32 hash;

	hash = ufid_hash(ufid);
	head = find_bucket(ti, hash);
	hlist_for_each_entry_rcu(flow, head, ufid_table.node[ti->node_ver],
				lockdep_ovsl_is_held()) {
		if (flow->ufid_table.hash == hash &&
		    ovs_flow_cmp_ufid(flow, ufid))
			return flow;
	}
	return NULL;
}

int ovs_flow_tbl_num_masks(const struct flow_table *table)
{
	struct mask_array *ma = rcu_dereference_ovsl(table->mask_array);
	return READ_ONCE(ma->count);
}

u32 ovs_flow_tbl_masks_cache_size(const struct flow_table *table)
{
	struct mask_cache *mc = rcu_dereference_ovsl(table->mask_cache);

	return READ_ONCE(mc->cache_size);
}

static struct table_instance *table_instance_expand(struct table_instance *ti,
						    bool ufid)
{
	return table_instance_rehash(ti, ti->n_buckets * 2, ufid);
}

/* Must be called with OVS mutex held. */
void ovs_flow_tbl_remove(struct flow_table *table, struct sw_flow *flow)
{
	struct table_instance *ti = ovsl_dereference(table->ti);
	struct table_instance *ufid_ti = ovsl_dereference(table->ufid_ti);

	BUG_ON(table->count == 0);
	table_instance_flow_free(table, ti, ufid_ti, flow, true);
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
	struct mask_array *ma;
	int i;

	ma = ovsl_dereference(tbl->mask_array);
	for (i = 0; i < ma->max; i++) {
		struct sw_flow_mask *t;
		t = ovsl_dereference(ma->masks[i]);

		if (t && mask_equal(mask, t))
			return t;
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

		/* Add mask to mask-list. */
		if (tbl_mask_array_add_mask(tbl, mask)) {
			kfree(mask);
			return -ENOMEM;
		}
	} else {
		BUG_ON(!mask->ref_count);
		mask->ref_count++;
	}

	flow->mask = mask;
	return 0;
}

/* Must be called with OVS mutex held. */
static void flow_key_insert(struct flow_table *table, struct sw_flow *flow)
{
	struct table_instance *new_ti = NULL;
	struct table_instance *ti;

	flow->flow_table.hash = flow_hash(&flow->key, &flow->mask->range);
	ti = ovsl_dereference(table->ti);
	table_instance_insert(ti, flow);
	table->count++;

	/* Expand table, if necessary, to make room. */
	if (table->count > ti->n_buckets)
		new_ti = table_instance_expand(ti, false);
	else if (time_after(jiffies, table->last_rehash + REHASH_INTERVAL))
		new_ti = table_instance_rehash(ti, ti->n_buckets, false);

	if (new_ti) {
		rcu_assign_pointer(table->ti, new_ti);
		call_rcu(&ti->rcu, flow_tbl_destroy_rcu_cb);
		table->last_rehash = jiffies;
	}
}

/* Must be called with OVS mutex held. */
static void flow_ufid_insert(struct flow_table *table, struct sw_flow *flow)
{
	struct table_instance *ti;

	flow->ufid_table.hash = ufid_hash(&flow->id);
	ti = ovsl_dereference(table->ufid_ti);
	ufid_table_instance_insert(ti, flow);
	table->ufid_count++;

	/* Expand table, if necessary, to make room. */
	if (table->ufid_count > ti->n_buckets) {
		struct table_instance *new_ti;

		new_ti = table_instance_expand(ti, true);
		if (new_ti) {
			rcu_assign_pointer(table->ufid_ti, new_ti);
			call_rcu(&ti->rcu, flow_tbl_destroy_rcu_cb);
		}
	}
}

/* Must be called with OVS mutex held. */
int ovs_flow_tbl_insert(struct flow_table *table, struct sw_flow *flow,
			const struct sw_flow_mask *mask)
{
	int err;

	err = flow_mask_insert(table, flow, mask);
	if (err)
		return err;
	flow_key_insert(table, flow);
	if (ovs_identifier_is_ufid(&flow->id))
		flow_ufid_insert(table, flow);

	return 0;
}

static int compare_mask_and_count(const void *a, const void *b)
{
	const struct mask_count *mc_a = a;
	const struct mask_count *mc_b = b;

	return (s64)mc_b->counter - (s64)mc_a->counter;
}

/* Must be called with OVS mutex held. */
void ovs_flow_masks_rebalance(struct flow_table *table)
{
	struct mask_array *ma = rcu_dereference_ovsl(table->mask_array);
	struct mask_count *masks_and_count;
	struct mask_array *new;
	int masks_entries = 0;
	int i;

	/* Build array of all current entries with use counters. */
	masks_and_count = kmalloc_array(ma->max, sizeof(*masks_and_count),
					GFP_KERNEL);
	if (!masks_and_count)
		return;

	for (i = 0; i < ma->max; i++)  {
		struct sw_flow_mask *mask;
		unsigned int start;
		int cpu;

		mask = rcu_dereference_ovsl(ma->masks[i]);
		if (unlikely(!mask))
			break;

		masks_and_count[i].index = i;
		masks_and_count[i].counter = 0;

		for_each_possible_cpu(cpu) {
			u64 *usage_counters = per_cpu_ptr(ma->masks_usage_cntr,
							  cpu);
			u64 counter;

			do {
				start = u64_stats_fetch_begin_irq(&ma->syncp);
				counter = usage_counters[i];
			} while (u64_stats_fetch_retry_irq(&ma->syncp, start));

			masks_and_count[i].counter += counter;
		}

		/* Subtract the zero count value. */
		masks_and_count[i].counter -= ma->masks_usage_zero_cntr[i];

		/* Rather than calling tbl_mask_array_reset_counters()
		 * below when no change is needed, do it inline here.
		 */
		ma->masks_usage_zero_cntr[i] += masks_and_count[i].counter;
	}

	if (i == 0)
		goto free_mask_entries;

	/* Sort the entries */
	masks_entries = i;
	sort(masks_and_count, masks_entries, sizeof(*masks_and_count),
	     compare_mask_and_count, NULL);

	/* If the order is the same, nothing to do... */
	for (i = 0; i < masks_entries; i++) {
		if (i != masks_and_count[i].index)
			break;
	}
	if (i == masks_entries)
		goto free_mask_entries;

	/* Rebuilt the new list in order of usage. */
	new = tbl_mask_array_alloc(ma->max);
	if (!new)
		goto free_mask_entries;

	for (i = 0; i < masks_entries; i++) {
		int index = masks_and_count[i].index;

		if (ovsl_dereference(ma->masks[index]))
			new->masks[new->count++] = ma->masks[index];
	}

	rcu_assign_pointer(table->mask_array, new);
	call_rcu(&ma->rcu, mask_array_rcu_cb);

free_mask_entries:
	kfree(masks_and_count);
}

/* Initializes the flow module.
 * Returns zero if successful or a negative error code. */
int ovs_flow_init(void)
{
	BUILD_BUG_ON(__alignof__(struct sw_flow_key) % __alignof__(long));
	BUILD_BUG_ON(sizeof(struct sw_flow_key) % sizeof(long));

	flow_cache = kmem_cache_create("sw_flow", sizeof(struct sw_flow)
				       + (nr_cpu_ids
					  * sizeof(struct sw_flow_stats *)),
				       0, 0, NULL);
	if (flow_cache == NULL)
		return -ENOMEM;

	flow_stats_cache
		= kmem_cache_create("sw_flow_stats", sizeof(struct sw_flow_stats),
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
