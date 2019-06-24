// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2011-2014 PLUMgrid, http://plumgrid.com
 * Copyright (c) 2016 Facebook
 */
#include <linux/bpf.h>
#include <linux/btf.h>
#include <linux/jhash.h>
#include <linux/filter.h>
#include <linux/rculist_nulls.h>
#include <linux/random.h>
#include <uapi/linux/btf.h>
#include "percpu_freelist.h"
#include "bpf_lru_list.h"
#include "map_in_map.h"

#define HTAB_CREATE_FLAG_MASK						\
	(BPF_F_NO_PREALLOC | BPF_F_NO_COMMON_LRU | BPF_F_NUMA_NODE |	\
	 BPF_F_ACCESS_MASK | BPF_F_ZERO_SEED)

struct bucket {
	struct hlist_nulls_head head;
	raw_spinlock_t lock;
};

struct bpf_htab {
	struct bpf_map map;
	struct bucket *buckets;
	void *elems;
	union {
		struct pcpu_freelist freelist;
		struct bpf_lru lru;
	};
	struct htab_elem *__percpu *extra_elems;
	atomic_t count;	/* number of elements in this hashtable */
	u32 n_buckets;	/* number of hash buckets */
	u32 elem_size;	/* size of each element in bytes */
	u32 hashrnd;
};

/* each htab element is struct htab_elem + key + value */
struct htab_elem {
	union {
		struct hlist_nulls_node hash_node;
		struct {
			void *padding;
			union {
				struct bpf_htab *htab;
				struct pcpu_freelist_node fnode;
			};
		};
	};
	union {
		struct rcu_head rcu;
		struct bpf_lru_node lru_node;
	};
	u32 hash;
	char key[0] __aligned(8);
};

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

static bool htab_is_prealloc(const struct bpf_htab *htab)
{
	return !(htab->map.map_flags & BPF_F_NO_PREALLOC);
}

static inline void htab_elem_set_ptr(struct htab_elem *l, u32 key_size,
				     void __percpu *pptr)
{
	*(void __percpu **)(l->key + key_size) = pptr;
}

static inline void __percpu *htab_elem_get_ptr(struct htab_elem *l, u32 key_size)
{
	return *(void __percpu **)(l->key + key_size);
}

static void *fd_htab_map_get_ptr(const struct bpf_map *map, struct htab_elem *l)
{
	return *(void **)(l->key + roundup(map->key_size, 8));
}

