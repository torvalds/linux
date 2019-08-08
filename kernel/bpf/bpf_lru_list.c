// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2016 Facebook
 */
#include <linux/cpumask.h>
#include <linux/spinlock.h>
#include <linux/percpu.h>

#include "bpf_lru_list.h"

#define LOCAL_FREE_TARGET		(128)
#define LOCAL_NR_SCANS			LOCAL_FREE_TARGET

#define PERCPU_FREE_TARGET		(4)
#define PERCPU_NR_SCANS			PERCPU_FREE_TARGET

/* Helpers to get the local list index */
#define LOCAL_LIST_IDX(t)	((t) - BPF_LOCAL_LIST_T_OFFSET)
#define LOCAL_FREE_LIST_IDX	LOCAL_LIST_IDX(BPF_LRU_LOCAL_LIST_T_FREE)
#define LOCAL_PENDING_LIST_IDX	LOCAL_LIST_IDX(BPF_LRU_LOCAL_LIST_T_PENDING)
#define IS_LOCAL_LIST_TYPE(t)	((t) >= BPF_LOCAL_LIST_T_OFFSET)

static int get_next_cpu(int cpu)
{
	cpu = cpumask_next(cpu, cpu_possible_mask);
	if (cpu >= nr_cpu_ids)
		cpu = cpumask_first(cpu_possible_mask);
	return cpu;
}

/* Local list helpers */
static struct list_head *local_free_list(struct bpf_lru_locallist *loc_l)
{
	return &loc_l->lists[LOCAL_FREE_LIST_IDX];
}

static struct list_head *local_pending_list(struct bpf_lru_locallist *loc_l)
{
	return &loc_l->lists[LOCAL_PENDING_LIST_IDX];
}

/* bpf_lru_node helpers */
static bool bpf_lru_node_is_ref(const struct bpf_lru_node *node)
{
	return node->ref;
}

static void bpf_lru_list_count_inc(struct bpf_lru_list *l,
				   enum bpf_lru_list_type type)
{
	if (type < NR_BPF_LRU_LIST_COUNT)
		l->counts[type]++;
}

static void bpf_lru_list_count_dec(struct bpf_lru_list *l,
				   enum bpf_lru_list_type type)
{
	if (type < NR_BPF_LRU_LIST_COUNT)
		l->counts[type]--;
}

static void __bpf_lru_node_move_to_free(struct bpf_lru_list *l,
					struct bpf_lru_node *node,
					struct list_head *free_list,
					enum bpf_lru_list_type tgt_free_type)
{
	if (WARN_ON_ONCE(IS_LOCAL_LIST_TYPE(node->type)))
		return;

	/* If the removing node is the next_inactive_rotation candidate,
	 * move the next_inactive_rotation pointer also.
	 */
	if (&node->list == l->next_inactive_rotation)
		l->next_inactive_rotation = l->next_inactive_rotation->prev;

	bpf_lru_list_count_dec(l, node->type);

	node->type = tgt_free_type;
	list_move(&node->list, free_list);
}

/* Move nodes from local list to the LRU list */
static void __bpf_lru_node_move_in(struct bpf_lru_list *l,
				   struct bpf_lru_node *node,
				   enum bpf_lru_list_type tgt_type)
{
	if (WARN_ON_ONCE(!IS_LOCAL_LIST_TYPE(node->type)) ||
	    WARN_ON_ONCE(IS_LOCAL_LIST_TYPE(tgt_type)))
		return;

	bpf_lru_list_count_inc(l, tgt_type);
	node->type = tgt_type;
	node->ref = 0;
	list_move(&node->list, &l->lists[tgt_type]);
}

/* Move nodes between or within active and inactive list (like
 * active to inactive, inactive to active or tail of active back to
 * the head of active).
 */
static void __bpf_lru_node_move(struct bpf_lru_list *l,
				struct bpf_lru_node *node,
				enum bpf_lru_list_type tgt_type)
{
	if (WARN_ON_ONCE(IS_LOCAL_LIST_TYPE(node->type)) ||
	    WARN_ON_ONCE(IS_LOCAL_LIST_TYPE(tgt_type)))
		return;

