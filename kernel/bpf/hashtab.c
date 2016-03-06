/* Copyright (c) 2011-2014 PLUMgrid, http://plumgrid.com
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 */
#include <linux/bpf.h>
#include <linux/jhash.h>
#include <linux/filter.h>
#include <linux/vmalloc.h>

struct bucket {
	struct hlist_head head;
	raw_spinlock_t lock;
};

struct bpf_htab {
	struct bpf_map map;
	struct bucket *buckets;
	atomic_t count;	/* number of elements in this hashtable */
	u32 n_buckets;	/* number of hash buckets */
	u32 elem_size;	/* size of each element in bytes */
};

/* each htab element is struct htab_elem + key + value */
struct htab_elem {
	struct hlist_node hash_node;
	struct rcu_head rcu;
	union {
		u32 hash;
		u32 key_size;
	};
	char key[0] __aligned(8);
};

/* Called from syscall */
static struct bpf_map *htab_map_alloc(union bpf_attr *attr)
{
	bool percpu = attr->map_type == BPF_MAP_TYPE_PERCPU_HASH;
	struct bpf_htab *htab;
	int err, i;
	u64 cost;

	htab = kzalloc(sizeof(*htab), GFP_USER);
	if (!htab)
		return ERR_PTR(-ENOMEM);

	/* mandatory map attributes */
	htab->map.map_type = attr->map_type;
	htab->map.key_size = attr->key_size;
	htab->map.value_size = attr->value_size;
	htab->map.max_entries = attr->max_entries;

	/* check sanity of attributes.
	 * value_size == 0 may be allowed in the future to use map as a set
	 */
	err = -EINVAL;
	if (htab->map.max_entries == 0 || htab->map.key_size == 0 ||
	    htab->map.value_size == 0)
		goto free_htab;

	/* hash table size must be power of 2 */
	htab->n_buckets = roundup_pow_of_two(htab->map.max_entries);

	err = -E2BIG;
	if (htab->map.key_size > MAX_BPF_STACK)
		/* eBPF programs initialize keys on stack, so they cannot be
		 * larger than max stack size
		 */
		goto free_htab;

	if (htab->map.value_size >= (1 << (KMALLOC_SHIFT_MAX - 1)) -
	    MAX_BPF_STACK - sizeof(struct htab_elem))
		/* if value_size is bigger, the user space won't be able to
		 * access the elements via bpf syscall. This check also makes
		 * sure that the elem_size doesn't overflow and it's
		 * kmalloc-able later in htab_map_update_elem()
		 */
		goto free_htab;

	if (percpu && round_up(htab->map.value_size, 8) > PCPU_MIN_UNIT_SIZE)
		/* make sure the size for pcpu_alloc() is reasonable */
		goto free_htab;

	htab->elem_size = sizeof(struct htab_elem) +
			  round_up(htab->map.key_size, 8);
	if (percpu)
		htab->elem_size += sizeof(void *);
	else
		htab->elem_size += htab->map.value_size;

	/* prevent zero size kmalloc and check for u32 overflow */
	if (htab->n_buckets == 0 ||
	    htab->n_buckets > U32_MAX / sizeof(struct bucket))
		goto free_htab;

	cost = (u64) htab->n_buckets * sizeof(struct bucket) +
	       (u64) htab->elem_size * htab->map.max_entries;

	if (percpu)
		cost += (u64) round_up(htab->map.value_size, 8) *
			num_possible_cpus() * htab->map.max_entries;

	if (cost >= U32_MAX - PAGE_SIZE)
		/* make sure page count doesn't overflow */
		goto free_htab;

	htab->map.pages = round_up(cost, PAGE_SIZE) >> PAGE_SHIFT;

	err = -ENOMEM;
	htab->buckets = kmalloc_array(htab->n_buckets, sizeof(struct bucket),
				      GFP_USER | __GFP_NOWARN);

	if (!htab->buckets) {
		htab->buckets = vmalloc(htab->n_buckets * sizeof(struct bucket));
		if (!htab->buckets)
			goto free_htab;
	}

	for (i = 0; i < htab->n_buckets; i++) {
		INIT_HLIST_HEAD(&htab->buckets[i].head);
		raw_spin_lock_init(&htab->buckets[i].lock);
	}