static struct htab_elem *get_htab_elem(struct bpf_htab *htab, int i)
{
	return (struct htab_elem *) (htab->elems + i * htab->elem_size);
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

static struct htab_elem *prealloc_lru_pop(struct bpf_htab *htab, void *key,
					  u32 hash)
{
	struct bpf_lru_node *node = bpf_lru_pop_free(&htab->lru, hash);
	struct htab_elem *l;

	if (node) {
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

	if (!htab_is_percpu(htab) && !htab_is_lru(htab))
		num_entries += num_possible_cpus();

	htab->elems = bpf_map_area_alloc(htab->elem_size * num_entries,
					 htab->map.numa_node);
	if (!htab->elems)
		return -ENOMEM;

	if (!htab_is_percpu(htab))
		goto skip_percpu_elems;

	for (i = 0; i < num_entries; i++) {
		u32 size = round_up(htab->map.value_size, 8);
		void __percpu *pptr;

		pptr = __alloc_percpu_gfp(size, 8, GFP_USER | __GFP_NOWARN);
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

	pptr = __alloc_percpu_gfp(sizeof(struct htab_elem *), 8,
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

	BUILD_BUG_ON(offsetof(struct htab_elem, htab) !=
		     offsetof(struct htab_elem, hash_node.pprev));
	BUILD_BUG_ON(offsetof(struct htab_elem, fnode.next) !=
		     offsetof(struct htab_elem, hash_node.pprev));

	if (lru && !capable(CAP_SYS_ADMIN))
		/* LRU implementation is much complicated than other
		 * maps.  Hence, limit to CAP_SYS_ADMIN for now.
		 */
		return -EPERM;

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

	if (attr->key_size > MAX_BPF_STACK)
		/* eBPF programs initialize keys on stack, so they cannot be
		 * larger than max stack size
		 */
		return -E2BIG;

	if (attr->value_size >= KMALLOC_MAX_SIZE -
	    MAX_BPF_STACK - sizeof(struct htab_elem))
		/* if value_size is bigger, the user space won't be able to
		 * access the elements via bpf syscall. This check also makes
		 * sure that the elem_size doesn't overflow and it's
		 * kmalloc-able later in htab_map_update_elem()
		 */
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
	u64 cost;

	htab = kzalloc(sizeof(*htab), GFP_USER);
	if (!htab)
		return ERR_PTR(-ENOMEM);

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

	/* hash table size must be power of 2 */
	htab->n_buckets = roundup_pow_of_two(htab->map.max_entries);

	htab->elem_size = sizeof(struct htab_elem) +
			  round_up(htab->map.key_size, 8);
	if (percpu)
		htab->elem_size += sizeof(void *);
	else
		htab->elem_size += round_up(htab->map.value_size, 8);

	err = -E2BIG;
	/* prevent zero size kmalloc and check for u32 overflow */
	if (htab->n_buckets == 0 ||
	    htab->n_buckets > U32_MAX / sizeof(struct bucket))
		goto free_htab;

	cost = (u64) htab->n_buckets * sizeof(struct bucket) +
	       (u64) htab->elem_size * htab->map.max_entries;

	if (percpu)
		cost += (u64) round_up(htab->map.value_size, 8) *
			num_possible_cpus() * htab->map.max_entries;
	else
	       cost += (u64) htab->elem_size * num_possible_cpus();

	/* if map size is larger than memlock limit, reject it */
	err = bpf_map_charge_init(&htab->map.memory, cost);
	if (err)
		goto free_htab;

	err = -ENOMEM;
	htab->buckets = bpf_map_area_alloc(htab->n_buckets *
					   sizeof(struct bucket),
					   htab->map.numa_node);
	if (!htab->buckets)
		goto free_charge;

	if (htab->map.map_flags & BPF_F_ZERO_SEED)
		htab->hashrnd = 0;
	else
		htab->hashrnd = get_random_int();

	for (i = 0; i < htab->n_buckets; i++) {
		INIT_HLIST_NULLS_HEAD(&htab->buckets[i].head, i);
		raw_spin_lock_init(&htab->buckets[i].lock);
	}

	if (prealloc) {
		err = prealloc_init(htab);
		if (err)
			goto free_buckets;

		if (!percpu && !lru) {
			/* lru itself can remove the least used element, so
			 * there is no need for an extra elem during map_update.
			 */
			err = alloc_extra_elems(htab);
			if (err)
				goto free_prealloc;
		}
	}

	return &htab->map;

free_prealloc:
	prealloc_destroy(htab);
free_buckets:
	bpf_map_area_free(htab->buckets);
free_charge:
	bpf_map_charge_finish(&htab->map.memory);
free_htab:
	kfree(htab);
	return ERR_PTR(err);
}

static inline u32 htab_map_hash(const void *key, u32 key_len, u32 hashrnd)
{
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

	/* Must be called with rcu_read_lock. */
	WARN_ON_ONCE(!rcu_read_lock_held());

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
static u32 htab_map_gen_lookup(struct bpf_map *map, struct bpf_insn *insn_buf)
{
	struct bpf_insn *insn = insn_buf;
	const int ret = BPF_REG_0;

	BUILD_BUG_ON(!__same_type(&__htab_map_lookup_elem,
		     (void *(*)(struct bpf_map *map, void *key))NULL));
	*insn++ = BPF_EMIT_CALL(BPF_CAST_CALL(__htab_map_lookup_elem));
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

static u32 htab_lru_map_gen_lookup(struct bpf_map *map,
				   struct bpf_insn *insn_buf)
{
	struct bpf_insn *insn = insn_buf;
	const int ret = BPF_REG_0;
	const int ref_reg = BPF_REG_1;

	BUILD_BUG_ON(!__same_type(&__htab_map_lookup_elem,
		     (void *(*)(struct bpf_map *map, void *key))NULL));
	*insn++ = BPF_EMIT_CALL(BPF_CAST_CALL(__htab_map_lookup_elem));
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

/* It is called from the bpf_lru_list when the LRU needs to delete
 * older elements from the htab.
 */
static bool htab_lru_map_delete_node(void *arg, struct bpf_lru_node *node)
{
	struct bpf_htab *htab = (struct bpf_htab *)arg;
	struct htab_elem *l = NULL, *tgt_l;
	struct hlist_nulls_head *head;
	struct hlist_nulls_node *n;
	unsigned long flags;
	struct bucket *b;

	tgt_l = container_of(node, struct htab_elem, lru_node);
	b = __select_bucket(htab, tgt_l->hash);
	head = &b->head;

	raw_spin_lock_irqsave(&b->lock, flags);

	hlist_nulls_for_each_entry_rcu(l, n, head, hash_node)
		if (l == tgt_l) {
			hlist_nulls_del_rcu(&l->hash_node);
			break;
		}

	raw_spin_unlock_irqrestore(&b->lock, flags);

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
	if (htab->map.map_type == BPF_MAP_TYPE_PERCPU_HASH)
		free_percpu(htab_elem_get_ptr(l, htab->map.key_size));
	kfree(l);
}

static void htab_elem_free_rcu(struct rcu_head *head)
{
	struct htab_elem *l = container_of(head, struct htab_elem, rcu);
	struct bpf_htab *htab = l->htab;

	/* must increment bpf_prog_active to avoid kprobe+bpf triggering while
	 * we're calling kfree, otherwise deadlock is possible if kprobes
	 * are placed somewhere inside of slub
	 */
	preempt_disable();
	__this_cpu_inc(bpf_prog_active);
	htab_elem_free(htab, l);
	__this_cpu_dec(bpf_prog_active);
	preempt_enable();
}

static void free_htab_elem(struct bpf_htab *htab, struct htab_elem *l)
{
	struct bpf_map *map = &htab->map;

	if (map->ops->map_fd_put_ptr) {
		void *ptr = fd_htab_map_get_ptr(map, l);

		map->ops->map_fd_put_ptr(ptr);
	}

	if (htab_is_prealloc(htab)) {
		__pcpu_freelist_push(&htab->freelist, &l->fnode);
	} else {
		atomic_dec(&htab->count);
		l->htab = htab;
		call_rcu(&l->rcu, htab_elem_free_rcu);
	}
}

static void pcpu_copy_value(struct bpf_htab *htab, void __percpu *pptr,
			    void *value, bool onallcpus)
{
	if (!onallcpus) {
		/* copy true value_size bytes */
		memcpy(this_cpu_ptr(pptr), value, htab->map.value_size);
	} else {
		u32 size = round_up(htab->map.value_size, 8);
		int off = 0, cpu;

		for_each_possible_cpu(cpu) {
			bpf_long_memcpy(per_cpu_ptr(pptr, cpu),
					value + off, size);
			off += size;
		}
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
		}
	} else {
		if (atomic_inc_return(&htab->count) > htab->map.max_entries)
			if (!old_elem) {
				/* when map is full and update() is replacing
				 * old element, it's ok to allocate, since
				 * old element will be freed immediately.
				 * Otherwise return an error
				 */
				l_new = ERR_PTR(-E2BIG);
				goto dec_count;
			}
		l_new = kmalloc_node(htab->elem_size, GFP_ATOMIC | __GFP_NOWARN,
				     htab->map.numa_node);
		if (!l_new) {
			l_new = ERR_PTR(-ENOMEM);
			goto dec_count;
		}
		check_and_init_map_lock(&htab->map,
					l_new->key + round_up(key_size, 8));
	}

	memcpy(l_new->key, key, key_size);
	if (percpu) {
		size = round_up(size, 8);
		if (prealloc) {
			pptr = htab_elem_get_ptr(l_new, key_size);
		} else {
			/* alloc_percpu zero-fills */
			pptr = __alloc_percpu_gfp(size, 8,
						  GFP_ATOMIC | __GFP_NOWARN);
			if (!pptr) {
				kfree(l_new);
				l_new = ERR_PTR(-ENOMEM);
				goto dec_count;
			}
		}

		pcpu_copy_value(htab, pptr, value, onallcpus);

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
	atomic_dec(&htab->count);
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
static int htab_map_update_elem(struct bpf_map *map, void *key, void *value,
				u64 map_flags)
{
	struct bpf_htab *htab = container_of(map, struct bpf_htab, map);
	struct htab_elem *l_new = NULL, *l_old;
	struct hlist_nulls_head *head;
	unsigned long flags;
	struct bucket *b;
	u32 key_size, hash;
	int ret;

	if (unlikely((map_flags & ~BPF_F_LOCK) > BPF_EXIST))
		/* unknown flags */
		return -EINVAL;

	WARN_ON_ONCE(!rcu_read_lock_held());

	key_size = map->key_size;

	hash = htab_map_hash(key, key_size, htab->hashrnd);

	b = __select_bucket(htab, hash);
	head = &b->head;

	if (unlikely(map_flags & BPF_F_LOCK)) {
		if (unlikely(!map_value_has_spin_lock(map)))
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

	/* bpf_map_update_elem() can be called in_irq() */
	raw_spin_lock_irqsave(&b->lock, flags);

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
		if (!htab_is_prealloc(htab))
			free_htab_elem(htab, l_old);
	}
	ret = 0;
err:
	raw_spin_unlock_irqrestore(&b->lock, flags);
	return ret;
}

static int htab_lru_map_update_elem(struct bpf_map *map, void *key, void *value,
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

	WARN_ON_ONCE(!rcu_read_lock_held());

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
	memcpy(l_new->key + round_up(map->key_size, 8), value, map->value_size);

	/* bpf_map_update_elem() can be called in_irq() */
	raw_spin_lock_irqsave(&b->lock, flags);

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
	raw_spin_unlock_irqrestore(&b->lock, flags);

	if (ret)
		bpf_lru_push_free(&htab->lru, &l_new->lru_node);
	else if (l_old)
		bpf_lru_push_free(&htab->lru, &l_old->lru_node);

	return ret;
}

static int __htab_percpu_map_update_elem(struct bpf_map *map, void *key,
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

	WARN_ON_ONCE(!rcu_read_lock_held());

	key_size = map->key_size;

	hash = htab_map_hash(key, key_size, htab->hashrnd);

	b = __select_bucket(htab, hash);
	head = &b->head;

	/* bpf_map_update_elem() can be called in_irq() */
	raw_spin_lock_irqsave(&b->lock, flags);

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
	raw_spin_unlock_irqrestore(&b->lock, flags);
	return ret;
}

static int __htab_lru_percpu_map_update_elem(struct bpf_map *map, void *key,
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

	WARN_ON_ONCE(!rcu_read_lock_held());

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

	/* bpf_map_update_elem() can be called in_irq() */
	raw_spin_lock_irqsave(&b->lock, flags);

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
		pcpu_copy_value(htab, htab_elem_get_ptr(l_new, key_size),
				value, onallcpus);
		hlist_nulls_add_head_rcu(&l_new->hash_node, head);
		l_new = NULL;
	}
	ret = 0;
err:
	raw_spin_unlock_irqrestore(&b->lock, flags);
	if (l_new)
		bpf_lru_push_free(&htab->lru, &l_new->lru_node);
	return ret;
}

static int htab_percpu_map_update_elem(struct bpf_map *map, void *key,
				       void *value, u64 map_flags)
{
	return __htab_percpu_map_update_elem(map, key, value, map_flags, false);
}

static int htab_lru_percpu_map_update_elem(struct bpf_map *map, void *key,
					   void *value, u64 map_flags)
{
	return __htab_lru_percpu_map_update_elem(map, key, value, map_flags,
						 false);
}

/* Called from syscall or from eBPF program */
static int htab_map_delete_elem(struct bpf_map *map, void *key)
{
	struct bpf_htab *htab = container_of(map, struct bpf_htab, map);
	struct hlist_nulls_head *head;
	struct bucket *b;
	struct htab_elem *l;
	unsigned long flags;
	u32 hash, key_size;
	int ret = -ENOENT;

	WARN_ON_ONCE(!rcu_read_lock_held());

	key_size = map->key_size;

	hash = htab_map_hash(key, key_size, htab->hashrnd);
	b = __select_bucket(htab, hash);
	head = &b->head;

	raw_spin_lock_irqsave(&b->lock, flags);

	l = lookup_elem_raw(head, hash, key, key_size);

	if (l) {
		hlist_nulls_del_rcu(&l->hash_node);
		free_htab_elem(htab, l);
		ret = 0;
	}

	raw_spin_unlock_irqrestore(&b->lock, flags);
	return ret;
}

static int htab_lru_map_delete_elem(struct bpf_map *map, void *key)
{
	struct bpf_htab *htab = container_of(map, struct bpf_htab, map);
	struct hlist_nulls_head *head;
	struct bucket *b;
	struct htab_elem *l;
	unsigned long flags;
	u32 hash, key_size;
	int ret = -ENOENT;

	WARN_ON_ONCE(!rcu_read_lock_held());

	key_size = map->key_size;

	hash = htab_map_hash(key, key_size, htab->hashrnd);
	b = __select_bucket(htab, hash);
	head = &b->head;

	raw_spin_lock_irqsave(&b->lock, flags);

	l = lookup_elem_raw(head, hash, key, key_size);

	if (l) {
		hlist_nulls_del_rcu(&l->hash_node);
		ret = 0;
	}

	raw_spin_unlock_irqrestore(&b->lock, flags);
	if (l)
		bpf_lru_push_free(&htab->lru, &l->lru_node);
	return ret;
}

static void delete_all_elements(struct bpf_htab *htab)
{
	int i;

	for (i = 0; i < htab->n_buckets; i++) {
		struct hlist_nulls_head *head = select_bucket(htab, i);
		struct hlist_nulls_node *n;
		struct htab_elem *l;

		hlist_nulls_for_each_entry_safe(l, n, head, hash_node) {
			hlist_nulls_del_rcu(&l->hash_node);
			htab_elem_free(htab, l);
		}
	}
}

/* Called when map->refcnt goes to zero, either from workqueue or from syscall */
static void htab_map_free(struct bpf_map *map)
{
	struct bpf_htab *htab = container_of(map, struct bpf_htab, map);

	/* at this point bpf_prog->aux->refcnt == 0 and this map->refcnt == 0,
	 * so the programs (can be more than one that used this map) were
	 * disconnected from events. Wait for outstanding critical sections in
	 * these programs to complete
	 */
	synchronize_rcu();

	/* some of free_htab_elem() callbacks for elements of this map may
	 * not have executed. Wait for them.
	 */
	rcu_barrier();
	if (!htab_is_prealloc(htab))
		delete_all_elements(htab);
	else
		prealloc_destroy(htab);

	free_percpu(htab->extra_elems);
	bpf_map_area_free(htab->buckets);
	kfree(htab);
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
	seq_puts(m, "\n");

	rcu_read_unlock();
}

const struct bpf_map_ops htab_map_ops = {
	.map_alloc_check = htab_map_alloc_check,
	.map_alloc = htab_map_alloc,
	.map_free = htab_map_free,
	.map_get_next_key = htab_map_get_next_key,
	.map_lookup_elem = htab_map_lookup_elem,
	.map_update_elem = htab_map_update_elem,
	.map_delete_elem = htab_map_delete_elem,
	.map_gen_lookup = htab_map_gen_lookup,
	.map_seq_show_elem = htab_map_seq_show_elem,
};

const struct bpf_map_ops htab_lru_map_ops = {
	.map_alloc_check = htab_map_alloc_check,
	.map_alloc = htab_map_alloc,
	.map_free = htab_map_free,
	.map_get_next_key = htab_map_get_next_key,
	.map_lookup_elem = htab_lru_map_lookup_elem,
	.map_lookup_elem_sys_only = htab_lru_map_lookup_elem_sys,
	.map_update_elem = htab_lru_map_update_elem,
	.map_delete_elem = htab_lru_map_delete_elem,
	.map_gen_lookup = htab_lru_map_gen_lookup,
	.map_seq_show_elem = htab_map_seq_show_elem,
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

static void *htab_lru_percpu_map_lookup_elem(struct bpf_map *map, void *key)
{
	struct htab_elem *l = __htab_map_lookup_elem(map, key);

	if (l) {
		bpf_lru_node_set_ref(&l->lru_node);
		return this_cpu_ptr(htab_elem_get_ptr(l, map->key_size));
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
		bpf_long_memcpy(value + off,
				per_cpu_ptr(pptr, cpu), size);
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
		seq_puts(m, "\n");
	}
	seq_puts(m, "}\n");

	rcu_read_unlock();
}

const struct bpf_map_ops htab_percpu_map_ops = {
	.map_alloc_check = htab_map_alloc_check,
	.map_alloc = htab_map_alloc,
	.map_free = htab_map_free,
	.map_get_next_key = htab_map_get_next_key,
	.map_lookup_elem = htab_percpu_map_lookup_elem,
	.map_update_elem = htab_percpu_map_update_elem,
	.map_delete_elem = htab_map_delete_elem,
	.map_seq_show_elem = htab_percpu_map_seq_show_elem,
};

const struct bpf_map_ops htab_lru_percpu_map_ops = {
	.map_alloc_check = htab_map_alloc_check,
	.map_alloc = htab_map_alloc,
	.map_free = htab_map_free,
	.map_get_next_key = htab_map_get_next_key,
	.map_lookup_elem = htab_lru_percpu_map_lookup_elem,
	.map_update_elem = htab_lru_percpu_map_update_elem,
	.map_delete_elem = htab_lru_map_delete_elem,
	.map_seq_show_elem = htab_percpu_map_seq_show_elem,
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

			map->ops->map_fd_put_ptr(ptr);
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

	ret = htab_map_update_elem(map, key, &ptr, map_flags);
	if (ret)
		map->ops->map_fd_put_ptr(ptr);

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

static u32 htab_of_map_gen_lookup(struct bpf_map *map,
				  struct bpf_insn *insn_buf)
{
	struct bpf_insn *insn = insn_buf;
	const int ret = BPF_REG_0;

	BUILD_BUG_ON(!__same_type(&__htab_map_lookup_elem,
		     (void *(*)(struct bpf_map *map, void *key))NULL));
	*insn++ = BPF_EMIT_CALL(BPF_CAST_CALL(__htab_map_lookup_elem));
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
};