	if (node->type != tgt_type) {
		bpf_lru_list_count_dec(l, node->type);
		bpf_lru_list_count_inc(l, tgt_type);
		node->type = tgt_type;
	}
	node->ref = 0;

	/* If the moving node is the next_inactive_rotation candidate,
	 * move the next_inactive_rotation pointer also.
	 */
	if (&node->list == l->next_inactive_rotation)
		l->next_inactive_rotation = l->next_inactive_rotation->prev;

	list_move(&node->list, &l->lists[tgt_type]);
}

static bool bpf_lru_list_inactive_low(const struct bpf_lru_list *l)
{
	return l->counts[BPF_LRU_LIST_T_INACTIVE] <
		l->counts[BPF_LRU_LIST_T_ACTIVE];
}

/* Rotate the active list:
 * 1. Start from tail
 * 2. If the node has the ref bit set, it will be rotated
 *    back to the head of active list with the ref bit cleared.
 *    Give this node one more chance to survive in the active list.
 * 3. If the ref bit is not set, move it to the head of the
 *    inactive list.
 * 4. It will at most scan nr_scans nodes
 */
static void __bpf_lru_list_rotate_active(struct bpf_lru *lru,
					 struct bpf_lru_list *l)
{
	struct list_head *active = &l->lists[BPF_LRU_LIST_T_ACTIVE];
	struct bpf_lru_node *node, *tmp_node, *first_node;
	unsigned int i = 0;

	first_node = list_first_entry(active, struct bpf_lru_node, list);
	list_for_each_entry_safe_reverse(node, tmp_node, active, list) {
		if (bpf_lru_node_is_ref(node))
			__bpf_lru_node_move(l, node, BPF_LRU_LIST_T_ACTIVE);
		else
			__bpf_lru_node_move(l, node, BPF_LRU_LIST_T_INACTIVE);

		if (++i == lru->nr_scans || node == first_node)
			break;
	}
}

/* Rotate the inactive list.  It starts from the next_inactive_rotation
 * 1. If the node has ref bit set, it will be moved to the head
 *    of active list with the ref bit cleared.
 * 2. If the node does not have ref bit set, it will leave it
 *    at its current location (i.e. do nothing) so that it can
 *    be considered during the next inactive_shrink.
 * 3. It will at most scan nr_scans nodes
 */
static void __bpf_lru_list_rotate_inactive(struct bpf_lru *lru,
					   struct bpf_lru_list *l)
{
	struct list_head *inactive = &l->lists[BPF_LRU_LIST_T_INACTIVE];
	struct list_head *cur, *last, *next = inactive;
	struct bpf_lru_node *node;
	unsigned int i = 0;

	if (list_empty(inactive))
		return;

	last = l->next_inactive_rotation->next;
	if (last == inactive)
		last = last->next;

	cur = l->next_inactive_rotation;
	while (i < lru->nr_scans) {
		if (cur == inactive) {
			cur = cur->prev;
			continue;
		}

		node = list_entry(cur, struct bpf_lru_node, list);
		next = cur->prev;
		if (bpf_lru_node_is_ref(node))
			__bpf_lru_node_move(l, node, BPF_LRU_LIST_T_ACTIVE);
		if (cur == last)
			break;
		cur = next;
		i++;
	}

	l->next_inactive_rotation = next;
}

/* Shrink the inactive list.  It starts from the tail of the
 * inactive list and only move the nodes without the ref bit
 * set to the designated free list.
 */
static unsigned int
__bpf_lru_list_shrink_inactive(struct bpf_lru *lru,
			       struct bpf_lru_list *l,
			       unsigned int tgt_nshrink,
			       struct list_head *free_list,
			       enum bpf_lru_list_type tgt_free_type)
{
	struct list_head *inactive = &l->lists[BPF_LRU_LIST_T_INACTIVE];
	struct bpf_lru_node *node, *tmp_node;
	unsigned int nshrinked = 0;
	unsigned int i = 0;