	atomic_set(&htab->count, 0);

	return &htab->map;

free_htab:
	kfree(htab);
	return ERR_PTR(err);
}

static inline u32 htab_map_hash(const void *key, u32 key_len)
{
	return jhash(key, key_len, 0);
}

static inline struct bucket *__select_bucket(struct bpf_htab *htab, u32 hash)
{
	return &htab->buckets[hash & (htab->n_buckets - 1)];
}

static inline struct hlist_head *select_bucket(struct bpf_htab *htab, u32 hash)
{
	return &__select_bucket(htab, hash)->head;
}

static struct htab_elem *lookup_elem_raw(struct hlist_head *head, u32 hash,
					 void *key, u32 key_size)
{
	struct htab_elem *l;

	hlist_for_each_entry_rcu(l, head, hash_node)
		if (l->hash == hash && !memcmp(&l->key, key, key_size))
			return l;

	return NULL;
}

/* Called from syscall or from eBPF program */
static void *__htab_map_lookup_elem(struct bpf_map *map, void *key)
{
	struct bpf_htab *htab = container_of(map, struct bpf_htab, map);
	struct hlist_head *head;
	struct htab_elem *l;
	u32 hash, key_size;

	/* Must be called with rcu_read_lock. */
	WARN_ON_ONCE(!rcu_read_lock_held());

	key_size = map->key_size;

	hash = htab_map_hash(key, key_size);

	head = select_bucket(htab, hash);

	l = lookup_elem_raw(head, hash, key, key_size);

	return l;
}

static void *htab_map_lookup_elem(struct bpf_map *map, void *key)
{
	struct htab_elem *l = __htab_map_lookup_elem(map, key);

	if (l)
		return l->key + round_up(map->key_size, 8);

	return NULL;
}

