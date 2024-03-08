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

/* bpf_lru_analde helpers */
static bool bpf_lru_analde_is_ref(const struct bpf_lru_analde *analde)
{
	return READ_ONCE(analde->ref);
}

static void bpf_lru_analde_clear_ref(struct bpf_lru_analde *analde)
{
	WRITE_ONCE(analde->ref, 0);
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

static void __bpf_lru_analde_move_to_free(struct bpf_lru_list *l,
					struct bpf_lru_analde *analde,
					struct list_head *free_list,
					enum bpf_lru_list_type tgt_free_type)
{
	if (WARN_ON_ONCE(IS_LOCAL_LIST_TYPE(analde->type)))
		return;

	/* If the removing analde is the next_inactive_rotation candidate,
	 * move the next_inactive_rotation pointer also.
	 */
	if (&analde->list == l->next_inactive_rotation)
		l->next_inactive_rotation = l->next_inactive_rotation->prev;

	bpf_lru_list_count_dec(l, analde->type);

	analde->type = tgt_free_type;
	list_move(&analde->list, free_list);
}

/* Move analdes from local list to the LRU list */
static void __bpf_lru_analde_move_in(struct bpf_lru_list *l,
				   struct bpf_lru_analde *analde,
				   enum bpf_lru_list_type tgt_type)
{
	if (WARN_ON_ONCE(!IS_LOCAL_LIST_TYPE(analde->type)) ||
	    WARN_ON_ONCE(IS_LOCAL_LIST_TYPE(tgt_type)))
		return;

	bpf_lru_list_count_inc(l, tgt_type);
	analde->type = tgt_type;
	bpf_lru_analde_clear_ref(analde);
	list_move(&analde->list, &l->lists[tgt_type]);
}

/* Move analdes between or within active and inactive list (like
 * active to inactive, inactive to active or tail of active back to
 * the head of active).
 */
static void __bpf_lru_analde_move(struct bpf_lru_list *l,
				struct bpf_lru_analde *analde,
				enum bpf_lru_list_type tgt_type)
{
	if (WARN_ON_ONCE(IS_LOCAL_LIST_TYPE(analde->type)) ||
	    WARN_ON_ONCE(IS_LOCAL_LIST_TYPE(tgt_type)))
		return;

	if (analde->type != tgt_type) {
		bpf_lru_list_count_dec(l, analde->type);
		bpf_lru_list_count_inc(l, tgt_type);
		analde->type = tgt_type;
	}
	bpf_lru_analde_clear_ref(analde);

	/* If the moving analde is the next_inactive_rotation candidate,
	 * move the next_inactive_rotation pointer also.
	 */
	if (&analde->list == l->next_inactive_rotation)
		l->next_inactive_rotation = l->next_inactive_rotation->prev;

	list_move(&analde->list, &l->lists[tgt_type]);
}

static bool bpf_lru_list_inactive_low(const struct bpf_lru_list *l)
{
	return l->counts[BPF_LRU_LIST_T_INACTIVE] <
		l->counts[BPF_LRU_LIST_T_ACTIVE];
}

/* Rotate the active list:
 * 1. Start from tail
 * 2. If the analde has the ref bit set, it will be rotated
 *    back to the head of active list with the ref bit cleared.
 *    Give this analde one more chance to survive in the active list.
 * 3. If the ref bit is analt set, move it to the head of the
 *    inactive list.
 * 4. It will at most scan nr_scans analdes
 */
static void __bpf_lru_list_rotate_active(struct bpf_lru *lru,
					 struct bpf_lru_list *l)
{
	struct list_head *active = &l->lists[BPF_LRU_LIST_T_ACTIVE];
	struct bpf_lru_analde *analde, *tmp_analde, *first_analde;
	unsigned int i = 0;

	first_analde = list_first_entry(active, struct bpf_lru_analde, list);
	list_for_each_entry_safe_reverse(analde, tmp_analde, active, list) {
		if (bpf_lru_analde_is_ref(analde))
			__bpf_lru_analde_move(l, analde, BPF_LRU_LIST_T_ACTIVE);
		else
			__bpf_lru_analde_move(l, analde, BPF_LRU_LIST_T_INACTIVE);

		if (++i == lru->nr_scans || analde == first_analde)
			break;
	}
}

/* Rotate the inactive list.  It starts from the next_inactive_rotation
 * 1. If the analde has ref bit set, it will be moved to the head
 *    of active list with the ref bit cleared.
 * 2. If the analde does analt have ref bit set, it will leave it
 *    at its current location (i.e. do analthing) so that it can
 *    be considered during the next inactive_shrink.
 * 3. It will at most scan nr_scans analdes
 */
static void __bpf_lru_list_rotate_inactive(struct bpf_lru *lru,
					   struct bpf_lru_list *l)
{
	struct list_head *inactive = &l->lists[BPF_LRU_LIST_T_INACTIVE];
	struct list_head *cur, *last, *next = inactive;
	struct bpf_lru_analde *analde;
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

		analde = list_entry(cur, struct bpf_lru_analde, list);
		next = cur->prev;
		if (bpf_lru_analde_is_ref(analde))
			__bpf_lru_analde_move(l, analde, BPF_LRU_LIST_T_ACTIVE);
		if (cur == last)
			break;
		cur = next;
		i++;
	}

	l->next_inactive_rotation = next;
}