	list_for_each_entry_safe_reverse(node, tmp_node, inactive, list) {
		if (bpf_lru_node_is_ref(node)) {
			__bpf_lru_node_move(l, node, BPF_LRU_LIST_T_ACTIVE);
		} else if (lru->del_from_htab(lru->del_arg, node)) {
			__bpf_lru_node_move_to_free(l, node, free_list,
						    tgt_free_type);
			if (++nshrinked == tgt_nshrink)
				break;
		}

		if (++i == lru->nr_scans)
			break;
	}

	return nshrinked;
}

/* 1. Rotate the active list (if needed)
 * 2. Always rotate the inactive list
 */
static void __bpf_lru_list_rotate(struct bpf_lru *lru, struct bpf_lru_list *l)
{
	if (bpf_lru_list_inactive_low(l))
		__bpf_lru_list_rotate_active(lru, l);

	__bpf_lru_list_rotate_inactive(lru, l);
}

/* Calls __bpf_lru_list_shrink_inactive() to shrink some
 * ref-bit-cleared nodes and move them to the designated
 * free list.
 *
 * If it cannot get a free node after calling
 * __bpf_lru_list_shrink_inactive().  It will just remove
 * one node from either inactive or active list without
 * honoring the ref-bit.  It prefers inactive list to active
 * list in this situation.
 */
static unsigned int __bpf_lru_list_shrink(struct bpf_lru *lru,
					  struct bpf_lru_list *l,
					  unsigned int tgt_nshrink,
					  struct list_head *free_list,
					  enum bpf_lru_list_type tgt_free_type)

{
	struct bpf_lru_node *node, *tmp_node;
	struct list_head *force_shrink_list;
	unsigned int nshrinked;

	nshrinked = __bpf_lru_list_shrink_inactive(lru, l, tgt_nshrink,
						   free_list, tgt_free_type);
	if (nshrinked)
		return nshrinked;

	/* Do a force shrink by ignoring the reference bit */
	if (!list_empty(&l->lists[BPF_LRU_LIST_T_INACTIVE]))
		force_shrink_list = &l->lists[BPF_LRU_LIST_T_INACTIVE];
	else
		force_shrink_list = &l->lists[BPF_LRU_LIST_T_ACTIVE];

	list_for_each_entry_safe_reverse(node, tmp_node, force_shrink_list,
					 list) {
		if (lru->del_from_htab(lru->del_arg, node)) {
			__bpf_lru_node_move_to_free(l, node, free_list,
						    tgt_free_type);
			return 1;
		}
	}

	return 0;
}

/* Flush the nodes from the local pending list to the LRU list */
static void __local_list_flush(struct bpf_lru_list *l,
			       struct bpf_lru_locallist *loc_l)
{
	struct bpf_lru_node *node, *tmp_node;

	list_for_each_entry_safe_reverse(node, tmp_node,
					 local_pending_list(loc_l), list) {
		if (bpf_lru_node_is_ref(node))
			__bpf_lru_node_move_in(l, node, BPF_LRU_LIST_T_ACTIVE);
		else
			__bpf_lru_node_move_in(l, node,
					       BPF_LRU_LIST_T_INACTIVE);
	}
}

static void bpf_lru_list_push_free(struct bpf_lru_list *l,
				   struct bpf_lru_node *node)
{
	unsigned long flags;

	if (WARN_ON_ONCE(IS_LOCAL_LIST_TYPE(node->type)))
		return;

	raw_spin_lock_irqsave(&l->lock, flags);
	__bpf_lru_node_move(l, node, BPF_LRU_LIST_T_FREE);
	raw_spin_unlock_irqrestore(&l->lock, flags);
}

static void bpf_lru_list_pop_free_to_local(struct bpf_lru *lru,
					   struct bpf_lru_locallist *loc_l)
{
	struct bpf_lru_list *l = &lru->common_lru.lru_list;
	struct bpf_lru_node *node, *tmp_node;
	unsigned int nfree = 0;

