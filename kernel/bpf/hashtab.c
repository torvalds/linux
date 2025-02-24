// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2011-2014 PLUMgrid, http://plumgrid.com
 * Copyright (c) 2016 Facebook
 */
#include <linux/bpf.h>
#include <linux/btf.h>
#include <linux/jhash.h>
#include <linux/filter.h>
#include <linux/rculist_nulls.h>
#include <linux/rcupdate_wait.h>
#include <linux/random.h>
#include <uapi/linux/btf.h>
#include <linux/rcupdate_trace.h>
#include <linux/btf_ids.h>
#include "percpu_freelist.h"
#include "bpf_lru_list.h"
#include "map_in_map.h"
#include <linux/bpf_mem_alloc.h>

#define HTAB_CREATE_FLAG_MASK						\
	(BPF_F_NO_PREALLOC | BPF_F_NO_COMMON_LRU | BPF_F_NUMA_NODE |	\
	 BPF_F_ACCESS_MASK | BPF_F_ZERO_SEED)

#define BATCH_OPS(_name)			\
	.map_lookup_batch =			\
	_name##_map_lookup_batch,		\
	.map_lookup_and_delete_batch =		\
	_name##_map_lookup_and_delete_batch,	\
	.map_update_batch =			\
	generic_map_update_batch,		\
	.map_delete_batch =			\
	generic_map_delete_batch

/*
 * The bucket lock has two protection scopes:
 *
 * 1) Serializing concurrent operations from BPF programs on different
 *    CPUs
 *
 * 2) Serializing concurrent operations from BPF programs and sys_bpf()
 *
 * BPF programs can execute in any context including perf, kprobes and
 * tracing. As there are almost no limits where perf, kprobes and tracing
 * can be invoked from the lock operations need to be protected against
 * deadlocks. Deadlocks can be caused by recursion and by an invocation in
 * the lock held section when functions which acquire this lock are invoked
 * from sys_bpf(). BPF recursion is prevented by incrementing the per CPU
 * variable bpf_prog_active, which prevents BPF programs attached to perf
 * events, kprobes and tracing to be invoked before the prior invocation
 * from one of these contexts completed. sys_bpf() uses the same mechanism
 * by pinning the task to the current CPU and incrementing the recursion
 * protection across the map operation.
 *
 * This has subtle implications on PREEMPT_RT. PREEMPT_RT forbids certain
 * operations like memory allocations (even with GFP_ATOMIC) from atomic
 * contexts. This is required because even with GFP_ATOMIC the memory
 * allocator calls into code paths which acquire locks with long held lock
 * sections. To ensure the deterministic behaviour these locks are regular
 * spinlocks, which are converted to 'sleepable' spinlocks on RT. The only
 * true atomic contexts on an RT kernel are the low level hardware
 * handling, scheduling, low level interrupt handling, NMIs etc. None of
 * these contexts should ever do memory allocations.
 *
 * As regular device interrupt handlers and soft interrupts are forced into
 * thread context, the existing code which does
 *   spin_lock*(); alloc(GFP_ATOMIC); spin_unlock*();
 * just works.
 *
 * In theory the BPF locks could be converted to regular spinlocks as well,
 * but the bucket locks and percpu_freelist locks can be taken from
 * arbitrary contexts (perf, kprobes, tracepoints) which are required to be
 * atomic contexts even on RT. Before the introduction of bpf_mem_alloc,
 * it is only safe to use raw spinlock for preallocated hash map on a RT kernel,
 * because there is no memory allocation within the lock held sections. However
 * after hash map was fully converted to use bpf_mem_alloc, there will be
 * non-synchronous memory allocation for non-preallocated hash map, so it is
 * safe to always use raw spinlock for bucket lock.
 */
struct bucket {
	struct hlist_nulls_head head;
	raw_spinlock_t raw_lock;
};

#define HASHTAB_MAP_LOCK_COUNT 8
#define HASHTAB_MAP_LOCK_MASK (HASHTAB_MAP_LOCK_COUNT - 1)

struct bpf_htab {
	struct bpf_map map;
	struct bpf_mem_alloc ma;
	struct bpf_mem_alloc pcpu_ma;
	struct bucket *buckets;
	void *elems;
	union {
		struct pcpu_freelist freelist;
		struct bpf_lru lru;
	};
	struct htab_elem *__percpu *extra_elems;
	/* number of elements in non-preallocated hashtable are kept
	 * in either pcount or count
	 */
	struct percpu_counter pcount;
	atomic_t count;
	bool use_percpu_counter;
	u32 n_buckets;	/* number of hash buckets */
	u32 elem_size;	/* size of each element in bytes */
	u32 hashrnd;
	struct lock_class_key lockdep_key;
	int __percpu *map_locked[HASHTAB_MAP_LOCK_COUNT];
};

/* each htab element is struct htab_elem + key + value */
struct htab_elem {
	union {
		struct hlist_nulls_node hash_node;
		struct {
			void *padding;
			union {
				struct pcpu_freelist_node fnode;
				struct htab_elem *batch_flink;
			};
		};
	};
	union {
		/* pointer to per-cpu pointer */
		void *ptr_to_pptr;
		struct bpf_lru_node lru_node;
	};
	u32 hash;
	char key[] __aligned(8);
};

static inline bool htab_is_prealloc(const struct bpf_htab *htab)
{
	return !(htab->map.map_flags & BPF_F_NO_PREALLOC);
}

static void htab_init_buckets(struct bpf_htab *htab)
{
	unsigned int i;

	for (i = 0; i < htab->n_buckets; i++) {
		INIT_HLIST_NULLS_HEAD(&htab->buckets[i].head, i);
		raw_spin_lock_init(&htab->buckets[i].raw_lock);
		lockdep_set_class(&htab->buckets[i].raw_lock,
					  &htab->lockdep_key);
		cond_resched();
	}
}

static inline int htab_lock_bucket(const struct bpf_htab *htab,
				   struct bucket *b, u32 hash,
				   unsigned long *pflags)
{
	unsigned long flags;

	hash = hash & min_t(u32, HASHTAB_MAP_LOCK_MASK, htab->n_buckets - 1);

	preempt_disable();
	local_irq_save(flags);
	if (unlikely(__this_cpu_inc_return(*(htab->map_locked[hash])) != 1)) {
		__this_cpu_dec(*(htab->map_locked[hash]));
		local_irq_restore(flags);
		preempt_enable();
		return -EBUSY;
	}

	raw_spin_lock(&b->raw_lock);
	*pflags = flags;

	return 0;
}

static inline void htab_unlock_bucket(const struct bpf_htab *htab,
				      struct bucket *b, u32 hash,
				      unsigned long flags)
{
	hash = hash & min_t(u32, HASHTAB_MAP_LOCK_MASK, htab->n_buckets - 1);
	raw_spin_unlock(&b->raw_lock);
	__this_cpu_dec(*(htab->map_locked[hash]));
	local_irq_restore(flags);
	preempt_enable();
}

static bool htab_lru_map_delete_node(void *arg, struct bpf_lru_node *node);

static bool htab_is_lru(const struct bpf_htab *htab)
{
	return htab->map.map_type == BPF_MAP_TYPE_LRU_HASH ||
		htab->map.map_type == BPF_MAP_TYPE_LRU_PERCPU_HASH;
}

static bool htab_is_percpu(const struct bpf_htab *htab)
{
	return htab->map.map_type == BPF_MAP_TYPE_PERCPU_HASH ||
		htab->map.map_type == BPF_MAP_TYPE_LRU_PERCPU_HASH;
}

static inline void htab_elem_set_ptr(struct htab_elem *l, u32 key_size,
				     void __percpu *pptr)
{
	*(void __percpu **)(l->key + roundup(key_size, 8)) = pptr;
}

static inline void __percpu *htab_elem_get_ptr(struct htab_elem *l, u32 key_size)
{
	return *(void __percpu **)(l->key + roundup(key_size, 8));
}

static void *fd_htab_map_get_ptr(const struct bpf_map *map, struct htab_elem *l)
{
	return *(void **)(l->key + roundup(map->key_size, 8));
}

static struct htab_elem *get_htab_elem(struct bpf_htab *htab, int i)
{
	return (struct htab_elem *) (htab->elems + i * (u64)htab->elem_size);
}

static bool htab_has_extra_elems(struct bpf_htab *htab)
{
	return !htab_is_percpu(htab) && !htab_is_lru(htab);
}

static void htab_free_prealloced_timers_and_wq(struct bpf_htab *htab)
{
	u32 num_entries = htab->map.max_entries;
	int i;

	if (htab_has_extra_elems(htab))
		num_entries += num_possible_cpus();

	for (i = 0; i < num_entries; i++) {
		struct htab_elem *elem;

		elem = get_htab_elem(htab, i);
		if (btf_record_has_field(htab->map.record, BPF_TIMER))
			bpf_obj_free_timer(htab->map.record,
					   elem->key + round_up(htab->map.key_size, 8));
		if (btf_record_has_field(htab->map.record, BPF_WORKQUEUE))
			bpf_obj_free_workqueue(htab->map.record,
					       elem->key + round_up(htab->map.key_size, 8));
		cond_resched();
	}
}

static void htab_free_prealloced_fields(struct bpf_htab *htab)
{
	u32 num_entries = htab->map.max_entries;
	int i;

	if (IS_ERR_OR_NULL(htab->map.record))
		return;
	if (htab_has_extra_elems(htab))
		num_entries += num_possible_cpus();
	for (i = 0; i < num_entries; i++) {
		struct htab_elem *elem;

		elem = get_htab_elem(htab, i);
		if (htab_is_percpu(htab)) {
			void __percpu *pptr = htab_elem_get_ptr(elem, htab->map.key_size);
			int cpu;

			for_each_possible_cpu(cpu) {
				bpf_obj_free_fields(htab->map.record, per_cpu_ptr(pptr, cpu));
				cond_resched();
			}
		} else {
			bpf_obj_free_fields(htab->map.record, elem->key + round_up(htab->map.key_size, 8));
			cond_resched();
		}
		cond_resched();
	}
}

static void htab_free_elems(struct bpf_htab *htab)
{
	int i;

	if (!htab_is_percpu(htab))
		goto free_elems;

	for (i = 0; i < htab->map.max_entries; i++) {
		void __percpu *pptr;

		pptr = htab_elem_get_ptr(get_htab_elem(htab, i),
					 htab->map.key_size);
		free_percpu(pptr);
		cond_resched();
	}
free_elems:
	bpf_map_area_free(htab->elems);
}

/* The LRU list has a lock (lru_lock). Each htab bucket has a lock
 * (bucket_lock). If both locks need to be acquired together, the lock
 * order is always lru_lock -> bucket_lock and this only happens in
 * bpf_lru_list.c logic. For example, certain code path of
 * bpf_lru_pop_free(), which is called by function prealloc_lru_pop(),
 * will acquire lru_lock first followed by acquiring bucket_lock.
 *
 * In hashtab.c, to avoid deadlock, lock acquisition of
 * bucket_lock followed by lru_lock is not allowed. In such cases,
 * bucket_lock needs to be released first before acquiring lru_lock.
 */
static struct htab_elem *prealloc_lru_pop(struct bpf_htab *htab, void *key,
					  u32 hash)
{
	struct bpf_lru_node *node = bpf_lru_pop_free(&htab->lru, hash);
	struct htab_elem *l;

	if (node) {
		bpf_map_inc_elem_count(&htab->map);
		l = container_of(node, struct htab_elem, lru_node);
		memcpy(l->key, key, htab->map.key_size);
		return l;
	}

	return NULL;
}