/* Called from syscall */
static int htab_map_get_next_key(struct bpf_map *map, void *key, void *next_key)
{
	struct bpf_htab *htab = container_of(map, struct bpf_htab, map);
	struct hlist_head *head;
	struct htab_elem *l, *next_l;
	u32 hash, key_size;
	int i;

	WARN_ON_ONCE(!rcu_read_lock_held());

	key_size = map->key_size;

	hash = htab_map_hash(key, key_size);

	head = select_bucket(htab, hash);

	/* lookup the key */
	l = lookup_elem_raw(head, hash, key, key_size);

	if (!l) {
		i = 0;
		goto find_first_elem;
	}

	/* key was found, get next key in the same bucket */
	next_l = hlist_entry_safe(rcu_dereference_raw(hlist_next_rcu(&l->hash_node)),
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
		next_l = hlist_entry_safe(rcu_dereference_raw(hlist_first_rcu(head)),
					  struct htab_elem, hash_node);
		if (next_l) {
			/* if it's not empty, just return it */
			memcpy(next_key, next_l->key, key_size);
			return 0;
		}
	}

	/* itereated over all buckets and all elements */
	return -ENOENT;
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

static void htab_percpu_elem_free(struct htab_elem *l)
{
	free_percpu(htab_elem_get_ptr(l, l->key_size));
	kfree(l);
}

static void htab_percpu_elem_free_rcu(struct rcu_head *head)
{
	struct htab_elem *l = container_of(head, struct htab_elem, rcu);

	htab_percpu_elem_free(l);
}

static void free_htab_elem(struct htab_elem *l, bool percpu, u32 key_size)
{
	if (percpu) {
		l->key_size = key_size;
		call_rcu(&l->rcu, htab_percpu_elem_free_rcu);
	} else {
		kfree_rcu(l, rcu);
	}
}

static struct htab_elem *alloc_htab_elem(struct bpf_htab *htab, void *key,
					 void *value, u32 key_size, u32 hash,
					 bool percpu, bool onallcpus)
{
	u32 size = htab->map.value_size;
	struct htab_elem *l_new;
	void __percpu *pptr;

	l_new = kmalloc(htab->elem_size, GFP_ATOMIC | __GFP_NOWARN);
	if (!l_new)
		return NULL;

	memcpy(l_new->key, key, key_size);
	if (percpu) {
		/* round up value_size to 8 bytes */
		size = round_up(size, 8);

		/* alloc_percpu zero-fills */
		pptr = __alloc_percpu_gfp(size, 8, GFP_ATOMIC | __GFP_NOWARN);
		if (!pptr) {
			kfree(l_new);
			return NULL;
		}

		if (!onallcpus) {
			/* copy true value_size bytes */
			memcpy(this_cpu_ptr(pptr), value, htab->map.value_size);
		} else {
			int off = 0, cpu;

			for_each_possible_cpu(cpu) {
				bpf_long_memcpy(per_cpu_ptr(pptr, cpu),
						value + off, size);
				off += size;
			}
		}
		htab_elem_set_ptr(l_new, key_size, pptr);
	} else {
		memcpy(l_new->key + round_up(key_size, 8), value, size);
	}

	l_new->hash = hash;
	return l_new;
}

static int check_flags(struct bpf_htab *htab, struct htab_elem *l_old,
		       u64 map_flags)
{
	if (!l_old && unlikely(atomic_read(&htab->count) >= htab->map.max_entries))
		/* if elem with this 'key' doesn't exist and we've reached
		 * max_entries limit, fail insertion of new elem
		 */
		return -E2BIG;

	if (l_old && map_flags == BPF_NOEXIST)
		/* elem already exists */
		return -EEXIST;

	if (!l_old && map_flags == BPF_EXIST)
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
	struct hlist_head *head;
	unsigned long flags;
	struct bucket *b;
	u32 key_size, hash;
	int ret;

	if (unlikely(map_flags > BPF_EXIST))
		/* unknown flags */
		return -EINVAL;

	WARN_ON_ONCE(!rcu_read_lock_held());

	key_size = map->key_size;

	hash = htab_map_hash(key, key_size);

	/* allocate new element outside of the lock, since
	 * we're most likley going to insert it
	 */
	l_new = alloc_htab_elem(htab, key, value, key_size, hash, false, false);
	if (!l_new)
		return -ENOMEM;

	b = __select_bucket(htab, hash);
	head = &b->head;

	/* bpf_map_update_elem() can be called in_irq() */
	raw_spin_lock_irqsave(&b->lock, flags);

	l_old = lookup_elem_raw(head, hash, key, key_size);

	ret = check_flags(htab, l_old, map_flags);
	if (ret)
		goto err;

	/* add new element to the head of the list, so that
	 * concurrent search will find it before old elem
	 */
	hlist_add_head_rcu(&l_new->hash_node, head);
	if (l_old) {
		hlist_del_rcu(&l_old->hash_node);
		kfree_rcu(l_old, rcu);
	} else {
		atomic_inc(&htab->count);
	}
	raw_spin_unlock_irqrestore(&b->lock, flags);
	return 0;
err:
	raw_spin_unlock_irqrestore(&b->lock, flags);
	kfree(l_new);
	return ret;
}

static int __htab_percpu_map_update_elem(struct bpf_map *map, void *key,
					 void *value, u64 map_flags,
					 bool onallcpus)
{
	struct bpf_htab *htab = container_of(map, struct bpf_htab, map);
	struct htab_elem *l_new = NULL, *l_old;
	struct hlist_head *head;
	unsigned long flags;
	struct bucket *b;
	u32 key_size, hash;
	int ret;

	if (unlikely(map_flags > BPF_EXIST))
		/* unknown flags */
		return -EINVAL;

	WARN_ON_ONCE(!rcu_read_lock_held());

	key_size = map->key_size;

	hash = htab_map_hash(key, key_size);

	b = __select_bucket(htab, hash);
	head = &b->head;

	/* bpf_map_update_elem() can be called in_irq() */
	raw_spin_lock_irqsave(&b->lock, flags);

	l_old = lookup_elem_raw(head, hash, key, key_size);

	ret = check_flags(htab, l_old, map_flags);
	if (ret)
		goto err;

	if (l_old) {
		void __percpu *pptr = htab_elem_get_ptr(l_old, key_size);
		u32 size = htab->map.value_size;

		/* per-cpu hash map can update value in-place */
		if (!onallcpus) {
			memcpy(this_cpu_ptr(pptr), value, size);
		} else {
			int off = 0, cpu;

			size = round_up(size, 8);
			for_each_possible_cpu(cpu) {
				bpf_long_memcpy(per_cpu_ptr(pptr, cpu),
						value + off, size);
				off += size;
			}
		}
	} else {
		l_new = alloc_htab_elem(htab, key, value, key_size,
					hash, true, onallcpus);
		if (!l_new) {
			ret = -ENOMEM;
			goto err;
		}
		hlist_add_head_rcu(&l_new->hash_node, head);
		atomic_inc(&htab->count);
	}
	ret = 0;
err:
	raw_spin_unlock_irqrestore(&b->lock, flags);
	return ret;
}

static int htab_percpu_map_update_elem(struct bpf_map *map, void *key,
				       void *value, u64 map_flags)
{
	return __htab_percpu_map_update_elem(map, key, value, map_flags, false);
}

/* Called from syscall or from eBPF program */
static int htab_map_delete_elem(struct bpf_map *map, void *key)
{
	struct bpf_htab *htab = container_of(map, struct bpf_htab, map);
	bool percpu = map->map_type == BPF_MAP_TYPE_PERCPU_HASH;
	struct hlist_head *head;
	struct bucket *b;
	struct htab_elem *l;
	unsigned long flags;
	u32 hash, key_size;
	int ret = -ENOENT;

	WARN_ON_ONCE(!rcu_read_lock_held());

	key_size = map->key_size;

	hash = htab_map_hash(key, key_size);
	b = __select_bucket(htab, hash);
	head = &b->head;

	raw_spin_lock_irqsave(&b->lock, flags);

	l = lookup_elem_raw(head, hash, key, key_size);

	if (l) {
		hlist_del_rcu(&l->hash_node);
		atomic_dec(&htab->count);
		free_htab_elem(l, percpu, key_size);
		ret = 0;
	}

	raw_spin_unlock_irqrestore(&b->lock, flags);
	return ret;
}

static void delete_all_elements(struct bpf_htab *htab)
{
	int i;

	for (i = 0; i < htab->n_buckets; i++) {
		struct hlist_head *head = select_bucket(htab, i);
		struct hlist_node *n;
		struct htab_elem *l;

		hlist_for_each_entry_safe(l, n, head, hash_node) {
			hlist_del_rcu(&l->hash_node);
			atomic_dec(&htab->count);
			if (htab->map.map_type == BPF_MAP_TYPE_PERCPU_HASH) {
				l->key_size = htab->map.key_size;
				htab_percpu_elem_free(l);
			} else {
				kfree(l);
			}
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

	/* some of kfree_rcu() callbacks for elements of this map may not have
	 * executed. It's ok. Proceed to free residual elements and map itself
	 */
	delete_all_elements(htab);
	kvfree(htab->buckets);
	kfree(htab);
}

static const struct bpf_map_ops htab_ops = {
	.map_alloc = htab_map_alloc,
	.map_free = htab_map_free,
	.map_get_next_key = htab_map_get_next_key,
	.map_lookup_elem = htab_map_lookup_elem,
	.map_update_elem = htab_map_update_elem,
	.map_delete_elem = htab_map_delete_elem,
};

static struct bpf_map_type_list htab_type __read_mostly = {
	.ops = &htab_ops,
	.type = BPF_MAP_TYPE_HASH,
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
	int ret;

	rcu_read_lock();
	ret = __htab_percpu_map_update_elem(map, key, value, map_flags, true);
	rcu_read_unlock();

	return ret;
}

static const struct bpf_map_ops htab_percpu_ops = {
	.map_alloc = htab_map_alloc,
	.map_free = htab_map_free,
	.map_get_next_key = htab_map_get_next_key,
	.map_lookup_elem = htab_percpu_map_lookup_elem,
	.map_update_elem = htab_percpu_map_update_elem,
	.map_delete_elem = htab_map_delete_elem,
};

static struct bpf_map_type_list htab_percpu_type __read_mostly = {
	.ops = &htab_percpu_ops,
	.type = BPF_MAP_TYPE_PERCPU_HASH,
};

static int __init register_htab_map(void)
{
	bpf_register_map_type(&htab_type);
	bpf_register_map_type(&htab_percpu_type);
	return 0;
}
late_initcall(register_htab_map);