	raw_spin_lock(&l->lock);

	__local_list_flush(l, loc_l);

	__bpf_lru_list_rotate(lru, l);

	list_for_each_entry_safe(node, tmp_node, &l->lists[BPF_LRU_LIST_T_FREE],
				 list) {
		__bpf_lru_node_move_to_free(l, node, local_free_list(loc_l),
					    BPF_LRU_LOCAL_LIST_T_FREE);
		if (++nfree == LOCAL_FREE_TARGET)
			break;
	}

	if (nfree < LOCAL_FREE_TARGET)
		__bpf_lru_list_shrink(lru, l, LOCAL_FREE_TARGET - nfree,
				      local_free_list(loc_l),
				      BPF_LRU_LOCAL_LIST_T_FREE);

	raw_spin_unlock(&l->lock);
}

static void __local_list_add_pending(struct bpf_lru *lru,
				     struct bpf_lru_locallist *loc_l,
				     int cpu,
				     struct bpf_lru_node *node,
				     u32 hash)
{
	*(u32 *)((void *)node + lru->hash_offset) = hash;
	node->cpu = cpu;
	node->type = BPF_LRU_LOCAL_LIST_T_PENDING;
	node->ref = 0;
	list_add(&node->list, local_pending_list(loc_l));
}

static struct bpf_lru_node *
__local_list_pop_free(struct bpf_lru_locallist *loc_l)
{
	struct bpf_lru_node *node;

	node = list_first_entry_or_null(local_free_list(loc_l),
					struct bpf_lru_node,
					list);
	if (node)
		list_del(&node->list);

	return node;
}

static struct bpf_lru_node *
__local_list_pop_pending(struct bpf_lru *lru, struct bpf_lru_locallist *loc_l)
{
	struct bpf_lru_node *node;
	bool force = false;

ignore_ref:
	/* Get from the tail (i.e. older element) of the pending list. */
	list_for_each_entry_reverse(node, local_pending_list(loc_l),
				    list) {
		if ((!bpf_lru_node_is_ref(node) || force) &&
		    lru->del_from_htab(lru->del_arg, node)) {
			list_del(&node->list);
			return node;
		}
	}

	if (!force) {
		force = true;
		goto ignore_ref;
	}

	return NULL;
}

static struct bpf_lru_node *bpf_percpu_lru_pop_free(struct bpf_lru *lru,
						    u32 hash)
{
	struct list_head *free_list;
	struct bpf_lru_node *node = NULL;
	struct bpf_lru_list *l;
	unsigned long flags;
	int cpu = raw_smp_processor_id();

	l = per_cpu_ptr(lru->percpu_lru, cpu);

	raw_spin_lock_irqsave(&l->lock, flags);

	__bpf_lru_list_rotate(lru, l);

	free_list = &l->lists[BPF_LRU_LIST_T_FREE];
	if (list_empty(free_list))
		__bpf_lru_list_shrink(lru, l, PERCPU_FREE_TARGET, free_list,
				      BPF_LRU_LIST_T_FREE);

	if (!list_empty(free_list)) {
		node = list_first_entry(free_list, struct bpf_lru_node, list);
		*(u32 *)((void *)node + lru->hash_offset) = hash;
		node->ref = 0;
		__bpf_lru_node_move(l, node, BPF_LRU_LIST_T_INACTIVE);
	}

	raw_spin_unlock_irqrestore(&l->lock, flags);

	return node;
}