static int prealloc_init(struct bpf_htab *htab)
{
	u32 num_entries = htab->map.max_entries;
	int err = -ENOMEM, i;

	if (htab_has_extra_elems(htab))
		num_entries += num_possible_cpus();

	htab->elems = bpf_map_area_alloc((u64)htab->elem_size * num_entries,
					 htab->map.numa_node);
	if (!htab->elems)
		return -ENOMEM;

	if (!htab_is_percpu(htab))
		goto skip_percpu_elems;

	for (i = 0; i < num_entries; i++) {
		u32 size = round_up(htab->map.value_size, 8);
		void __percpu *pptr;

		pptr = bpf_map_alloc_percpu(&htab->map, size, 8,
					    GFP_USER | __GFP_NOWARN);
		if (!pptr)
			goto free_elems;
		htab_elem_set_ptr(get_htab_elem(htab, i), htab->map.key_size,
				  pptr);
		cond_resched();
	}

skip_percpu_elems:
	if (htab_is_lru(htab))
		err = bpf_lru_init(&htab->lru,
				   htab->map.map_flags & BPF_F_NO_COMMON_LRU,
				   offsetof(struct htab_elem, hash) -
				   offsetof(struct htab_elem, lru_node),
				   htab_lru_map_delete_node,
				   htab);
	else
		err = pcpu_freelist_init(&htab->freelist);

	if (err)
		goto free_elems;

	if (htab_is_lru(htab))
		bpf_lru_populate(&htab->lru, htab->elems,
				 offsetof(struct htab_elem, lru_node),
				 htab->elem_size, num_entries);
	else
		pcpu_freelist_populate(&htab->freelist,
				       htab->elems + offsetof(struct htab_elem, fnode),
				       htab->elem_size, num_entries);

	return 0;

free_elems:
	htab_free_elems(htab);
	return err;
}

static void prealloc_destroy(struct bpf_htab *htab)
{
	htab_free_elems(htab);

	if (htab_is_lru(htab))
		bpf_lru_destroy(&htab->lru);
	else
		pcpu_freelist_destroy(&htab->freelist);
}

static int alloc_extra_elems(struct bpf_htab *htab)
{
	struct htab_elem *__percpu *pptr, *l_new;
	struct pcpu_freelist_node *l;
	int cpu;

	pptr = bpf_map_alloc_percpu(&htab->map, sizeof(struct htab_elem *), 8,
				    GFP_USER | __GFP_NOWARN);
	if (!pptr)
		return -ENOMEM;

	for_each_possible_cpu(cpu) {
		l = pcpu_freelist_pop(&htab->freelist);
		/* pop will succeed, since prealloc_init()
		 * preallocated extra num_possible_cpus elements
		 */
		l_new = container_of(l, struct htab_elem, fnode);
		*per_cpu_ptr(pptr, cpu) = l_new;
	}
	htab->extra_elems = pptr;
	return 0;
}

/* Called from syscall */
static int htab_map_alloc_check(union bpf_attr *attr)
{
	bool percpu = (attr->map_type == BPF_MAP_TYPE_PERCPU_HASH ||
		       attr->map_type == BPF_MAP_TYPE_LRU_PERCPU_HASH);
	bool lru = (attr->map_type == BPF_MAP_TYPE_LRU_HASH ||
		    attr->map_type == BPF_MAP_TYPE_LRU_PERCPU_HASH);
	/* percpu_lru means each cpu has its own LRU list.
	 * it is different from BPF_MAP_TYPE_PERCPU_HASH where
	 * the map's value itself is percpu.  percpu_lru has
	 * nothing to do with the map's value.
	 */
	bool percpu_lru = (attr->map_flags & BPF_F_NO_COMMON_LRU);
	bool prealloc = !(attr->map_flags & BPF_F_NO_PREALLOC);
	bool zero_seed = (attr->map_flags & BPF_F_ZERO_SEED);
	int numa_node = bpf_map_attr_numa_node(attr);

	BUILD_BUG_ON(offsetof(struct htab_elem, fnode.next) !=
		     offsetof(struct htab_elem, hash_node.pprev));

	if (zero_seed && !capable(CAP_SYS_ADMIN))
		/* Guard against local DoS, and discourage production use. */
		return -EPERM;

	if (attr->map_flags & ~HTAB_CREATE_FLAG_MASK ||
	    !bpf_map_flags_access_ok(attr->map_flags))
		return -EINVAL;

	if (!lru && percpu_lru)
		return -EINVAL;

	if (lru && !prealloc)
		return -ENOTSUPP;

	if (numa_node != NUMA_NO_NODE && (percpu || percpu_lru))
		return -EINVAL;

	/* check sanity of attributes.
	 * value_size == 0 may be allowed in the future to use map as a set
	 */
	if (attr->max_entries == 0 || attr->key_size == 0 ||
	    attr->value_size == 0)
		return -EINVAL;

	if ((u64)attr->key_size + attr->value_size >= KMALLOC_MAX_SIZE -
	   sizeof(struct htab_elem))
		/* if key_size + value_size is bigger, the user space won't be
		 * able to access the elements via bpf syscall. This check
		 * also makes sure that the elem_size doesn't overflow and it's
		 * kmalloc-able later in htab_map_update_elem()
		 */
		return -E2BIG;
	/* percpu map value size is bound by PCPU_MIN_UNIT_SIZE */
	if (percpu && round_up(attr->value_size, 8) > PCPU_MIN_UNIT_SIZE)
		return -E2BIG;

	return 0;
}

static struct bpf_map *htab_map_alloc(union bpf_attr *attr)
{
	bool percpu = (attr->map_type == BPF_MAP_TYPE_PERCPU_HASH ||
		       attr->map_type == BPF_MAP_TYPE_LRU_PERCPU_HASH);
	bool lru = (attr->map_type == BPF_MAP_TYPE_LRU_HASH ||
		    attr->map_type == BPF_MAP_TYPE_LRU_PERCPU_HASH);
	/* percpu_lru means each cpu has its own LRU list.
	 * it is different from BPF_MAP_TYPE_PERCPU_HASH where
	 * the map's value itself is percpu.  percpu_lru has
	 * nothing to do with the map's value.
	 */
	bool percpu_lru = (attr->map_flags & BPF_F_NO_COMMON_LRU);
	bool prealloc = !(attr->map_flags & BPF_F_NO_PREALLOC);
	struct bpf_htab *htab;
	int err, i;

	htab = bpf_map_area_alloc(sizeof(*htab), NUMA_NO_NODE);
	if (!htab)
		return ERR_PTR(-ENOMEM);

	lockdep_register_key(&htab->lockdep_key);

	bpf_map_init_from_attr(&htab->map, attr);

	if (percpu_lru) {
		/* ensure each CPU's lru list has >=1 elements.
		 * since we are at it, make each lru list has the same
		 * number of elements.
		 */
		htab->map.max_entries = roundup(attr->max_entries,
						num_possible_cpus());
		if (htab->map.max_entries < attr->max_entries)
			htab->map.max_entries = rounddown(attr->max_entries,
							  num_possible_cpus());
	}

	/* hash table size must be power of 2; roundup_pow_of_two() can overflow
	 * into UB on 32-bit arches, so check that first
	 */
	err = -E2BIG;
	if (htab->map.max_entries > 1UL << 31)
		goto free_htab;

	htab->n_buckets = roundup_pow_of_two(htab->map.max_entries);

	htab->elem_size = sizeof(struct htab_elem) +
			  round_up(htab->map.key_size, 8);
	if (percpu)
		htab->elem_size += sizeof(void *);
	else
		htab->elem_size += round_up(htab->map.value_size, 8);

	/* check for u32 overflow */
	if (htab->n_buckets > U32_MAX / sizeof(struct bucket))
		goto free_htab;

	err = bpf_map_init_elem_count(&htab->map);
	if (err)
		goto free_htab;

	err = -ENOMEM;
	htab->buckets = bpf_map_area_alloc(htab->n_buckets *
					   sizeof(struct bucket),
					   htab->map.numa_node);
	if (!htab->buckets)
		goto free_elem_count;

	for (i = 0; i < HASHTAB_MAP_LOCK_COUNT; i++) {
		htab->map_locked[i] = bpf_map_alloc_percpu(&htab->map,
							   sizeof(int),
							   sizeof(int),
							   GFP_USER);
		if (!htab->map_locked[i])
			goto free_map_locked;
	}

	if (htab->map.map_flags & BPF_F_ZERO_SEED)
		htab->hashrnd = 0;
	else
		htab->hashrnd = get_random_u32();

	htab_init_buckets(htab);

/* compute_batch_value() computes batch value as num_online_cpus() * 2
 * and __percpu_counter_compare() needs
 * htab->max_entries - cur_number_of_elems to be more than batch * num_online_cpus()
 * for percpu_counter to be faster than atomic_t. In practice the average bpf
 * hash map size is 10k, which means that a system with 64 cpus will fill
 * hashmap to 20% of 10k before percpu_counter becomes ineffective. Therefore
 * define our own batch count as 32 then 10k hash map can be filled up to 80%:
 * 10k - 8k > 32 _batch_ * 64 _cpus_
 * and __percpu_counter_compare() will still be fast. At that point hash map
 * collisions will dominate its performance anyway. Assume that hash map filled
 * to 50+% isn't going to be O(1) and use the following formula to choose
 * between percpu_counter and atomic_t.
 */
#define PERCPU_COUNTER_BATCH 32
	if (attr->max_entries / 2 > num_online_cpus() * PERCPU_COUNTER_BATCH)
		htab->use_percpu_counter = true;

	if (htab->use_percpu_counter) {
		err = percpu_counter_init(&htab->pcount, 0, GFP_KERNEL);
		if (err)
			goto free_map_locked;
	}

	if (prealloc) {
		err = prealloc_init(htab);
		if (err)
			goto free_map_locked;

		if (!percpu && !lru) {
			/* lru itself can remove the least used element, so
			 * there is no need for an extra elem during map_update.
			 */
			err = alloc_extra_elems(htab);
			if (err)
				goto free_prealloc;
		}
	} else {
		err = bpf_mem_alloc_init(&htab->ma, htab->elem_size, false);
		if (err)
			goto free_map_locked;
		if (percpu) {
			err = bpf_mem_alloc_init(&htab->pcpu_ma,
						 round_up(htab->map.value_size, 8), true);
			if (err)
				goto free_map_locked;
		}
	}

	return &htab->map;

free_prealloc:
	prealloc_destroy(htab);
free_map_locked:
	if (htab->use_percpu_counter)
		percpu_counter_destroy(&htab->pcount);
	for (i = 0; i < HASHTAB_MAP_LOCK_COUNT; i++)
		free_percpu(htab->map_locked[i]);
	bpf_map_area_free(htab->buckets);
	bpf_mem_alloc_destroy(&htab->pcpu_ma);
	bpf_mem_alloc_destroy(&htab->ma);
free_elem_count:
	bpf_map_free_elem_count(&htab->map);
free_htab:
	lockdep_unregister_key(&htab->lockdep_key);
	bpf_map_area_free(htab);
	return ERR_PTR(err);
}