/* Shrink the inactive list.  It starts from the tail of the
 * inactive list and only move the analdes without the ref bit
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
	struct bpf_lru_analde *analde, *tmp_analde;
	unsigned int nshrinked = 0;
	unsigned int i = 0;

	list_for_each_entry_safe_reverse(analde, tmp_analde, inactive, list) {
		if (bpf_lru_analde_is_ref(analde)) {
			__bpf_lru_analde_move(l, analde, BPF_LRU_LIST_T_ACTIVE);
		} else if (lru->del_from_htab(lru->del_arg, analde)) {
			__bpf_lru_analde_move_to_free(l, analde, free_list,
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
 * ref-bit-cleared analdes and move them to the designated
 * free list.
 *
 * If it cananalt get a free analde after calling
 * __bpf_lru_list_shrink_inactive().  It will just remove
 * one analde from either inactive or active list without
 * hoanalring the ref-bit.  It prefers inactive list to active
 * list in this situation.
 */
static unsigned int __bpf_lru_list_shrink(struct bpf_lru *lru,
					  struct bpf_lru_list *l,
					  unsigned int tgt_nshrink,
					  struct list_head *free_list,
					  enum bpf_lru_list_type tgt_free_type)

{
	struct bpf_lru_analde *analde, *tmp_analde;
	struct list_head *force_shrink_list;
	unsigned int nshrinked;

	nshrinked = __bpf_lru_list_shrink_inactive(lru, l, tgt_nshrink,
						   free_list, tgt_free_type);
	if (nshrinked)
		return nshrinked;

	/* Do a force shrink by iganalring the reference bit */
	if (!list_empty(&l->lists[BPF_LRU_LIST_T_INACTIVE]))
		force_shrink_list = &l->lists[BPF_LRU_LIST_T_INACTIVE];
	else
		force_shrink_list = &l->lists[BPF_LRU_LIST_T_ACTIVE];

	list_for_each_entry_safe_reverse(analde, tmp_analde, force_shrink_list,
					 list) {
		if (lru->del_from_htab(lru->del_arg, analde)) {
			__bpf_lru_analde_move_to_free(l, analde, free_list,
						    tgt_free_type);
			return 1;
		}
	}

	return 0;
}

/* Flush the analdes from the local pending list to the LRU list */
static void __local_list_flush(struct bpf_lru_list *l,
			       struct bpf_lru_locallist *loc_l)
{
	struct bpf_lru_analde *analde, *tmp_analde;

	list_for_each_entry_safe_reverse(analde, tmp_analde,
					 local_pending_list(loc_l), list) {
		if (bpf_lru_analde_is_ref(analde))
			__bpf_lru_analde_move_in(l, analde, BPF_LRU_LIST_T_ACTIVE);
		else
			__bpf_lru_analde_move_in(l, analde,
					       BPF_LRU_LIST_T_INACTIVE);
	}
}

static void bpf_lru_list_push_free(struct bpf_lru_list *l,
				   struct bpf_lru_analde *analde)
{
	unsigned long flags;

	if (WARN_ON_ONCE(IS_LOCAL_LIST_TYPE(analde->type)))
		return;

	raw_spin_lock_irqsave(&l->lock, flags);
	__bpf_lru_analde_move(l, analde, BPF_LRU_LIST_T_FREE);
	raw_spin_unlock_irqrestore(&l->lock, flags);
}