static struct bpf_lru_node *bpf_common_lru_pop_free(struct bpf_lru *lru,
						    u32 hash)
{
	struct bpf_lru_locallist *loc_l, *steal_loc_l;
	struct bpf_common_lru *clru = &lru->common_lru;
	struct bpf_lru_node *node;
	int steal, first_steal;
	unsigned long flags;
	int cpu = raw_smp_processor_id();

	loc_l = per_cpu_ptr(clru->local_list, cpu);

	raw_spin_lock_irqsave(&loc_l->lock, flags);

	node = __local_list_pop_free(loc_l);
	if (!node) {
		bpf_lru_list_pop_free_to_local(lru, loc_l);
		node = __local_list_pop_free(loc_l);
	}

	if (node)
		__local_list_add_pending(lru, loc_l, cpu, node, hash);

	raw_spin_unlock_irqrestore(&loc_l->lock, flags);

	if (node)
		return node;

	/* No free nodes found from the local free list and
	 * the global LRU list.
	 *
	 * Steal from the local free/pending list of the
	 * current CPU and remote CPU in RR.  It starts
	 * with the loc_l->next_steal CPU.
	 */

	first_steal = loc_l->next_steal;
	steal = first_steal;
	do {
		steal_loc_l = per_cpu_ptr(clru->local_list, steal);

		raw_spin_lock_irqsave(&steal_loc_l->lock, flags);

		node = __local_list_pop_free(steal_loc_l);
		if (!node)
			node = __local_list_pop_pending(lru, steal_loc_l);

		raw_spin_unlock_irqrestore(&steal_loc_l->lock, flags);

		steal = get_next_cpu(steal);
	} while (!node && steal != first_steal);

	loc_l->next_steal = steal;

	if (node) {
		raw_spin_lock_irqsave(&loc_l->lock, flags);
		__local_list_add_pending(lru, loc_l, cpu, node, hash);
		raw_spin_unlock_irqrestore(&loc_l->lock, flags);
	}

	return node;
}

struct bpf_lru_node *bpf_lru_pop_free(struct bpf_lru *lru, u32 hash)
{
	if (lru->percpu)
		return bpf_percpu_lru_pop_free(lru, hash);
	else
		return bpf_common_lru_pop_free(lru, hash);
}

static void bpf_common_lru_push_free(struct bpf_lru *lru,
				     struct bpf_lru_node *node)
{
	unsigned long flags;

	if (WARN_ON_ONCE(node->type == BPF_LRU_LIST_T_FREE) ||
	    WARN_ON_ONCE(node->type == BPF_LRU_LOCAL_LIST_T_FREE))
		return;

	if (node->type == BPF_LRU_LOCAL_LIST_T_PENDING) {
		struct bpf_lru_locallist *loc_l;

		loc_l = per_cpu_ptr(lru->common_lru.local_list, node->cpu);

		raw_spin_lock_irqsave(&loc_l->lock, flags);

		if (unlikely(node->type != BPF_LRU_LOCAL_LIST_T_PENDING)) {
			raw_spin_unlock_irqrestore(&loc_l->lock, flags);
			goto check_lru_list;
		}

		node->type = BPF_LRU_LOCAL_LIST_T_FREE;
		node->ref = 0;
		list_move(&node->list, local_free_list(loc_l));

		raw_spin_unlock_irqrestore(&loc_l->lock, flags);
		return;
	}

check_lru_list:
	bpf_lru_list_push_free(&lru->common_lru.lru_list, node);
}

static void bpf_percpu_lru_push_free(struct bpf_lru *lru,
				     struct bpf_lru_node *node)
{
	struct bpf_lru_list *l;
	unsigned long flags;

	l = per_cpu_ptr(lru->percpu_lru, node->cpu);

	raw_spin_lock_irqsave(&l->lock, flags);

	__bpf_lru_node_move(l, node, BPF_LRU_LIST_T_FREE);

	raw_spin_unlock_irqrestore(&l->lock, flags);
}

void bpf_lru_push_free(struct bpf_lru *lru, struct bpf_lru_node *node)
{
	if (lru->percpu)
		bpf_percpu_lru_push_free(lru, node);
	else
		bpf_common_lru_push_free(lru, node);
}

static void bpf_common_lru_populate(struct bpf_lru *lru, void *buf,
				    u32 node_offset, u32 elem_size,
				    u32 nr_elems)
{
	struct bpf_lru_list *l = &lru->common_lru.lru_list;
	u32 i;

	for (i = 0; i < nr_elems; i++) {
		struct bpf_lru_node *node;

		node = (struct bpf_lru_node *)(buf + node_offset);
		node->type = BPF_LRU_LIST_T_FREE;
		node->ref = 0;
		list_add(&node->list, &l->lists[BPF_LRU_LIST_T_FREE]);
		buf += elem_size;
	}
}