static inline u32 htab_map_hash(const void *key, u32 key_len, u32 hashrnd)
{
	if (likely(key_len % 4 == 0))
		return jhash2(key, key_len / 4, hashrnd);
	return jhash(key, key_len, hashrnd);
}

static inline struct bucket *__select_bucket(struct bpf_htab *htab, u32 hash)
{
	return &htab->buckets[hash & (htab->n_buckets - 1)];
}

static inline struct hlist_nulls_head *select_bucket(struct bpf_htab *htab, u32 hash)
{
	return &__select_bucket(htab, hash)->head;
}

/* this lookup function can only be called with bucket lock taken */
static struct htab_elem *lookup_elem_raw(struct hlist_nulls_head *head, u32 hash,
					 void *key, u32 key_size)
{
	struct hlist_nulls_node *n;
	struct htab_elem *l;

	hlist_nulls_for_each_entry_rcu(l, n, head, hash_node)
		if (l->hash == hash && !memcmp(&l->key, key, key_size))
			return l;

	return NULL;
}

/* can be called without bucket lock. it will repeat the loop in
 * the unlikely event when elements moved from one bucket into another
 * while link list is being walked
 */
static struct htab_elem *lookup_nulls_elem_raw(struct hlist_nulls_head *head,
					       u32 hash, void *key,
					       u32 key_size, u32 n_buckets)
{
	struct hlist_nulls_node *n;
	struct htab_elem *l;

again:
	hlist_nulls_for_each_entry_rcu(l, n, head, hash_node)
		if (l->hash == hash && !memcmp(&l->key, key, key_size))
			return l;

	if (unlikely(get_nulls_value(n) != (hash & (n_buckets - 1))))
		goto again;

	return NULL;
}

/* Called from syscall or from eBPF program directly, so
 * arguments have to match bpf_map_lookup_elem() exactly.
 * The return value is adjusted by BPF instructions
 * in htab_map_gen_lookup().
 */
static void *__htab_map_lookup_elem(struct bpf_map *map, void *key)
{
	struct bpf_htab *htab = container_of(map, struct bpf_htab, map);
	struct hlist_nulls_head *head;
	struct htab_elem *l;
	u32 hash, key_size;

	WARN_ON_ONCE(!rcu_read_lock_held() && !rcu_read_lock_trace_held() &&
		     !rcu_read_lock_bh_held());

	key_size = map->key_size;

	hash = htab_map_hash(key, key_size, htab->hashrnd);

	head = select_bucket(htab, hash);

	l = lookup_nulls_elem_raw(head, hash, key, key_size, htab->n_buckets);

	return l;
}

static void *htab_map_lookup_elem(struct bpf_map *map, void *key)
{
	struct htab_elem *l = __htab_map_lookup_elem(map, key);

	if (l)
		return l->key + round_up(map->key_size, 8);

	return NULL;
}

/* inline bpf_map_lookup_elem() call.
 * Instead of:
 * bpf_prog
 *   bpf_map_lookup_elem
 *     map->ops->map_lookup_elem
 *       htab_map_lookup_elem
 *         __htab_map_lookup_elem
 * do:
 * bpf_prog
 *   __htab_map_lookup_elem
 */
static int htab_map_gen_lookup(struct bpf_map *map, struct bpf_insn *insn_buf)
{
	struct bpf_insn *insn = insn_buf;
	const int ret = BPF_REG_0;

	BUILD_BUG_ON(!__same_type(&__htab_map_lookup_elem,
		     (void *(*)(struct bpf_map *map, void *key))NULL));
	*insn++ = BPF_EMIT_CALL(__htab_map_lookup_elem);
	*insn++ = BPF_JMP_IMM(BPF_JEQ, ret, 0, 1);
	*insn++ = BPF_ALU64_IMM(BPF_ADD, ret,
				offsetof(struct htab_elem, key) +
				round_up(map->key_size, 8));
	return insn - insn_buf;
}

static __always_inline void *__htab_lru_map_lookup_elem(struct bpf_map *map,
							void *key, const bool mark)
{
	struct htab_elem *l = __htab_map_lookup_elem(map, key);

	if (l) {
		if (mark)
			bpf_lru_node_set_ref(&l->lru_node);
		return l->key + round_up(map->key_size, 8);
	}

	return NULL;
}

static void *htab_lru_map_lookup_elem(struct bpf_map *map, void *key)
{
	return __htab_lru_map_lookup_elem(map, key, true);
}

static void *htab_lru_map_lookup_elem_sys(struct bpf_map *map, void *key)
{
	return __htab_lru_map_lookup_elem(map, key, false);
}

static int htab_lru_map_gen_lookup(struct bpf_map *map,
				   struct bpf_insn *insn_buf)
{
	struct bpf_insn *insn = insn_buf;
	const int ret = BPF_REG_0;
	const int ref_reg = BPF_REG_1;

	BUILD_BUG_ON(!__same_type(&__htab_map_lookup_elem,
		     (void *(*)(struct bpf_map *map, void *key))NULL));
	*insn++ = BPF_EMIT_CALL(__htab_map_lookup_elem);
	*insn++ = BPF_JMP_IMM(BPF_JEQ, ret, 0, 4);
	*insn++ = BPF_LDX_MEM(BPF_B, ref_reg, ret,
			      offsetof(struct htab_elem, lru_node) +
			      offsetof(struct bpf_lru_node, ref));
	*insn++ = BPF_JMP_IMM(BPF_JNE, ref_reg, 0, 1);
	*insn++ = BPF_ST_MEM(BPF_B, ret,
			     offsetof(struct htab_elem, lru_node) +
			     offsetof(struct bpf_lru_node, ref),
			     1);
	*insn++ = BPF_ALU64_IMM(BPF_ADD, ret,
				offsetof(struct htab_elem, key) +
				round_up(map->key_size, 8));
	return insn - insn_buf;
}

static void check_and_free_fields(struct bpf_htab *htab,
				  struct htab_elem *elem)
{
	if (htab_is_percpu(htab)) {
		void __percpu *pptr = htab_elem_get_ptr(elem, htab->map.key_size);
		int cpu;

		for_each_possible_cpu(cpu)
			bpf_obj_free_fields(htab->map.record, per_cpu_ptr(pptr, cpu));
	} else {
		void *map_value = elem->key + round_up(htab->map.key_size, 8);

		bpf_obj_free_fields(htab->map.record, map_value);
	}
}

/* It is called from the bpf_lru_list when the LRU needs to delete
 * older elements from the htab.
 */
static bool htab_lru_map_delete_node(void *arg, struct bpf_lru_node *node)
{
	struct bpf_htab *htab = arg;
	struct htab_elem *l = NULL, *tgt_l;
	struct hlist_nulls_head *head;
	struct hlist_nulls_node *n;
	unsigned long flags;
	struct bucket *b;
	int ret;

	tgt_l = container_of(node, struct htab_elem, lru_node);
	b = __select_bucket(htab, tgt_l->hash);
	head = &b->head;

	ret = htab_lock_bucket(htab, b, tgt_l->hash, &flags);
	if (ret)
		return false;

	hlist_nulls_for_each_entry_rcu(l, n, head, hash_node)
		if (l == tgt_l) {
			hlist_nulls_del_rcu(&l->hash_node);
			bpf_map_dec_elem_count(&htab->map);
			break;
		}

	htab_unlock_bucket(htab, b, tgt_l->hash, flags);

	if (l == tgt_l)
		check_and_free_fields(htab, l);
	return l == tgt_l;
}

/* Called from syscall */
static int htab_map_get_next_key(struct bpf_map *map, void *key, void *next_key)
{
	struct bpf_htab *htab = container_of(map, struct bpf_htab, map);
	struct hlist_nulls_head *head;
	struct htab_elem *l, *next_l;
	u32 hash, key_size;
	int i = 0;

	WARN_ON_ONCE(!rcu_read_lock_held());

	key_size = map->key_size;

	if (!key)
		goto find_first_elem;

	hash = htab_map_hash(key, key_size, htab->hashrnd);

	head = select_bucket(htab, hash);

	/* lookup the key */
	l = lookup_nulls_elem_raw(head, hash, key, key_size, htab->n_buckets);

	if (!l)
		goto find_first_elem;

	/* key was found, get next key in the same bucket */
	next_l = hlist_nulls_entry_safe(rcu_dereference_raw(hlist_nulls_next_rcu(&l->hash_node)),
				  struct htab_elem, hash_node);

	if (next_l) {
		/* if next elem in this hash list is non-zero, just return it */
		memcpy(next_key, next_l->key, key_size);
		return 0;
	}

	/* no more elements in this hash list, go to the next bucket */
	i = hash & (htab->n_buckets - 1);
	i++;

find_first_elem:
	/* iterate over buckets */
	for (; i < htab->n_buckets; i++) {
		head = select_bucket(htab, i);

		/* pick first element in the bucket */
		next_l = hlist_nulls_entry_safe(rcu_dereference_raw(hlist_nulls_first_rcu(head)),
					  struct htab_elem, hash_node);
		if (next_l) {
			/* if it's not empty, just return it */
			memcpy(next_key, next_l->key, key_size);
			return 0;
		}
	}

	/* iterated over all buckets and all elements */
	return -ENOENT;
}

static void htab_elem_free(struct bpf_htab *htab, struct htab_elem *l)
{
	check_and_free_fields(htab, l);

	if (htab->map.map_type == BPF_MAP_TYPE_PERCPU_HASH)
		bpf_mem_cache_free(&htab->pcpu_ma, l->ptr_to_pptr);
	bpf_mem_cache_free(&htab->ma, l);
}

static void htab_put_fd_value(struct bpf_htab *htab, struct htab_elem *l)
{
	struct bpf_map *map = &htab->map;
	void *ptr;

	if (map->ops->map_fd_put_ptr) {
		ptr = fd_htab_map_get_ptr(map, l);
		map->ops->map_fd_put_ptr(map, ptr, true);
	}
}

static bool is_map_full(struct bpf_htab *htab)
{
	if (htab->use_percpu_counter)
		return __percpu_counter_compare(&htab->pcount, htab->map.max_entries,
						PERCPU_COUNTER_BATCH) >= 0;
	return atomic_read(&htab->count) >= htab->map.max_entries;
}

static void inc_elem_count(struct bpf_htab *htab)
{
	bpf_map_inc_elem_count(&htab->map);

	if (htab->use_percpu_counter)
		percpu_counter_add_batch(&htab->pcount, 1, PERCPU_COUNTER_BATCH);
	else
		atomic_inc(&htab->count);
}

static void dec_elem_count(struct bpf_htab *htab)
{
	bpf_map_dec_elem_count(&htab->map);

	if (htab->use_percpu_counter)
		percpu_counter_add_batch(&htab->pcount, -1, PERCPU_COUNTER_BATCH);
	else
		atomic_dec(&htab->count);
}


static void free_htab_elem(struct bpf_htab *htab, struct htab_elem *l)
{
	htab_put_fd_value(htab, l);

	if (htab_is_prealloc(htab)) {
		bpf_map_dec_elem_count(&htab->map);
		check_and_free_fields(htab, l);
		pcpu_freelist_push(&htab->freelist, &l->fnode);
	} else {
		dec_elem_count(htab);
		htab_elem_free(htab, l);
	}
}