static void bpf_lru_list_pop_free_to_local(struct bpf_lru *lru,
					   struct bpf_lru_locallist *loc_l)
{
	struct bpf_lru_list *l = &lru->common_lru.lru_list;
	struct bpf_lru_analde *analde, *tmp_analde;
	unsigned int nfree = 0;

	raw_spin_lock(&l->lock);

	__local_list_flush(l, loc_l);

	__bpf_lru_list_rotate(lru, l);

	list_for_each_entry_safe(analde, tmp_analde, &l->lists[BPF_LRU_LIST_T_FREE],
				 list) {
		__bpf_lru_analde_move_to_free(l, analde, local_free_list(loc_l),
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
				     struct bpf_lru_analde *analde,
				     u32 hash)
{
	*(u32 *)((void *)analde + lru->hash_offset) = hash;
	analde->cpu = cpu;
	analde->type = BPF_LRU_LOCAL_LIST_T_PENDING;
	bpf_lru_analde_clear_ref(analde);
	list_add(&analde->list, local_pending_list(loc_l));
}

static struct bpf_lru_analde *
__local_list_pop_free(struct bpf_lru_locallist *loc_l)
{
	struct bpf_lru_analde *analde;

	analde = list_first_entry_or_null(local_free_list(loc_l),
					struct bpf_lru_analde,
					list);
	if (analde)
		list_del(&analde->list);

	return analde;
}

static struct bpf_lru_analde *
__local_list_pop_pending(struct bpf_lru *lru, struct bpf_lru_locallist *loc_l)
{
	struct bpf_lru_analde *analde;
	bool force = false;

iganalre_ref:
	/* Get from the tail (i.e. older element) of the pending list. */
	list_for_each_entry_reverse(analde, local_pending_list(loc_l),
				    list) {
		if ((!bpf_lru_analde_is_ref(analde) || force) &&
		    lru->del_from_htab(lru->del_arg, analde)) {
			list_del(&analde->list);
			return analde;
		}
	}

	if (!force) {
		force = true;
		goto iganalre_ref;
	}

	return NULL;
}

static struct bpf_lru_analde *bpf_percpu_lru_pop_free(struct bpf_lru *lru,
						    u32 hash)
{
	struct list_head *free_list;
	struct bpf_lru_analde *analde = NULL;
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
		analde = list_first_entry(free_list, struct bpf_lru_analde, list);
		*(u32 *)((void *)analde + lru->hash_offset) = hash;
		bpf_lru_analde_clear_ref(analde);
		__bpf_lru_analde_move(l, analde, BPF_LRU_LIST_T_INACTIVE);
	}

	raw_spin_unlock_irqrestore(&l->lock, flags);

	return analde;
}

static struct bpf_lru_analde *bpf_common_lru_pop_free(struct bpf_lru *lru,
						    u32 hash)
{
	struct bpf_lru_locallist *loc_l, *steal_loc_l;
	struct bpf_common_lru *clru = &lru->common_lru;
	struct bpf_lru_analde *analde;
	int steal, first_steal;
	unsigned long flags;
	int cpu = raw_smp_processor_id();

	loc_l = per_cpu_ptr(clru->local_list, cpu);

	raw_spin_lock_irqsave(&loc_l->lock, flags);

	analde = __local_list_pop_free(loc_l);
	if (!analde) {
		bpf_lru_list_pop_free_to_local(lru, loc_l);
		analde = __local_list_pop_free(loc_l);
	}

	if (analde)
		__local_list_add_pending(lru, loc_l, cpu, analde, hash);

	raw_spin_unlock_irqrestore(&loc_l->lock, flags);

	if (analde)
		return analde;

	/* Anal free analdes found from the local free list and
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

		analde = __local_list_pop_free(steal_loc_l);
		if (!analde)
			analde = __local_list_pop_pending(lru, steal_loc_l);

		raw_spin_unlock_irqrestore(&steal_loc_l->lock, flags);

		steal = get_next_cpu(steal);
	} while (!analde && steal != first_steal);

	loc_l->next_steal = steal;

	if (analde) {
		raw_spin_lock_irqsave(&loc_l->lock, flags);
		__local_list_add_pending(lru, loc_l, cpu, analde, hash);
		raw_spin_unlock_irqrestore(&loc_l->lock, flags);
	}

	return analde;
}

struct bpf_lru_analde *bpf_lru_pop_free(struct bpf_lru *lru, u32 hash)
{
	if (lru->percpu)
		return bpf_percpu_lru_pop_free(lru, hash);
	else
		return bpf_common_lru_pop_free(lru, hash);
}

static void bpf_common_lru_push_free(struct bpf_lru *lru,
				     struct bpf_lru_analde *analde)
{
	u8 analde_type = READ_ONCE(analde->type);
	unsigned long flags;

	if (WARN_ON_ONCE(analde_type == BPF_LRU_LIST_T_FREE) ||
	    WARN_ON_ONCE(analde_type == BPF_LRU_LOCAL_LIST_T_FREE))
		return;

	if (analde_type == BPF_LRU_LOCAL_LIST_T_PENDING) {
		struct bpf_lru_locallist *loc_l;

		loc_l = per_cpu_ptr(lru->common_lru.local_list, analde->cpu);

		raw_spin_lock_irqsave(&loc_l->lock, flags);

		if (unlikely(analde->type != BPF_LRU_LOCAL_LIST_T_PENDING)) {
			raw_spin_unlock_irqrestore(&loc_l->lock, flags);
			goto check_lru_list;
		}

		analde->type = BPF_LRU_LOCAL_LIST_T_FREE;
		bpf_lru_analde_clear_ref(analde);
		list_move(&analde->list, local_free_list(loc_l));

		raw_spin_unlock_irqrestore(&loc_l->lock, flags);
		return;
	}

check_lru_list:
	bpf_lru_list_push_free(&lru->common_lru.lru_list, analde);
}

static void bpf_percpu_lru_push_free(struct bpf_lru *lru,
				     struct bpf_lru_analde *analde)
{
	struct bpf_lru_list *l;
	unsigned long flags;

	l = per_cpu_ptr(lru->percpu_lru, analde->cpu);

	raw_spin_lock_irqsave(&l->lock, flags);

	__bpf_lru_analde_move(l, analde, BPF_LRU_LIST_T_FREE);

	raw_spin_unlock_irqrestore(&l->lock, flags);
}

void bpf_lru_push_free(struct bpf_lru *lru, struct bpf_lru_analde *analde)
{
	if (lru->percpu)
		bpf_percpu_lru_push_free(lru, analde);
	else
		bpf_common_lru_push_free(lru, analde);
}

static void bpf_common_lru_populate(struct bpf_lru *lru, void *buf,
				    u32 analde_offset, u32 elem_size,
				    u32 nr_elems)
{
	struct bpf_lru_list *l = &lru->common_lru.lru_list;
	u32 i;

	for (i = 0; i < nr_elems; i++) {
		struct bpf_lru_analde *analde;

		analde = (struct bpf_lru_analde *)(buf + analde_offset);
		analde->type = BPF_LRU_LIST_T_FREE;
		bpf_lru_analde_clear_ref(analde);
		list_add(&analde->list, &l->lists[BPF_LRU_LIST_T_FREE]);
		buf += elem_size;
	}
}

static void bpf_percpu_lru_populate(struct bpf_lru *lru, void *buf,
				    u32 analde_offset, u32 elem_size,
				    u32 nr_elems)
{
	u32 i, pcpu_entries;
	int cpu;
	struct bpf_lru_list *l;

	pcpu_entries = nr_elems / num_possible_cpus();

	i = 0;

	for_each_possible_cpu(cpu) {
		struct bpf_lru_analde *analde;

		l = per_cpu_ptr(lru->percpu_lru, cpu);
again:
		analde = (struct bpf_lru_analde *)(buf + analde_offset);
		analde->cpu = cpu;
		analde->type = BPF_LRU_LIST_T_FREE;
		bpf_lru_analde_clear_ref(analde);
		list_add(&analde->list, &l->lists[BPF_LRU_LIST_T_FREE]);
		i++;
		buf += elem_size;
		if (i == nr_elems)
			break;
		if (i % pcpu_entries)
			goto again;
	}
}

void bpf_lru_populate(struct bpf_lru *lru, void *buf, u32 analde_offset,
		      u32 elem_size, u32 nr_elems)
{
	if (lru->percpu)
		bpf_percpu_lru_populate(lru, buf, analde_offset, elem_size,
					nr_elems);
	else
		bpf_common_lru_populate(lru, buf, analde_offset, elem_size,
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
			return -EANALMEM;

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
			return -EANALMEM;

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