static void bpf_percpu_lru_populate(struct bpf_lru *lru, void *buf,
				    u32 node_offset, u32 elem_size,
				    u32 nr_elems)
{
	u32 i, pcpu_entries;
	int cpu;
	struct bpf_lru_list *l;

	pcpu_entries = nr_elems / num_possible_cpus();

	i = 0;

	for_each_possible_cpu(cpu) {
		struct bpf_lru_node *node;

		l = per_cpu_ptr(lru->percpu_lru, cpu);
again:
		node = (struct bpf_lru_node *)(buf + node_offset);
		node->cpu = cpu;
		node->type = BPF_LRU_LIST_T_FREE;
		node->ref = 0;
		list_add(&node->list, &l->lists[BPF_LRU_LIST_T_FREE]);
		i++;
		buf += elem_size;
		if (i == nr_elems)
			break;
		if (i % pcpu_entries)
			goto again;
	}
}

void bpf_lru_populate(struct bpf_lru *lru, void *buf, u32 node_offset,
		      u32 elem_size, u32 nr_elems)
{
	if (lru->percpu)
		bpf_percpu_lru_populate(lru, buf, node_offset, elem_size,
					nr_elems);
	else
		bpf_common_lru_populate(lru, buf, node_offset, elem_size,
					nr_elems);
}

static void bpf_lru_locallist_init(struct bpf_lru_locallist *loc_l, int cpu)
{
	int i;

	for (i = 0; i < NR_BPF_LRU_LOCAL_LIST_T; i++)
		INIT_LIST_HEAD(&loc_l->lists[i]);

	loc_l->next_steal = cpu;

	raw_spin_lock_init(&loc_l->lock);
}

static void bpf_lru_list_init(struct bpf_lru_list *l)
{
	int i;

	for (i = 0; i < NR_BPF_LRU_LIST_T; i++)
		INIT_LIST_HEAD(&l->lists[i]);

	for (i = 0; i < NR_BPF_LRU_LIST_COUNT; i++)
		l->counts[i] = 0;

	l->next_inactive_rotation = &l->lists[BPF_LRU_LIST_T_INACTIVE];

	raw_spin_lock_init(&l->lock);
}

int bpf_lru_init(struct bpf_lru *lru, bool percpu, u32 hash_offset,
		 del_from_htab_func del_from_htab, void *del_arg)
{
	int cpu;

	if (percpu) {
		lru->percpu_lru = alloc_percpu(struct bpf_lru_list);
		if (!lru->percpu_lru)
			return -ENOMEM;

		for_each_possible_cpu(cpu) {
			struct bpf_lru_list *l;

			l = per_cpu_ptr(lru->percpu_lru, cpu);
			bpf_lru_list_init(l);
		}
		lru->nr_scans = PERCPU_NR_SCANS;
	} else {
		struct bpf_common_lru *clru = &lru->common_lru;

		clru->local_list = alloc_percpu(struct bpf_lru_locallist);
		if (!clru->local_list)
			return -ENOMEM;

		for_each_possible_cpu(cpu) {
			struct bpf_lru_locallist *loc_l;

			loc_l = per_cpu_ptr(clru->local_list, cpu);
			bpf_lru_locallist_init(loc_l, cpu);
		}

		bpf_lru_list_init(&clru->lru_list);
		lru->nr_scans = LOCAL_NR_SCANS;
	}

	lru->percpu = percpu;
	lru->del_from_htab = del_from_htab;
	lru->del_arg = del_arg;
	lru->hash_offset = hash_offset;

	return 0;
}

void bpf_lru_destroy(struct bpf_lru *lru)
{
	if (lru->percpu)
		free_percpu(lru->percpu_lru);
	else
		free_percpu(lru->common_lru.local_list);
}