static void pcpu_copy_value(struct bpf_htab *htab, void __percpu *pptr,
			    void *value, bool onallcpus)
{
	if (!onallcpus) {
		/* copy true value_size bytes */
		copy_map_value(&htab->map, this_cpu_ptr(pptr), value);
	} else {
		u32 size = round_up(htab->map.value_size, 8);
		int off = 0, cpu;

		for_each_possible_cpu(cpu) {
			copy_map_value_long(&htab->map, per_cpu_ptr(pptr, cpu), value + off);
			off += size;
		}
	}
}

static void pcpu_init_value(struct bpf_htab *htab, void __percpu *pptr,
			    void *value, bool onallcpus)
{
	/* When not setting the initial value on all cpus, zero-fill element
	 * values for other cpus. Otherwise, bpf program has no way to ensure
	 * known initial values for cpus other than current one
	 * (onallcpus=false always when coming from bpf prog).
	 */
	if (!onallcpus) {
		int current_cpu = raw_smp_processor_id();
		int cpu;

		for_each_possible_cpu(cpu) {
			if (cpu == current_cpu)
				copy_map_value_long(&htab->map, per_cpu_ptr(pptr, cpu), value);
			else /* Since elem is preallocated, we cannot touch special fields */
				zero_map_value(&htab->map, per_cpu_ptr(pptr, cpu));
		}
	} else {
		pcpu_copy_value(htab, pptr, value, onallcpus);
	}
}

static bool fd_htab_map_needs_adjust(const struct bpf_htab *htab)
{
	return htab->map.map_type == BPF_MAP_TYPE_HASH_OF_MAPS &&
	       BITS_PER_LONG == 64;
}

static struct htab_elem *alloc_htab_elem(struct bpf_htab *htab, void *key,
					 void *value, u32 key_size, u32 hash,
					 bool percpu, bool onallcpus,
					 struct htab_elem *old_elem)
{
	u32 size = htab->map.value_size;
	bool prealloc = htab_is_prealloc(htab);
	struct htab_elem *l_new, **pl_new;
	void __percpu *pptr;

	if (prealloc) {
		if (old_elem) {
			/* if we're updating the existing element,
			 * use per-cpu extra elems to avoid freelist_pop/push
			 */
			pl_new = this_cpu_ptr(htab->extra_elems);
			l_new = *pl_new;
			*pl_new = old_elem;
		} else {
			struct pcpu_freelist_node *l;

			l = __pcpu_freelist_pop(&htab->freelist);
			if (!l)
				return ERR_PTR(-E2BIG);
			l_new = container_of(l, struct htab_elem, fnode);
			bpf_map_inc_elem_count(&htab->map);
		}
	} else {
		if (is_map_full(htab))
			if (!old_elem)
				/* when map is full and update() is replacing
				 * old element, it's ok to allocate, since
				 * old element will be freed immediately.
				 * Otherwise return an error
				 */
				return ERR_PTR(-E2BIG);
		inc_elem_count(htab);
		l_new = bpf_mem_cache_alloc(&htab->ma);
		if (!l_new) {
			l_new = ERR_PTR(-ENOMEM);
			goto dec_count;
		}
	}

	memcpy(l_new->key, key, key_size);
	if (percpu) {
		if (prealloc) {
			pptr = htab_elem_get_ptr(l_new, key_size);
		} else {
			/* alloc_percpu zero-fills */
			void *ptr = bpf_mem_cache_alloc(&htab->pcpu_ma);

			if (!ptr) {
				bpf_mem_cache_free(&htab->ma, l_new);
				l_new = ERR_PTR(-ENOMEM);
				goto dec_count;
			}
			l_new->ptr_to_pptr = ptr;
			pptr = *(void __percpu **)ptr;
		}

		pcpu_init_value(htab, pptr, value, onallcpus);

		if (!prealloc)
			htab_elem_set_ptr(l_new, key_size, pptr);
	} else if (fd_htab_map_needs_adjust(htab)) {
		size = round_up(size, 8);
		memcpy(l_new->key + round_up(key_size, 8), value, size);
	} else {
		copy_map_value(&htab->map,
			       l_new->key + round_up(key_size, 8),
			       value);
	}

	l_new->hash = hash;
	return l_new;
dec_count:
	dec_elem_count(htab);
	return l_new;
}

static int check_flags(struct bpf_htab *htab, struct htab_elem *l_old,
		       u64 map_flags)
{
	if (l_old && (map_flags & ~BPF_F_LOCK) == BPF_NOEXIST)
		/* elem already exists */
		return -EEXIST;

	if (!l_old && (map_flags & ~BPF_F_LOCK) == BPF_EXIST)
		/* elem doesn't exist, cannot update it */
		return -ENOENT;

	return 0;
}

/* Called from syscall or from eBPF program */
static long htab_map_update_elem(struct bpf_map *map, void *key, void *value,
				 u64 map_flags)
{
	struct bpf_htab *htab = container_of(map, struct bpf_htab, map);
	struct htab_elem *l_new = NULL, *l_old;
	struct hlist_nulls_head *head;
	unsigned long flags;
	void *old_map_ptr;
	struct bucket *b;
	u32 key_size, hash;
	int ret;

	if (unlikely((map_flags & ~BPF_F_LOCK) > BPF_EXIST))
		/* unknown flags */
		return -EINVAL;

	WARN_ON_ONCE(!rcu_read_lock_held() && !rcu_read_lock_trace_held() &&
		     !rcu_read_lock_bh_held());

	key_size = map->key_size;

	hash = htab_map_hash(key, key_size, htab->hashrnd);

	b = __select_bucket(htab, hash);
	head = &b->head;

	if (unlikely(map_flags & BPF_F_LOCK)) {
		if (unlikely(!btf_record_has_field(map->record, BPF_SPIN_LOCK)))
			return -EINVAL;
		/* find an element without taking the bucket lock */
		l_old = lookup_nulls_elem_raw(head, hash, key, key_size,
					      htab->n_buckets);
		ret = check_flags(htab, l_old, map_flags);
		if (ret)
			return ret;
		if (l_old) {
			/* grab the element lock and update value in place */
			copy_map_value_locked(map,
					      l_old->key + round_up(key_size, 8),
					      value, false);
			return 0;
		}
		/* fall through, grab the bucket lock and lookup again.
		 * 99.9% chance that the element won't be found,
		 * but second lookup under lock has to be done.
		 */
	}

	ret = htab_lock_bucket(htab, b, hash, &flags);
	if (ret)
		return ret;

	l_old = lookup_elem_raw(head, hash, key, key_size);

	ret = check_flags(htab, l_old, map_flags);
	if (ret)
		goto err;

	if (unlikely(l_old && (map_flags & BPF_F_LOCK))) {
		/* first lookup without the bucket lock didn't find the element,
		 * but second lookup with the bucket lock found it.
		 * This case is highly unlikely, but has to be dealt with:
		 * grab the element lock in addition to the bucket lock
		 * and update element in place
		 */
		copy_map_value_locked(map,
				      l_old->key + round_up(key_size, 8),
				      value, false);
		ret = 0;
		goto err;
	}

	l_new = alloc_htab_elem(htab, key, value, key_size, hash, false, false,
				l_old);
	if (IS_ERR(l_new)) {
		/* all pre-allocated elements are in use or memory exhausted */
		ret = PTR_ERR(l_new);
		goto err;
	}

	/* add new element to the head of the list, so that
	 * concurrent search will find it before old elem
	 */
	hlist_nulls_add_head_rcu(&l_new->hash_node, head);
	if (l_old) {
		hlist_nulls_del_rcu(&l_old->hash_node);

		/* l_old has already been stashed in htab->extra_elems, free
		 * its special fields before it is available for reuse. Also
		 * save the old map pointer in htab of maps before unlock
		 * and release it after unlock.
		 */
		old_map_ptr = NULL;
		if (htab_is_prealloc(htab)) {
			if (map->ops->map_fd_put_ptr)
				old_map_ptr = fd_htab_map_get_ptr(map, l_old);
			check_and_free_fields(htab, l_old);
		}
	}
	htab_unlock_bucket(htab, b, hash, flags);
	if (l_old) {
		if (old_map_ptr)
			map->ops->map_fd_put_ptr(map, old_map_ptr, true);
		if (!htab_is_prealloc(htab))
			free_htab_elem(htab, l_old);
	}
	return 0;
err:
	htab_unlock_bucket(htab, b, hash, flags);
	return ret;
}

static void htab_lru_push_free(struct bpf_htab *htab, struct htab_elem *elem)
{
	check_and_free_fields(htab, elem);
	bpf_map_dec_elem_count(&htab->map);
	bpf_lru_push_free(&htab->lru, &elem->lru_node);
}

static long htab_lru_map_update_elem(struct bpf_map *map, void *key, void *value,
				     u64 map_flags)
{
	struct bpf_htab *htab = container_of(map, struct bpf_htab, map);
	struct htab_elem *l_new, *l_old = NULL;
	struct hlist_nulls_head *head;
	unsigned long flags;
	struct bucket *b;
	u32 key_size, hash;
	int ret;

	if (unlikely(map_flags > BPF_EXIST))
		/* unknown flags */
		return -EINVAL;

	WARN_ON_ONCE(!rcu_read_lock_held() && !rcu_read_lock_trace_held() &&
		     !rcu_read_lock_bh_held());

	key_size = map->key_size;

	hash = htab_map_hash(key, key_size, htab->hashrnd);

	b = __select_bucket(htab, hash);
	head = &b->head;

	/* For LRU, we need to alloc before taking bucket's
	 * spinlock because getting free nodes from LRU may need
	 * to remove older elements from htab and this removal
	 * operation will need a bucket lock.
	 */
	l_new = prealloc_lru_pop(htab, key, hash);
	if (!l_new)
		return -ENOMEM;
	copy_map_value(&htab->map,
		       l_new->key + round_up(map->key_size, 8), value);

	ret = htab_lock_bucket(htab, b, hash, &flags);
	if (ret)
		goto err_lock_bucket;

	l_old = lookup_elem_raw(head, hash, key, key_size);

	ret = check_flags(htab, l_old, map_flags);
	if (ret)
		goto err;

	/* add new element to the head of the list, so that
	 * concurrent search will find it before old elem
	 */
	hlist_nulls_add_head_rcu(&l_new->hash_node, head);
	if (l_old) {
		bpf_lru_node_set_ref(&l_new->lru_node);
		hlist_nulls_del_rcu(&l_old->hash_node);
	}
	ret = 0;

err:
	htab_unlock_bucket(htab, b, hash, flags);

err_lock_bucket:
	if (ret)
		htab_lru_push_free(htab, l_new);
	else if (l_old)
		htab_lru_push_free(htab, l_old);

	return ret;
}

static long __htab_percpu_map_update_elem(struct bpf_map *map, void *key,
					  void *value, u64 map_flags,
					  bool onallcpus)
{
	struct bpf_htab *htab = container_of(map, struct bpf_htab, map);
	struct htab_elem *l_new = NULL, *l_old;
	struct hlist_nulls_head *head;
	unsigned long flags;
	struct bucket *b;
	u32 key_size, hash;
	int ret;

	if (unlikely(map_flags > BPF_EXIST))
		/* unknown flags */
		return -EINVAL;

	WARN_ON_ONCE(!rcu_read_lock_held() && !rcu_read_lock_trace_held() &&
		     !rcu_read_lock_bh_held());

	key_size = map->key_size;

	hash = htab_map_hash(key, key_size, htab->hashrnd);

	b = __select_bucket(htab, hash);
	head = &b->head;

	ret = htab_lock_bucket(htab, b, hash, &flags);
	if (ret)
		return ret;

	l_old = lookup_elem_raw(head, hash, key, key_size);

	ret = check_flags(htab, l_old, map_flags);
	if (ret)
		goto err;

	if (l_old) {
		/* per-cpu hash map can update value in-place */
		pcpu_copy_value(htab, htab_elem_get_ptr(l_old, key_size),
				value, onallcpus);
	} else {
		l_new = alloc_htab_elem(htab, key, value, key_size,
					hash, true, onallcpus, NULL);
		if (IS_ERR(l_new)) {
			ret = PTR_ERR(l_new);
			goto err;
		}
		hlist_nulls_add_head_rcu(&l_new->hash_node, head);
	}
	ret = 0;
err:
	htab_unlock_bucket(htab, b, hash, flags);
	return ret;
}

static long __htab_lru_percpu_map_update_elem(struct bpf_map *map, void *key,
					      void *value, u64 map_flags,
					      bool onallcpus)
{
	struct bpf_htab *htab = container_of(map, struct bpf_htab, map);
	struct htab_elem *l_new = NULL, *l_old;
	struct hlist_nulls_head *head;
	unsigned long flags;
	struct bucket *b;
	u32 key_size, hash;
	int ret;

	if (unlikely(map_flags > BPF_EXIST))
		/* unknown flags */
		return -EINVAL;

	WARN_ON_ONCE(!rcu_read_lock_held() && !rcu_read_lock_trace_held() &&
		     !rcu_read_lock_bh_held());

	key_size = map->key_size;

	hash = htab_map_hash(key, key_size, htab->hashrnd);

	b = __select_bucket(htab, hash);
	head = &b->head;

	/* For LRU, we need to alloc before taking bucket's
	 * spinlock because LRU's elem alloc may need
	 * to remove older elem from htab and this removal
	 * operation will need a bucket lock.
	 */
	if (map_flags != BPF_EXIST) {
		l_new = prealloc_lru_pop(htab, key, hash);
		if (!l_new)
			return -ENOMEM;
	}

	ret = htab_lock_bucket(htab, b, hash, &flags);
	if (ret)
		goto err_lock_bucket;

	l_old = lookup_elem_raw(head, hash, key, key_size);

	ret = check_flags(htab, l_old, map_flags);
	if (ret)
		goto err;

	if (l_old) {
		bpf_lru_node_set_ref(&l_old->lru_node);

		/* per-cpu hash map can update value in-place */
		pcpu_copy_value(htab, htab_elem_get_ptr(l_old, key_size),
				value, onallcpus);
	} else {
		pcpu_init_value(htab, htab_elem_get_ptr(l_new, key_size),
				value, onallcpus);
		hlist_nulls_add_head_rcu(&l_new->hash_node, head);
		l_new = NULL;
	}
	ret = 0;
err:
	htab_unlock_bucket(htab, b, hash, flags);
err_lock_bucket:
	if (l_new) {
		bpf_map_dec_elem_count(&htab->map);
		bpf_lru_push_free(&htab->lru, &l_new->lru_node);
	}
	return ret;
}

static long htab_percpu_map_update_elem(struct bpf_map *map, void *key,
					void *value, u64 map_flags)
{
	return __htab_percpu_map_update_elem(map, key, value, map_flags, false);
}

static long htab_lru_percpu_map_update_elem(struct bpf_map *map, void *key,
					    void *value, u64 map_flags)
{
	return __htab_lru_percpu_map_update_elem(map, key, value, map_flags,
						 false);
}

/* Called from syscall or from eBPF program */
static long htab_map_delete_elem(struct bpf_map *map, void *key)
{
	struct bpf_htab *htab = container_of(map, struct bpf_htab, map);
	struct hlist_nulls_head *head;
	struct bucket *b;
	struct htab_elem *l;
	unsigned long flags;
	u32 hash, key_size;
	int ret;

	WARN_ON_ONCE(!rcu_read_lock_held() && !rcu_read_lock_trace_held() &&
		     !rcu_read_lock_bh_held());

	key_size = map->key_size;

	hash = htab_map_hash(key, key_size, htab->hashrnd);
	b = __select_bucket(htab, hash);
	head = &b->head;

	ret = htab_lock_bucket(htab, b, hash, &flags);
	if (ret)
		return ret;

	l = lookup_elem_raw(head, hash, key, key_size);
	if (l)
		hlist_nulls_del_rcu(&l->hash_node);
	else
		ret = -ENOENT;

	htab_unlock_bucket(htab, b, hash, flags);

	if (l)
		free_htab_elem(htab, l);
	return ret;
}

static long htab_lru_map_delete_elem(struct bpf_map *map, void *key)
{
	struct bpf_htab *htab = container_of(map, struct bpf_htab, map);
	struct hlist_nulls_head *head;
	struct bucket *b;
	struct htab_elem *l;
	unsigned long flags;
	u32 hash, key_size;
	int ret;

	WARN_ON_ONCE(!rcu_read_lock_held() && !rcu_read_lock_trace_held() &&
		     !rcu_read_lock_bh_held());

	key_size = map->key_size;

	hash = htab_map_hash(key, key_size, htab->hashrnd);
	b = __select_bucket(htab, hash);
	head = &b->head;

	ret = htab_lock_bucket(htab, b, hash, &flags);
	if (ret)
		return ret;

	l = lookup_elem_raw(head, hash, key, key_size);

	if (l)
		hlist_nulls_del_rcu(&l->hash_node);
	else
		ret = -ENOENT;

	htab_unlock_bucket(htab, b, hash, flags);
	if (l)
		htab_lru_push_free(htab, l);
	return ret;
}

static void delete_all_elements(struct bpf_htab *htab)
{
	int i;

	/* It's called from a worker thread and migration has been disabled,
	 * therefore, it is OK to invoke bpf_mem_cache_free() directly.
	 */
	for (i = 0; i < htab->n_buckets; i++) {
		struct hlist_nulls_head *head = select_bucket(htab, i);
		struct hlist_nulls_node *n;
		struct htab_elem *l;

		hlist_nulls_for_each_entry_safe(l, n, head, hash_node) {
			hlist_nulls_del_rcu(&l->hash_node);
			htab_elem_free(htab, l);
		}
		cond_resched();
	}
}

static void htab_free_malloced_timers_and_wq(struct bpf_htab *htab)
{
	int i;

	rcu_read_lock();
	for (i = 0; i < htab->n_buckets; i++) {
		struct hlist_nulls_head *head = select_bucket(htab, i);
		struct hlist_nulls_node *n;
		struct htab_elem *l;

		hlist_nulls_for_each_entry(l, n, head, hash_node) {
			/* We only free timer on uref dropping to zero */
			if (btf_record_has_field(htab->map.record, BPF_TIMER))
				bpf_obj_free_timer(htab->map.record,
						   l->key + round_up(htab->map.key_size, 8));
			if (btf_record_has_field(htab->map.record, BPF_WORKQUEUE))
				bpf_obj_free_workqueue(htab->map.record,
						       l->key + round_up(htab->map.key_size, 8));
		}
		cond_resched_rcu();
	}
	rcu_read_unlock();
}

static void htab_map_free_timers_and_wq(struct bpf_map *map)
{
	struct bpf_htab *htab = container_of(map, struct bpf_htab, map);

	/* We only free timer and workqueue on uref dropping to zero */
	if (btf_record_has_field(htab->map.record, BPF_TIMER | BPF_WORKQUEUE)) {
		if (!htab_is_prealloc(htab))
			htab_free_malloced_timers_and_wq(htab);
		else
			htab_free_prealloced_timers_and_wq(htab);
	}
}

/* Called when map->refcnt goes to zero, either from workqueue or from syscall */
static void htab_map_free(struct bpf_map *map)
{
	struct bpf_htab *htab = container_of(map, struct bpf_htab, map);
	int i;

	/* bpf_free_used_maps() or close(map_fd) will trigger this map_free callback.
	 * bpf_free_used_maps() is called after bpf prog is no longer executing.
	 * There is no need to synchronize_rcu() here to protect map elements.
	 */

	/* htab no longer uses call_rcu() directly. bpf_mem_alloc does it
	 * underneath and is responsible for waiting for callbacks to finish
	 * during bpf_mem_alloc_destroy().
	 */
	if (!htab_is_prealloc(htab)) {
		delete_all_elements(htab);
	} else {
		htab_free_prealloced_fields(htab);
		prealloc_destroy(htab);
	}

	bpf_map_free_elem_count(map);
	free_percpu(htab->extra_elems);
	bpf_map_area_free(htab->buckets);
	bpf_mem_alloc_destroy(&htab->pcpu_ma);
	bpf_mem_alloc_destroy(&htab->ma);
	if (htab->use_percpu_counter)
		percpu_counter_destroy(&htab->pcount);
	for (i = 0; i < HASHTAB_MAP_LOCK_COUNT; i++)
		free_percpu(htab->map_locked[i]);
	lockdep_unregister_key(&htab->lockdep_key);
	bpf_map_area_free(htab);
}

static void htab_map_seq_show_elem(struct bpf_map *map, void *key,
				   struct seq_file *m)
{
	void *value;

	rcu_read_lock();

	value = htab_map_lookup_elem(map, key);
	if (!value) {
		rcu_read_unlock();
		return;
	}

	btf_type_seq_show(map->btf, map->btf_key_type_id, key, m);
	seq_puts(m, ": ");
	btf_type_seq_show(map->btf, map->btf_value_type_id, value, m);
	seq_putc(m, '\n');

	rcu_read_unlock();
}

static int __htab_map_lookup_and_delete_elem(struct bpf_map *map, void *key,
					     void *value, bool is_lru_map,
					     bool is_percpu, u64 flags)
{
	struct bpf_htab *htab = container_of(map, struct bpf_htab, map);
	struct hlist_nulls_head *head;
	unsigned long bflags;
	struct htab_elem *l;
	u32 hash, key_size;
	struct bucket *b;
	int ret;

	key_size = map->key_size;

	hash = htab_map_hash(key, key_size, htab->hashrnd);
	b = __select_bucket(htab, hash);
	head = &b->head;

	ret = htab_lock_bucket(htab, b, hash, &bflags);
	if (ret)
		return ret;

	l = lookup_elem_raw(head, hash, key, key_size);
	if (!l) {
		ret = -ENOENT;
		goto out_unlock;
	}

	if (is_percpu) {
		u32 roundup_value_size = round_up(map->value_size, 8);
		void __percpu *pptr;
		int off = 0, cpu;

		pptr = htab_elem_get_ptr(l, key_size);
		for_each_possible_cpu(cpu) {
			copy_map_value_long(&htab->map, value + off, per_cpu_ptr(pptr, cpu));
			check_and_init_map_value(&htab->map, value + off);
			off += roundup_value_size;
		}
	} else {
		u32 roundup_key_size = round_up(map->key_size, 8);

		if (flags & BPF_F_LOCK)
			copy_map_value_locked(map, value, l->key +
					      roundup_key_size,
					      true);
		else
			copy_map_value(map, value, l->key +
				       roundup_key_size);
		/* Zeroing special fields in the temp buffer */
		check_and_init_map_value(map, value);
	}
	hlist_nulls_del_rcu(&l->hash_node);

out_unlock:
	htab_unlock_bucket(htab, b, hash, bflags);

	if (l) {
		if (is_lru_map)
			htab_lru_push_free(htab, l);
		else
			free_htab_elem(htab, l);
	}

	return ret;
}

static int htab_map_lookup_and_delete_elem(struct bpf_map *map, void *key,
					   void *value, u64 flags)
{
	return __htab_map_lookup_and_delete_elem(map, key, value, false, false,
						 flags);
}

static int htab_percpu_map_lookup_and_delete_elem(struct bpf_map *map,
						  void *key, void *value,
						  u64 flags)
{
	return __htab_map_lookup_and_delete_elem(map, key, value, false, true,
						 flags);
}

static int htab_lru_map_lookup_and_delete_elem(struct bpf_map *map, void *key,
					       void *value, u64 flags)
{
	return __htab_map_lookup_and_delete_elem(map, key, value, true, false,
						 flags);
}

static int htab_lru_percpu_map_lookup_and_delete_elem(struct bpf_map *map,
						      void *key, void *value,
						      u64 flags)
{
	return __htab_map_lookup_and_delete_elem(map, key, value, true, true,
						 flags);
}

static int
__htab_map_lookup_and_delete_batch(struct bpf_map *map,
				   const union bpf_attr *attr,
				   union bpf_attr __user *uattr,
				   bool do_delete, bool is_lru_map,
				   bool is_percpu)
{
	struct bpf_htab *htab = container_of(map, struct bpf_htab, map);
	u32 bucket_cnt, total, key_size, value_size, roundup_key_size;
	void *keys = NULL, *values = NULL, *value, *dst_key, *dst_val;
	void __user *uvalues = u64_to_user_ptr(attr->batch.values);
	void __user *ukeys = u64_to_user_ptr(attr->batch.keys);
	void __user *ubatch = u64_to_user_ptr(attr->batch.in_batch);
	u32 batch, max_count, size, bucket_size, map_id;
	struct htab_elem *node_to_free = NULL;
	u64 elem_map_flags, map_flags;
	struct hlist_nulls_head *head;
	struct hlist_nulls_node *n;
	unsigned long flags = 0;
	bool locked = false;
	struct htab_elem *l;
	struct bucket *b;
	int ret = 0;

	elem_map_flags = attr->batch.elem_flags;
	if ((elem_map_flags & ~BPF_F_LOCK) ||
	    ((elem_map_flags & BPF_F_LOCK) && !btf_record_has_field(map->record, BPF_SPIN_LOCK)))
		return -EINVAL;

	map_flags = attr->batch.flags;
	if (map_flags)
		return -EINVAL;

	max_count = attr->batch.count;
	if (!max_count)
		return 0;

	if (put_user(0, &uattr->batch.count))
		return -EFAULT;

	batch = 0;
	if (ubatch && copy_from_user(&batch, ubatch, sizeof(batch)))
		return -EFAULT;

	if (batch >= htab->n_buckets)
		return -ENOENT;

	key_size = htab->map.key_size;
	roundup_key_size = round_up(htab->map.key_size, 8);
	value_size = htab->map.value_size;
	size = round_up(value_size, 8);
	if (is_percpu)
		value_size = size * num_possible_cpus();
	total = 0;
	/* while experimenting with hash tables with sizes ranging from 10 to
	 * 1000, it was observed that a bucket can have up to 5 entries.
	 */
	bucket_size = 5;

alloc:
	/* We cannot do copy_from_user or copy_to_user inside
	 * the rcu_read_lock. Allocate enough space here.
	 */
	keys = kvmalloc_array(key_size, bucket_size, GFP_USER | __GFP_NOWARN);
	values = kvmalloc_array(value_size, bucket_size, GFP_USER | __GFP_NOWARN);
	if (!keys || !values) {
		ret = -ENOMEM;
		goto after_loop;
	}

again:
	bpf_disable_instrumentation();
	rcu_read_lock();
again_nocopy:
	dst_key = keys;
	dst_val = values;
	b = &htab->buckets[batch];
	head = &b->head;
	/* do not grab the lock unless need it (bucket_cnt > 0). */
	if (locked) {
		ret = htab_lock_bucket(htab, b, batch, &flags);
		if (ret) {
			rcu_read_unlock();
			bpf_enable_instrumentation();
			goto after_loop;
		}
	}

	bucket_cnt = 0;
	hlist_nulls_for_each_entry_rcu(l, n, head, hash_node)
		bucket_cnt++;

	if (bucket_cnt && !locked) {
		locked = true;
		goto again_nocopy;
	}

	if (bucket_cnt > (max_count - total)) {
		if (total == 0)
			ret = -ENOSPC;
		/* Note that since bucket_cnt > 0 here, it is implicit
		 * that the locked was grabbed, so release it.
		 */
		htab_unlock_bucket(htab, b, batch, flags);
		rcu_read_unlock();
		bpf_enable_instrumentation();
		goto after_loop;
	}

	if (bucket_cnt > bucket_size) {
		bucket_size = bucket_cnt;
		/* Note that since bucket_cnt > 0 here, it is implicit
		 * that the locked was grabbed, so release it.
		 */
		htab_unlock_bucket(htab, b, batch, flags);
		rcu_read_unlock();
		bpf_enable_instrumentation();
		kvfree(keys);
		kvfree(values);
		goto alloc;
	}

	/* Next block is only safe to run if you have grabbed the lock */
	if (!locked)
		goto next_batch;

	hlist_nulls_for_each_entry_safe(l, n, head, hash_node) {
		memcpy(dst_key, l->key, key_size);

		if (is_percpu) {
			int off = 0, cpu;
			void __percpu *pptr;

			pptr = htab_elem_get_ptr(l, map->key_size);
			for_each_possible_cpu(cpu) {
				copy_map_value_long(&htab->map, dst_val + off, per_cpu_ptr(pptr, cpu));
				check_and_init_map_value(&htab->map, dst_val + off);
				off += size;
			}
		} else {
			value = l->key + roundup_key_size;
			if (map->map_type == BPF_MAP_TYPE_HASH_OF_MAPS) {
				struct bpf_map **inner_map = value;

				 /* Actual value is the id of the inner map */
				map_id = map->ops->map_fd_sys_lookup_elem(*inner_map);
				value = &map_id;
			}

			if (elem_map_flags & BPF_F_LOCK)
				copy_map_value_locked(map, dst_val, value,
						      true);
			else
				copy_map_value(map, dst_val, value);
			/* Zeroing special fields in the temp buffer */
			check_and_init_map_value(map, dst_val);
		}
		if (do_delete) {
			hlist_nulls_del_rcu(&l->hash_node);

			/* bpf_lru_push_free() will acquire lru_lock, which
			 * may cause deadlock. See comments in function
			 * prealloc_lru_pop(). Let us do bpf_lru_push_free()
			 * after releasing the bucket lock.
			 *
			 * For htab of maps, htab_put_fd_value() in
			 * free_htab_elem() may acquire a spinlock with bucket
			 * lock being held and it violates the lock rule, so
			 * invoke free_htab_elem() after unlock as well.
			 */
			l->batch_flink = node_to_free;
			node_to_free = l;
		}
		dst_key += key_size;
		dst_val += value_size;
	}

	htab_unlock_bucket(htab, b, batch, flags);
	locked = false;

	while (node_to_free) {
		l = node_to_free;
		node_to_free = node_to_free->batch_flink;
		if (is_lru_map)
			htab_lru_push_free(htab, l);
		else
			free_htab_elem(htab, l);
	}

next_batch:
	/* If we are not copying data, we can go to next bucket and avoid
	 * unlocking the rcu.
	 */
	if (!bucket_cnt && (batch + 1 < htab->n_buckets)) {
		batch++;
		goto again_nocopy;
	}

	rcu_read_unlock();
	bpf_enable_instrumentation();
	if (bucket_cnt && (copy_to_user(ukeys + total * key_size, keys,
	    key_size * bucket_cnt) ||
	    copy_to_user(uvalues + total * value_size, values,
	    value_size * bucket_cnt))) {
		ret = -EFAULT;
		goto after_loop;
	}

	total += bucket_cnt;
	batch++;
	if (batch >= htab->n_buckets) {
		ret = -ENOENT;
		goto after_loop;
	}
	goto again;

after_loop:
	if (ret == -EFAULT)
		goto out;

	/* copy # of entries and next batch */
	ubatch = u64_to_user_ptr(attr->batch.out_batch);
	if (copy_to_user(ubatch, &batch, sizeof(batch)) ||
	    put_user(total, &uattr->batch.count))
		ret = -EFAULT;

out:
	kvfree(keys);
	kvfree(values);
	return ret;
}

static int
htab_percpu_map_lookup_batch(struct bpf_map *map, const union bpf_attr *attr,
			     union bpf_attr __user *uattr)
{
	return __htab_map_lookup_and_delete_batch(map, attr, uattr, false,
						  false, true);
}

static int
htab_percpu_map_lookup_and_delete_batch(struct bpf_map *map,
					const union bpf_attr *attr,
					union bpf_attr __user *uattr)
{
	return __htab_map_lookup_and_delete_batch(map, attr, uattr, true,
						  false, true);
}

static int
htab_map_lookup_batch(struct bpf_map *map, const union bpf_attr *attr,
		      union bpf_attr __user *uattr)
{
	return __htab_map_lookup_and_delete_batch(map, attr, uattr, false,
						  false, false);
}

static int
htab_map_lookup_and_delete_batch(struct bpf_map *map,
				 const union bpf_attr *attr,
				 union bpf_attr __user *uattr)
{
	return __htab_map_lookup_and_delete_batch(map, attr, uattr, true,
						  false, false);
}

static int
htab_lru_percpu_map_lookup_batch(struct bpf_map *map,
				 const union bpf_attr *attr,
				 union bpf_attr __user *uattr)
{
	return __htab_map_lookup_and_delete_batch(map, attr, uattr, false,
						  true, true);
}

static int
htab_lru_percpu_map_lookup_and_delete_batch(struct bpf_map *map,
					    const union bpf_attr *attr,
					    union bpf_attr __user *uattr)
{
	return __htab_map_lookup_and_delete_batch(map, attr, uattr, true,
						  true, true);
}

static int
htab_lru_map_lookup_batch(struct bpf_map *map, const union bpf_attr *attr,
			  union bpf_attr __user *uattr)
{
	return __htab_map_lookup_and_delete_batch(map, attr, uattr, false,
						  true, false);
}

static int
htab_lru_map_lookup_and_delete_batch(struct bpf_map *map,
				     const union bpf_attr *attr,
				     union bpf_attr __user *uattr)
{
	return __htab_map_lookup_and_delete_batch(map, attr, uattr, true,
						  true, false);
}

struct bpf_iter_seq_hash_map_info {
	struct bpf_map *map;
	struct bpf_htab *htab;
	void *percpu_value_buf; // non-zero means percpu hash
	u32 bucket_id;
	u32 skip_elems;
};

static struct htab_elem *
bpf_hash_map_seq_find_next(struct bpf_iter_seq_hash_map_info *info,
			   struct htab_elem *prev_elem)
{
	const struct bpf_htab *htab = info->htab;
	u32 skip_elems = info->skip_elems;
	u32 bucket_id = info->bucket_id;
	struct hlist_nulls_head *head;
	struct hlist_nulls_node *n;
	struct htab_elem *elem;
	struct bucket *b;
	u32 i, count;

	if (bucket_id >= htab->n_buckets)
		return NULL;

	/* try to find next elem in the same bucket */
	if (prev_elem) {
		/* no update/deletion on this bucket, prev_elem should be still valid
		 * and we won't skip elements.
		 */
		n = rcu_dereference_raw(hlist_nulls_next_rcu(&prev_elem->hash_node));
		elem = hlist_nulls_entry_safe(n, struct htab_elem, hash_node);
		if (elem)
			return elem;

		/* not found, unlock and go to the next bucket */
		b = &htab->buckets[bucket_id++];
		rcu_read_unlock();
		skip_elems = 0;
	}

	for (i = bucket_id; i < htab->n_buckets; i++) {
		b = &htab->buckets[i];
		rcu_read_lock();

		count = 0;
		head = &b->head;
		hlist_nulls_for_each_entry_rcu(elem, n, head, hash_node) {
			if (count >= skip_elems) {
				info->bucket_id = i;
				info->skip_elems = count;
				return elem;
			}
			count++;
		}

		rcu_read_unlock();
		skip_elems = 0;
	}

	info->bucket_id = i;
	info->skip_elems = 0;
	return NULL;
}

static void *bpf_hash_map_seq_start(struct seq_file *seq, loff_t *pos)
{
	struct bpf_iter_seq_hash_map_info *info = seq->private;
	struct htab_elem *elem;

	elem = bpf_hash_map_seq_find_next(info, NULL);
	if (!elem)
		return NULL;

	if (*pos == 0)
		++*pos;
	return elem;
}

static void *bpf_hash_map_seq_next(struct seq_file *seq, void *v, loff_t *pos)
{
	struct bpf_iter_seq_hash_map_info *info = seq->private;

	++*pos;
	++info->skip_elems;
	return bpf_hash_map_seq_find_next(info, v);
}

static int __bpf_hash_map_seq_show(struct seq_file *seq, struct htab_elem *elem)
{
	struct bpf_iter_seq_hash_map_info *info = seq->private;
	u32 roundup_key_size, roundup_value_size;
	struct bpf_iter__bpf_map_elem ctx = {};
	struct bpf_map *map = info->map;
	struct bpf_iter_meta meta;
	int ret = 0, off = 0, cpu;
	struct bpf_prog *prog;
	void __percpu *pptr;

	meta.seq = seq;
	prog = bpf_iter_get_info(&meta, elem == NULL);
	if (prog) {
		ctx.meta = &meta;
		ctx.map = info->map;
		if (elem) {
			roundup_key_size = round_up(map->key_size, 8);
			ctx.key = elem->key;
			if (!info->percpu_value_buf) {
				ctx.value = elem->key + roundup_key_size;
			} else {
				roundup_value_size = round_up(map->value_size, 8);
				pptr = htab_elem_get_ptr(elem, map->key_size);
				for_each_possible_cpu(cpu) {
					copy_map_value_long(map, info->percpu_value_buf + off,
							    per_cpu_ptr(pptr, cpu));
					check_and_init_map_value(map, info->percpu_value_buf + off);
					off += roundup_value_size;
				}
				ctx.value = info->percpu_value_buf;
			}
		}
		ret = bpf_iter_run_prog(prog, &ctx);
	}

	return ret;
}

static int bpf_hash_map_seq_show(struct seq_file *seq, void *v)
{
	return __bpf_hash_map_seq_show(seq, v);
}

static void bpf_hash_map_seq_stop(struct seq_file *seq, void *v)
{
	if (!v)
		(void)__bpf_hash_map_seq_show(seq, NULL);
	else
		rcu_read_unlock();
}

static int bpf_iter_init_hash_map(void *priv_data,
				  struct bpf_iter_aux_info *aux)
{
	struct bpf_iter_seq_hash_map_info *seq_info = priv_data;
	struct bpf_map *map = aux->map;
	void *value_buf;
	u32 buf_size;

	if (map->map_type == BPF_MAP_TYPE_PERCPU_HASH ||
	    map->map_type == BPF_MAP_TYPE_LRU_PERCPU_HASH) {
		buf_size = round_up(map->value_size, 8) * num_possible_cpus();
		value_buf = kmalloc(buf_size, GFP_USER | __GFP_NOWARN);
		if (!value_buf)
			return -ENOMEM;

		seq_info->percpu_value_buf = value_buf;
	}

	bpf_map_inc_with_uref(map);
	seq_info->map = map;
	seq_info->htab = container_of(map, struct bpf_htab, map);
	return 0;
}

static void bpf_iter_fini_hash_map(void *priv_data)
{
	struct bpf_iter_seq_hash_map_info *seq_info = priv_data;

	bpf_map_put_with_uref(seq_info->map);
	kfree(seq_info->percpu_value_buf);
}

static const struct seq_operations bpf_hash_map_seq_ops = {
	.start	= bpf_hash_map_seq_start,
	.next	= bpf_hash_map_seq_next,
	.stop	= bpf_hash_map_seq_stop,
	.show	= bpf_hash_map_seq_show,
};

static const struct bpf_iter_seq_info iter_seq_info = {
	.seq_ops		= &bpf_hash_map_seq_ops,
	.init_seq_private	= bpf_iter_init_hash_map,
	.fini_seq_private	= bpf_iter_fini_hash_map,
	.seq_priv_size		= sizeof(struct bpf_iter_seq_hash_map_info),
};

static long bpf_for_each_hash_elem(struct bpf_map *map, bpf_callback_t callback_fn,
				   void *callback_ctx, u64 flags)
{
	struct bpf_htab *htab = container_of(map, struct bpf_htab, map);
	struct hlist_nulls_head *head;
	struct hlist_nulls_node *n;
	struct htab_elem *elem;
	u32 roundup_key_size;
	int i, num_elems = 0;
	void __percpu *pptr;
	struct bucket *b;
	void *key, *val;
	bool is_percpu;
	u64 ret = 0;

	cant_migrate();

	if (flags != 0)
		return -EINVAL;

	is_percpu = htab_is_percpu(htab);

	roundup_key_size = round_up(map->key_size, 8);
	/* migration has been disabled, so percpu value prepared here will be
	 * the same as the one seen by the bpf program with
	 * bpf_map_lookup_elem().
	 */
	for (i = 0; i < htab->n_buckets; i++) {
		b = &htab->buckets[i];
		rcu_read_lock();
		head = &b->head;
		hlist_nulls_for_each_entry_rcu(elem, n, head, hash_node) {
			key = elem->key;
			if (is_percpu) {
				/* current cpu value for percpu map */
				pptr = htab_elem_get_ptr(elem, map->key_size);
				val = this_cpu_ptr(pptr);
			} else {
				val = elem->key + roundup_key_size;
			}
			num_elems++;
			ret = callback_fn((u64)(long)map, (u64)(long)key,
					  (u64)(long)val, (u64)(long)callback_ctx, 0);
			/* return value: 0 - continue, 1 - stop and return */
			if (ret) {
				rcu_read_unlock();
				goto out;
			}
		}
		rcu_read_unlock();
	}
out:
	return num_elems;
}

static u64 htab_map_mem_usage(const struct bpf_map *map)
{
	struct bpf_htab *htab = container_of(map, struct bpf_htab, map);
	u32 value_size = round_up(htab->map.value_size, 8);
	bool prealloc = htab_is_prealloc(htab);
	bool percpu = htab_is_percpu(htab);
	bool lru = htab_is_lru(htab);
	u64 num_entries;
	u64 usage = sizeof(struct bpf_htab);

	usage += sizeof(struct bucket) * htab->n_buckets;
	usage += sizeof(int) * num_possible_cpus() * HASHTAB_MAP_LOCK_COUNT;
	if (prealloc) {
		num_entries = map->max_entries;
		if (htab_has_extra_elems(htab))
			num_entries += num_possible_cpus();

		usage += htab->elem_size * num_entries;

		if (percpu)
			usage += value_size * num_possible_cpus() * num_entries;
		else if (!lru)
			usage += sizeof(struct htab_elem *) * num_possible_cpus();
	} else {
#define LLIST_NODE_SZ sizeof(struct llist_node)

		num_entries = htab->use_percpu_counter ?
					  percpu_counter_sum(&htab->pcount) :
					  atomic_read(&htab->count);
		usage += (htab->elem_size + LLIST_NODE_SZ) * num_entries;
		if (percpu) {
			usage += (LLIST_NODE_SZ + sizeof(void *)) * num_entries;
			usage += value_size * num_possible_cpus() * num_entries;
		}
	}
	return usage;
}

BTF_ID_LIST_SINGLE(htab_map_btf_ids, struct, bpf_htab)
const struct bpf_map_ops htab_map_ops = {
	.map_meta_equal = bpf_map_meta_equal,
	.map_alloc_check = htab_map_alloc_check,
	.map_alloc = htab_map_alloc,
	.map_free = htab_map_free,
	.map_get_next_key = htab_map_get_next_key,
	.map_release_uref = htab_map_free_timers_and_wq,
	.map_lookup_elem = htab_map_lookup_elem,
	.map_lookup_and_delete_elem = htab_map_lookup_and_delete_elem,
	.map_update_elem = htab_map_update_elem,
	.map_delete_elem = htab_map_delete_elem,
	.map_gen_lookup = htab_map_gen_lookup,
	.map_seq_show_elem = htab_map_seq_show_elem,
	.map_set_for_each_callback_args = map_set_for_each_callback_args,
	.map_for_each_callback = bpf_for_each_hash_elem,
	.map_mem_usage = htab_map_mem_usage,
	BATCH_OPS(htab),
	.map_btf_id = &htab_map_btf_ids[0],
	.iter_seq_info = &iter_seq_info,
};

const struct bpf_map_ops htab_lru_map_ops = {
	.map_meta_equal = bpf_map_meta_equal,
	.map_alloc_check = htab_map_alloc_check,
	.map_alloc = htab_map_alloc,
	.map_free = htab_map_free,
	.map_get_next_key = htab_map_get_next_key,
	.map_release_uref = htab_map_free_timers_and_wq,
	.map_lookup_elem = htab_lru_map_lookup_elem,
	.map_lookup_and_delete_elem = htab_lru_map_lookup_and_delete_elem,
	.map_lookup_elem_sys_only = htab_lru_map_lookup_elem_sys,
	.map_update_elem = htab_lru_map_update_elem,
	.map_delete_elem = htab_lru_map_delete_elem,
	.map_gen_lookup = htab_lru_map_gen_lookup,
	.map_seq_show_elem = htab_map_seq_show_elem,
	.map_set_for_each_callback_args = map_set_for_each_callback_args,
	.map_for_each_callback = bpf_for_each_hash_elem,
	.map_mem_usage = htab_map_mem_usage,
	BATCH_OPS(htab_lru),
	.map_btf_id = &htab_map_btf_ids[0],
	.iter_seq_info = &iter_seq_info,
};

/* Called from eBPF program */
static void *htab_percpu_map_lookup_elem(struct bpf_map *map, void *key)
{
	struct htab_elem *l = __htab_map_lookup_elem(map, key);

	if (l)
		return this_cpu_ptr(htab_elem_get_ptr(l, map->key_size));
	else
		return NULL;
}

/* inline bpf_map_lookup_elem() call for per-CPU hashmap */
static int htab_percpu_map_gen_lookup(struct bpf_map *map, struct bpf_insn *insn_buf)
{
	struct bpf_insn *insn = insn_buf;

	if (!bpf_jit_supports_percpu_insn())
		return -EOPNOTSUPP;

	BUILD_BUG_ON(!__same_type(&__htab_map_lookup_elem,
		     (void *(*)(struct bpf_map *map, void *key))NULL));
	*insn++ = BPF_EMIT_CALL(__htab_map_lookup_elem);
	*insn++ = BPF_JMP_IMM(BPF_JEQ, BPF_REG_0, 0, 3);
	*insn++ = BPF_ALU64_IMM(BPF_ADD, BPF_REG_0,
				offsetof(struct htab_elem, key) + roundup(map->key_size, 8));
	*insn++ = BPF_LDX_MEM(BPF_DW, BPF_REG_0, BPF_REG_0, 0);
	*insn++ = BPF_MOV64_PERCPU_REG(BPF_REG_0, BPF_REG_0);

	return insn - insn_buf;
}

static void *htab_percpu_map_lookup_percpu_elem(struct bpf_map *map, void *key, u32 cpu)
{
	struct htab_elem *l;

	if (cpu >= nr_cpu_ids)
		return NULL;

	l = __htab_map_lookup_elem(map, key);
	if (l)
		return per_cpu_ptr(htab_elem_get_ptr(l, map->key_size), cpu);
	else
		return NULL;
}

static void *htab_lru_percpu_map_lookup_elem(struct bpf_map *map, void *key)
{
	struct htab_elem *l = __htab_map_lookup_elem(map, key);

	if (l) {
		bpf_lru_node_set_ref(&l->lru_node);
		return this_cpu_ptr(htab_elem_get_ptr(l, map->key_size));
	}

	return NULL;
}

static void *htab_lru_percpu_map_lookup_percpu_elem(struct bpf_map *map, void *key, u32 cpu)
{
	struct htab_elem *l;

	if (cpu >= nr_cpu_ids)
		return NULL;

	l = __htab_map_lookup_elem(map, key);
	if (l) {
		bpf_lru_node_set_ref(&l->lru_node);
		return per_cpu_ptr(htab_elem_get_ptr(l, map->key_size), cpu);
	}

	return NULL;
}

int bpf_percpu_hash_copy(struct bpf_map *map, void *key, void *value)
{
	struct htab_elem *l;
	void __percpu *pptr;
	int ret = -ENOENT;
	int cpu, off = 0;
	u32 size;

	/* per_cpu areas are zero-filled and bpf programs can only
	 * access 'value_size' of them, so copying rounded areas
	 * will not leak any kernel data
	 */
	size = round_up(map->value_size, 8);
	rcu_read_lock();
	l = __htab_map_lookup_elem(map, key);
	if (!l)
		goto out;
	/* We do not mark LRU map element here in order to not mess up
	 * eviction heuristics when user space does a map walk.
	 */
	pptr = htab_elem_get_ptr(l, map->key_size);
	for_each_possible_cpu(cpu) {
		copy_map_value_long(map, value + off, per_cpu_ptr(pptr, cpu));
		check_and_init_map_value(map, value + off);
		off += size;
	}
	ret = 0;
out:
	rcu_read_unlock();
	return ret;
}

int bpf_percpu_hash_update(struct bpf_map *map, void *key, void *value,
			   u64 map_flags)
{
	struct bpf_htab *htab = container_of(map, struct bpf_htab, map);
	int ret;

	rcu_read_lock();
	if (htab_is_lru(htab))
		ret = __htab_lru_percpu_map_update_elem(map, key, value,
							map_flags, true);
	else
		ret = __htab_percpu_map_update_elem(map, key, value, map_flags,
						    true);
	rcu_read_unlock();

	return ret;
}

static void htab_percpu_map_seq_show_elem(struct bpf_map *map, void *key,
					  struct seq_file *m)
{
	struct htab_elem *l;
	void __percpu *pptr;
	int cpu;

	rcu_read_lock();

	l = __htab_map_lookup_elem(map, key);
	if (!l) {
		rcu_read_unlock();
		return;
	}

	btf_type_seq_show(map->btf, map->btf_key_type_id, key, m);
	seq_puts(m, ": {\n");
	pptr = htab_elem_get_ptr(l, map->key_size);
	for_each_possible_cpu(cpu) {
		seq_printf(m, "\tcpu%d: ", cpu);
		btf_type_seq_show(map->btf, map->btf_value_type_id,
				  per_cpu_ptr(pptr, cpu), m);
		seq_putc(m, '\n');
	}
	seq_puts(m, "}\n");

	rcu_read_unlock();
}

const struct bpf_map_ops htab_percpu_map_ops = {
	.map_meta_equal = bpf_map_meta_equal,
	.map_alloc_check = htab_map_alloc_check,
	.map_alloc = htab_map_alloc,
	.map_free = htab_map_free,
	.map_get_next_key = htab_map_get_next_key,
	.map_lookup_elem = htab_percpu_map_lookup_elem,
	.map_gen_lookup = htab_percpu_map_gen_lookup,
	.map_lookup_and_delete_elem = htab_percpu_map_lookup_and_delete_elem,
	.map_update_elem = htab_percpu_map_update_elem,
	.map_delete_elem = htab_map_delete_elem,
	.map_lookup_percpu_elem = htab_percpu_map_lookup_percpu_elem,
	.map_seq_show_elem = htab_percpu_map_seq_show_elem,
	.map_set_for_each_callback_args = map_set_for_each_callback_args,
	.map_for_each_callback = bpf_for_each_hash_elem,
	.map_mem_usage = htab_map_mem_usage,
	BATCH_OPS(htab_percpu),
	.map_btf_id = &htab_map_btf_ids[0],
	.iter_seq_info = &iter_seq_info,
};

const struct bpf_map_ops htab_lru_percpu_map_ops = {
	.map_meta_equal = bpf_map_meta_equal,
	.map_alloc_check = htab_map_alloc_check,
	.map_alloc = htab_map_alloc,
	.map_free = htab_map_free,
	.map_get_next_key = htab_map_get_next_key,
	.map_lookup_elem = htab_lru_percpu_map_lookup_elem,
	.map_lookup_and_delete_elem = htab_lru_percpu_map_lookup_and_delete_elem,
	.map_update_elem = htab_lru_percpu_map_update_elem,
	.map_delete_elem = htab_lru_map_delete_elem,
	.map_lookup_percpu_elem = htab_lru_percpu_map_lookup_percpu_elem,
	.map_seq_show_elem = htab_percpu_map_seq_show_elem,
	.map_set_for_each_callback_args = map_set_for_each_callback_args,
	.map_for_each_callback = bpf_for_each_hash_elem,
	.map_mem_usage = htab_map_mem_usage,
	BATCH_OPS(htab_lru_percpu),
	.map_btf_id = &htab_map_btf_ids[0],
	.iter_seq_info = &iter_seq_info,
};

static int fd_htab_map_alloc_check(union bpf_attr *attr)
{
	if (attr->value_size != sizeof(u32))
		return -EINVAL;
	return htab_map_alloc_check(attr);
}

static void fd_htab_map_free(struct bpf_map *map)
{
	struct bpf_htab *htab = container_of(map, struct bpf_htab, map);
	struct hlist_nulls_node *n;
	struct hlist_nulls_head *head;
	struct htab_elem *l;
	int i;

	for (i = 0; i < htab->n_buckets; i++) {
		head = select_bucket(htab, i);

		hlist_nulls_for_each_entry_safe(l, n, head, hash_node) {
			void *ptr = fd_htab_map_get_ptr(map, l);

			map->ops->map_fd_put_ptr(map, ptr, false);
		}
	}

	htab_map_free(map);
}

/* only called from syscall */
int bpf_fd_htab_map_lookup_elem(struct bpf_map *map, void *key, u32 *value)
{
	void **ptr;
	int ret = 0;

	if (!map->ops->map_fd_sys_lookup_elem)
		return -ENOTSUPP;

	rcu_read_lock();
	ptr = htab_map_lookup_elem(map, key);
	if (ptr)
		*value = map->ops->map_fd_sys_lookup_elem(READ_ONCE(*ptr));
	else
		ret = -ENOENT;
	rcu_read_unlock();

	return ret;
}

/* only called from syscall */
int bpf_fd_htab_map_update_elem(struct bpf_map *map, struct file *map_file,
				void *key, void *value, u64 map_flags)
{
	void *ptr;
	int ret;
	u32 ufd = *(u32 *)value;

	ptr = map->ops->map_fd_get_ptr(map, map_file, ufd);
	if (IS_ERR(ptr))
		return PTR_ERR(ptr);

	/* The htab bucket lock is always held during update operations in fd
	 * htab map, and the following rcu_read_lock() is only used to avoid
	 * the WARN_ON_ONCE in htab_map_update_elem().
	 */
	rcu_read_lock();
	ret = htab_map_update_elem(map, key, &ptr, map_flags);
	rcu_read_unlock();
	if (ret)
		map->ops->map_fd_put_ptr(map, ptr, false);

	return ret;
}

static struct bpf_map *htab_of_map_alloc(union bpf_attr *attr)
{
	struct bpf_map *map, *inner_map_meta;

	inner_map_meta = bpf_map_meta_alloc(attr->inner_map_fd);
	if (IS_ERR(inner_map_meta))
		return inner_map_meta;

	map = htab_map_alloc(attr);
	if (IS_ERR(map)) {
		bpf_map_meta_free(inner_map_meta);
		return map;
	}

	map->inner_map_meta = inner_map_meta;

	return map;
}

static void *htab_of_map_lookup_elem(struct bpf_map *map, void *key)
{
	struct bpf_map **inner_map  = htab_map_lookup_elem(map, key);

	if (!inner_map)
		return NULL;

	return READ_ONCE(*inner_map);
}

static int htab_of_map_gen_lookup(struct bpf_map *map,
				  struct bpf_insn *insn_buf)
{
	struct bpf_insn *insn = insn_buf;
	const int ret = BPF_REG_0;

	BUILD_BUG_ON(!__same_type(&__htab_map_lookup_elem,
		     (void *(*)(struct bpf_map *map, void *key))NULL));
	*insn++ = BPF_EMIT_CALL(__htab_map_lookup_elem);
	*insn++ = BPF_JMP_IMM(BPF_JEQ, ret, 0, 2);
	*insn++ = BPF_ALU64_IMM(BPF_ADD, ret,
				offsetof(struct htab_elem, key) +
				round_up(map->key_size, 8));
	*insn++ = BPF_LDX_MEM(BPF_DW, ret, ret, 0);

	return insn - insn_buf;
}

static void htab_of_map_free(struct bpf_map *map)
{
	bpf_map_meta_free(map->inner_map_meta);
	fd_htab_map_free(map);
}

const struct bpf_map_ops htab_of_maps_map_ops = {
	.map_alloc_check = fd_htab_map_alloc_check,
	.map_alloc = htab_of_map_alloc,
	.map_free = htab_of_map_free,
	.map_get_next_key = htab_map_get_next_key,
	.map_lookup_elem = htab_of_map_lookup_elem,
	.map_delete_elem = htab_map_delete_elem,
	.map_fd_get_ptr = bpf_map_fd_get_ptr,
	.map_fd_put_ptr = bpf_map_fd_put_ptr,
	.map_fd_sys_lookup_elem = bpf_map_fd_sys_lookup_elem,
	.map_gen_lookup = htab_of_map_gen_lookup,
	.map_check_btf = map_check_no_btf,
	.map_mem_usage = htab_map_mem_usage,
	BATCH_OPS(htab),
	.map_btf_id = &htab_map_btf_ids[0],
};
