// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2020 Facebook

#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>

#define LOOP_BOUND 0xf
#define MAX_ENTRIES 8
#define HALF_ENTRIES (MAX_ENTRIES >> 1)

_Static_assert(MAX_ENTRIES < LOOP_BOUND, "MAX_ENTRIES must be < LOOP_BOUND");

enum bpf_map_type g_map_type = BPF_MAP_TYPE_UNSPEC;
__u32 g_line = 0;
int page_size = 0; /* userspace should set it */

#define VERIFY_TYPE(type, func) ({	\
	g_map_type = type;		\
	if (!func())			\
		return 0;		\
})


#define VERIFY(expr) ({		\
	g_line = __LINE__;	\
	if (!(expr))		\
		return 0;	\
})

struct bpf_map {
	enum bpf_map_type map_type;
	__u32 key_size;
	__u32 value_size;
	__u32 max_entries;
	__u32 id;
} __attribute__((preserve_access_index));

static inline int check_bpf_map_fields(struct bpf_map *map, __u32 key_size,
				       __u32 value_size, __u32 max_entries)
{
	VERIFY(map->map_type == g_map_type);
	VERIFY(map->key_size == key_size);
	VERIFY(map->value_size == value_size);
	VERIFY(map->max_entries == max_entries);
	VERIFY(map->id > 0);

	return 1;
}

static inline int check_bpf_map_ptr(struct bpf_map *indirect,
				    struct bpf_map *direct)
{
	VERIFY(indirect->map_type == direct->map_type);
	VERIFY(indirect->key_size == direct->key_size);
	VERIFY(indirect->value_size == direct->value_size);
	VERIFY(indirect->max_entries == direct->max_entries);
	VERIFY(indirect->id == direct->id);

	return 1;
}

static inline int check(struct bpf_map *indirect, struct bpf_map *direct,
			__u32 key_size, __u32 value_size, __u32 max_entries)
{
	VERIFY(check_bpf_map_ptr(indirect, direct));
	VERIFY(check_bpf_map_fields(indirect, key_size, value_size,
				    max_entries));
	return 1;
}

static inline int check_default(struct bpf_map *indirect,
				struct bpf_map *direct)
{
	VERIFY(check(indirect, direct, sizeof(__u32), sizeof(__u32),
		     MAX_ENTRIES));
	return 1;
}

static __noinline int
check_default_noinline(struct bpf_map *indirect, struct bpf_map *direct)
{
	VERIFY(check(indirect, direct, sizeof(__u32), sizeof(__u32),
		     MAX_ENTRIES));
	return 1;
}

typedef struct {
	int counter;
} atomic_t;

struct bpf_htab {
	struct bpf_map map;
	atomic_t count;
	__u32 n_buckets;
	__u32 elem_size;
} __attribute__((preserve_access_index));

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(map_flags, BPF_F_NO_PREALLOC); /* to test bpf_htab.count */
	__uint(max_entries, MAX_ENTRIES);
	__type(key, __u32);
	__type(value, __u32);
} m_hash SEC(".maps");

static inline int check_hash(void)
{
	struct bpf_htab *hash = (struct bpf_htab *)&m_hash;
	struct bpf_map *map = (struct bpf_map *)&m_hash;
	int i;

	VERIFY(check_default_noinline(&hash->map, map));

	VERIFY(hash->n_buckets == MAX_ENTRIES);
	VERIFY(hash->elem_size == 64);

	VERIFY(hash->count.counter == 0);
	for (i = 0; i < HALF_ENTRIES; ++i) {
		const __u32 key = i;
		const __u32 val = 1;

		if (bpf_map_update_elem(hash, &key, &val, 0))
			return 0;
	}
	VERIFY(hash->count.counter == HALF_ENTRIES);

	return 1;
}

struct bpf_array {
	struct bpf_map map;
	__u32 elem_size;
} __attribute__((preserve_access_index));

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, MAX_ENTRIES);
	__type(key, __u32);
	__type(value, __u32);
} m_array SEC(".maps");

static inline int check_array(void)
{
	struct bpf_array *array = (struct bpf_array *)&m_array;
	struct bpf_map *map = (struct bpf_map *)&m_array;
	int i, n_lookups = 0, n_keys = 0;

	VERIFY(check_default(&array->map, map));

	VERIFY(array->elem_size == 8);

	for (i = 0; i < array->map.max_entries && i < LOOP_BOUND; ++i) {
		const __u32 key = i;
		__u32 *val = bpf_map_lookup_elem(array, &key);

		++n_lookups;
		if (val)
			++n_keys;
	}

	VERIFY(n_lookups == MAX_ENTRIES);
	VERIFY(n_keys == MAX_ENTRIES);

	return 1;
}

struct {
	__uint(type, BPF_MAP_TYPE_PROG_ARRAY);
	__uint(max_entries, MAX_ENTRIES);
	__type(key, __u32);
	__type(value, __u32);
} m_prog_array SEC(".maps");

static inline int check_prog_array(void)
{
	struct bpf_array *prog_array = (struct bpf_array *)&m_prog_array;
	struct bpf_map *map = (struct bpf_map *)&m_prog_array;

	VERIFY(check_default(&prog_array->map, map));

	return 1;
}

struct {
	__uint(type, BPF_MAP_TYPE_PERF_EVENT_ARRAY);
	__uint(max_entries, MAX_ENTRIES);
	__type(key, __u32);
	__type(value, __u32);
} m_perf_event_array SEC(".maps");

static inline int check_perf_event_array(void)
{
	struct bpf_array *perf_event_array = (struct bpf_array *)&m_perf_event_array;
	struct bpf_map *map = (struct bpf_map *)&m_perf_event_array;

	VERIFY(check_default(&perf_event_array->map, map));

	return 1;
}

struct {
	__uint(type, BPF_MAP_TYPE_PERCPU_HASH);
	__uint(max_entries, MAX_ENTRIES);
	__type(key, __u32);
	__type(value, __u32);
} m_percpu_hash SEC(".maps");

static inline int check_percpu_hash(void)
{
	struct bpf_htab *percpu_hash = (struct bpf_htab *)&m_percpu_hash;
	struct bpf_map *map = (struct bpf_map *)&m_percpu_hash;

	VERIFY(check_default(&percpu_hash->map, map));

	return 1;
}

struct {
	__uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
	__uint(max_entries, MAX_ENTRIES);
	__type(key, __u32);
	__type(value, __u32);
} m_percpu_array SEC(".maps");

static inline int check_percpu_array(void)
{
	struct bpf_array *percpu_array = (struct bpf_array *)&m_percpu_array;
	struct bpf_map *map = (struct bpf_map *)&m_percpu_array;

	VERIFY(check_default(&percpu_array->map, map));

	return 1;
}

struct bpf_stack_map {
	struct bpf_map map;
} __attribute__((preserve_access_index));

struct {
	__uint(type, BPF_MAP_TYPE_STACK_TRACE);
	__uint(max_entries, MAX_ENTRIES);
	__type(key, __u32);
	__type(value, __u64);
} m_stack_trace SEC(".maps");

static inline int check_stack_trace(void)
{
	struct bpf_stack_map *stack_trace =
		(struct bpf_stack_map *)&m_stack_trace;
	struct bpf_map *map = (struct bpf_map *)&m_stack_trace;

	VERIFY(check(&stack_trace->map, map, sizeof(__u32), sizeof(__u64),
		     MAX_ENTRIES));

	return 1;
}

struct {
	__uint(type, BPF_MAP_TYPE_CGROUP_ARRAY);
	__uint(max_entries, MAX_ENTRIES);
	__type(key, __u32);
	__type(value, __u32);
} m_cgroup_array SEC(".maps");

static inline int check_cgroup_array(void)
{
	struct bpf_array *cgroup_array = (struct bpf_array *)&m_cgroup_array;
	struct bpf_map *map = (struct bpf_map *)&m_cgroup_array;

	VERIFY(check_default(&cgroup_array->map, map));

	return 1;
}

struct {
	__uint(type, BPF_MAP_TYPE_LRU_HASH);
	__uint(max_entries, MAX_ENTRIES);
	__type(key, __u32);
	__type(value, __u32);
} m_lru_hash SEC(".maps");

static inline int check_lru_hash(void)
{
	struct bpf_htab *lru_hash = (struct bpf_htab *)&m_lru_hash;
	struct bpf_map *map = (struct bpf_map *)&m_lru_hash;

	VERIFY(check_default(&lru_hash->map, map));

	return 1;
}

struct {
	__uint(type, BPF_MAP_TYPE_LRU_PERCPU_HASH);
	__uint(max_entries, MAX_ENTRIES);
	__type(key, __u32);
	__type(value, __u32);
} m_lru_percpu_hash SEC(".maps");

static inline int check_lru_percpu_hash(void)
{
	struct bpf_htab *lru_percpu_hash = (struct bpf_htab *)&m_lru_percpu_hash;
	struct bpf_map *map = (struct bpf_map *)&m_lru_percpu_hash;

	VERIFY(check_default(&lru_percpu_hash->map, map));

	return 1;
}

struct lpm_trie {
	struct bpf_map map;
} __attribute__((preserve_access_index));

struct lpm_key {
	struct bpf_lpm_trie_key trie_key;
	__u32 data;
};

struct {
	__uint(type, BPF_MAP_TYPE_LPM_TRIE);
	__uint(map_flags, BPF_F_NO_PREALLOC);
	__uint(max_entries, MAX_ENTRIES);
	__type(key, struct lpm_key);
	__type(value, __u32);
} m_lpm_trie SEC(".maps");

static inline int check_lpm_trie(void)
{
	struct lpm_trie *lpm_trie = (struct lpm_trie *)&m_lpm_trie;
	struct bpf_map *map = (struct bpf_map *)&m_lpm_trie;

	VERIFY(check(&lpm_trie->map, map, sizeof(struct lpm_key), sizeof(__u32),
		     MAX_ENTRIES));

	return 1;
}

#define INNER_MAX_ENTRIES 1234

struct inner_map {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, INNER_MAX_ENTRIES);
	__type(key, __u32);
	__type(value, __u32);
} inner_map SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY_OF_MAPS);
	__uint(max_entries, MAX_ENTRIES);
	__type(key, __u32);
	__type(value, __u32);
	__array(values, struct {
		__uint(type, BPF_MAP_TYPE_ARRAY);
		__uint(max_entries, INNER_MAX_ENTRIES);
		__type(key, __u32);
		__type(value, __u32);
	});
} m_array_of_maps SEC(".maps") = {
	.values = { (void *)&inner_map, 0, 0, 0, 0, 0, 0, 0, 0 },
};

static inline int check_array_of_maps(void)
{
	struct bpf_array *array_of_maps = (struct bpf_array *)&m_array_of_maps;
	struct bpf_map *map = (struct bpf_map *)&m_array_of_maps;
	struct bpf_array *inner_map;
	int key = 0;

	VERIFY(check_default(&array_of_maps->map, map));
	inner_map = bpf_map_lookup_elem(array_of_maps, &key);
	VERIFY(inner_map != NULL);
	VERIFY(inner_map->map.max_entries == INNER_MAX_ENTRIES);

	return 1;
}

struct {
	__uint(type, BPF_MAP_TYPE_HASH_OF_MAPS);
	__uint(max_entries, MAX_ENTRIES);
	__type(key, __u32);
	__type(value, __u32);
	__array(values, struct inner_map);
} m_hash_of_maps SEC(".maps") = {
	.values = {
		[2] = &inner_map,
	},
};

static inline int check_hash_of_maps(void)
{
	struct bpf_htab *hash_of_maps = (struct bpf_htab *)&m_hash_of_maps;
	struct bpf_map *map = (struct bpf_map *)&m_hash_of_maps;
	struct bpf_htab *inner_map;
	int key = 2;

	VERIFY(check_default(&hash_of_maps->map, map));
	inner_map = bpf_map_lookup_elem(hash_of_maps, &key);
	VERIFY(inner_map != NULL);
	VERIFY(inner_map->map.max_entries == INNER_MAX_ENTRIES);

	return 1;
}

struct bpf_dtab {
	struct bpf_map map;
} __attribute__((preserve_access_index));

struct {
	__uint(type, BPF_MAP_TYPE_DEVMAP);
	__uint(max_entries, MAX_ENTRIES);
	__type(key, __u32);
	__type(value, __u32);
} m_devmap SEC(".maps");

static inline int check_devmap(void)
{
	struct bpf_dtab *devmap = (struct bpf_dtab *)&m_devmap;
	struct bpf_map *map = (struct bpf_map *)&m_devmap;

	VERIFY(check_default(&devmap->map, map));

	return 1;
}

struct bpf_stab {
	struct bpf_map map;
} __attribute__((preserve_access_index));

struct {
	__uint(type, BPF_MAP_TYPE_SOCKMAP);
	__uint(max_entries, MAX_ENTRIES);
	__type(key, __u32);
	__type(value, __u32);
} m_sockmap SEC(".maps");

static inline int check_sockmap(void)
{
	struct bpf_stab *sockmap = (struct bpf_stab *)&m_sockmap;
	struct bpf_map *map = (struct bpf_map *)&m_sockmap;

	VERIFY(check_default(&sockmap->map, map));

	return 1;
}

struct bpf_cpu_map {
	struct bpf_map map;
} __attribute__((preserve_access_index));

struct {
	__uint(type, BPF_MAP_TYPE_CPUMAP);
	__uint(max_entries, MAX_ENTRIES);
	__type(key, __u32);
	__type(value, __u32);
} m_cpumap SEC(".maps");

static inline int check_cpumap(void)
{
	struct bpf_cpu_map *cpumap = (struct bpf_cpu_map *)&m_cpumap;
	struct bpf_map *map = (struct bpf_map *)&m_cpumap;

	VERIFY(check_default(&cpumap->map, map));

	return 1;
}

struct xsk_map {
	struct bpf_map map;
} __attribute__((preserve_access_index));

struct {
	__uint(type, BPF_MAP_TYPE_XSKMAP);
	__uint(max_entries, MAX_ENTRIES);
	__type(key, __u32);
	__type(value, __u32);
} m_xskmap SEC(".maps");

static inline int check_xskmap(void)
{
	struct xsk_map *xskmap = (struct xsk_map *)&m_xskmap;
	struct bpf_map *map = (struct bpf_map *)&m_xskmap;

	VERIFY(check_default(&xskmap->map, map));

	return 1;
}

struct bpf_shtab {
	struct bpf_map map;
} __attribute__((preserve_access_index));

struct {
	__uint(type, BPF_MAP_TYPE_SOCKHASH);
	__uint(max_entries, MAX_ENTRIES);
	__type(key, __u32);
	__type(value, __u32);
} m_sockhash SEC(".maps");

static inline int check_sockhash(void)
{
	struct bpf_shtab *sockhash = (struct bpf_shtab *)&m_sockhash;
	struct bpf_map *map = (struct bpf_map *)&m_sockhash;

	VERIFY(check_default(&sockhash->map, map));

	return 1;
}

struct bpf_cgroup_storage_map {
	struct bpf_map map;
} __attribute__((preserve_access_index));

struct {
	__uint(type, BPF_MAP_TYPE_CGROUP_STORAGE);
	__type(key, struct bpf_cgroup_storage_key);
	__type(value, __u32);
} m_cgroup_storage SEC(".maps");

static inline int check_cgroup_storage(void)
{
	struct bpf_cgroup_storage_map *cgroup_storage =
		(struct bpf_cgroup_storage_map *)&m_cgroup_storage;
	struct bpf_map *map = (struct bpf_map *)&m_cgroup_storage;

	VERIFY(check(&cgroup_storage->map, map,
		     sizeof(struct bpf_cgroup_storage_key), sizeof(__u32), 0));

	return 1;
}

struct reuseport_array {
	struct bpf_map map;
} __attribute__((preserve_access_index));

struct {
	__uint(type, BPF_MAP_TYPE_REUSEPORT_SOCKARRAY);
	__uint(max_entries, MAX_ENTRIES);
	__type(key, __u32);
	__type(value, __u32);
} m_reuseport_sockarray SEC(".maps");

static inline int check_reuseport_sockarray(void)
{
	struct reuseport_array *reuseport_sockarray =
		(struct reuseport_array *)&m_reuseport_sockarray;
	struct bpf_map *map = (struct bpf_map *)&m_reuseport_sockarray;

	VERIFY(check_default(&reuseport_sockarray->map, map));

	return 1;
}

struct {
	__uint(type, BPF_MAP_TYPE_PERCPU_CGROUP_STORAGE);
	__type(key, struct bpf_cgroup_storage_key);
	__type(value, __u32);
} m_percpu_cgroup_storage SEC(".maps");

static inline int check_percpu_cgroup_storage(void)
{
	struct bpf_cgroup_storage_map *percpu_cgroup_storage =
		(struct bpf_cgroup_storage_map *)&m_percpu_cgroup_storage;
	struct bpf_map *map = (struct bpf_map *)&m_percpu_cgroup_storage;

	VERIFY(check(&percpu_cgroup_storage->map, map,
		     sizeof(struct bpf_cgroup_storage_key), sizeof(__u32), 0));

	return 1;
}

struct bpf_queue_stack {
	struct bpf_map map;
} __attribute__((preserve_access_index));

struct {
	__uint(type, BPF_MAP_TYPE_QUEUE);
	__uint(max_entries, MAX_ENTRIES);
	__type(value, __u32);
} m_queue SEC(".maps");

static inline int check_queue(void)
{
	struct bpf_queue_stack *queue = (struct bpf_queue_stack *)&m_queue;
	struct bpf_map *map = (struct bpf_map *)&m_queue;

	VERIFY(check(&queue->map, map, 0, sizeof(__u32), MAX_ENTRIES));

	return 1;
}

struct {
	__uint(type, BPF_MAP_TYPE_STACK);
	__uint(max_entries, MAX_ENTRIES);
	__type(value, __u32);
} m_stack SEC(".maps");

static inline int check_stack(void)
{
	struct bpf_queue_stack *stack = (struct bpf_queue_stack *)&m_stack;
	struct bpf_map *map = (struct bpf_map *)&m_stack;

	VERIFY(check(&stack->map, map, 0, sizeof(__u32), MAX_ENTRIES));

	return 1;
}

struct bpf_local_storage_map {
	struct bpf_map map;
} __attribute__((preserve_access_index));

struct {
	__uint(type, BPF_MAP_TYPE_SK_STORAGE);
	__uint(map_flags, BPF_F_NO_PREALLOC);
	__type(key, __u32);
	__type(value, __u32);
} m_sk_storage SEC(".maps");

static inline int check_sk_storage(void)
{
	struct bpf_local_storage_map *sk_storage =
		(struct bpf_local_storage_map *)&m_sk_storage;
	struct bpf_map *map = (struct bpf_map *)&m_sk_storage;

	VERIFY(check(&sk_storage->map, map, sizeof(__u32), sizeof(__u32), 0));

	return 1;
}

struct {
	__uint(type, BPF_MAP_TYPE_DEVMAP_HASH);
	__uint(max_entries, MAX_ENTRIES);
	__type(key, __u32);
	__type(value, __u32);
} m_devmap_hash SEC(".maps");

static inline int check_devmap_hash(void)
{
	struct bpf_dtab *devmap_hash = (struct bpf_dtab *)&m_devmap_hash;
	struct bpf_map *map = (struct bpf_map *)&m_devmap_hash;

	VERIFY(check_default(&devmap_hash->map, map));

	return 1;
}

struct bpf_ringbuf_map {
	struct bpf_map map;
} __attribute__((preserve_access_index));

struct {
	__uint(type, BPF_MAP_TYPE_RINGBUF);
} m_ringbuf SEC(".maps");

static inline int check_ringbuf(void)
{
	struct bpf_ringbuf_map *ringbuf = (struct bpf_ringbuf_map *)&m_ringbuf;
	struct bpf_map *map = (struct bpf_map *)&m_ringbuf;

	VERIFY(check(&ringbuf->map, map, 0, 0, page_size));

	return 1;
}

SEC("cgroup_skb/egress")
int cg_skb(void *ctx)
{
	VERIFY_TYPE(BPF_MAP_TYPE_HASH, check_hash);
	VERIFY_TYPE(BPF_MAP_TYPE_ARRAY, check_array);
	VERIFY_TYPE(BPF_MAP_TYPE_PROG_ARRAY, check_prog_array);
	VERIFY_TYPE(BPF_MAP_TYPE_PERF_EVENT_ARRAY, check_perf_event_array);
	VERIFY_TYPE(BPF_MAP_TYPE_PERCPU_HASH, check_percpu_hash);
	VERIFY_TYPE(BPF_MAP_TYPE_PERCPU_ARRAY, check_percpu_array);
	VERIFY_TYPE(BPF_MAP_TYPE_STACK_TRACE, check_stack_trace);
	VERIFY_TYPE(BPF_MAP_TYPE_CGROUP_ARRAY, check_cgroup_array);
	VERIFY_TYPE(BPF_MAP_TYPE_LRU_HASH, check_lru_hash);
	VERIFY_TYPE(BPF_MAP_TYPE_LRU_PERCPU_HASH, check_lru_percpu_hash);
	VERIFY_TYPE(BPF_MAP_TYPE_LPM_TRIE, check_lpm_trie);
	VERIFY_TYPE(BPF_MAP_TYPE_ARRAY_OF_MAPS, check_array_of_maps);
	VERIFY_TYPE(BPF_MAP_TYPE_HASH_OF_MAPS, check_hash_of_maps);
	VERIFY_TYPE(BPF_MAP_TYPE_DEVMAP, check_devmap);
	VERIFY_TYPE(BPF_MAP_TYPE_SOCKMAP, check_sockmap);
	VERIFY_TYPE(BPF_MAP_TYPE_CPUMAP, check_cpumap);
	VERIFY_TYPE(BPF_MAP_TYPE_XSKMAP, check_xskmap);
	VERIFY_TYPE(BPF_MAP_TYPE_SOCKHASH, check_sockhash);
	VERIFY_TYPE(BPF_MAP_TYPE_CGROUP_STORAGE, check_cgroup_storage);
	VERIFY_TYPE(BPF_MAP_TYPE_REUSEPORT_SOCKARRAY,
		    check_reuseport_sockarray);
	VERIFY_TYPE(BPF_MAP_TYPE_PERCPU_CGROUP_STORAGE,
		    check_percpu_cgroup_storage);
	VERIFY_TYPE(BPF_MAP_TYPE_QUEUE, check_queue);
	VERIFY_TYPE(BPF_MAP_TYPE_STACK, check_stack);
	VERIFY_TYPE(BPF_MAP_TYPE_SK_STORAGE, check_sk_storage);
	VERIFY_TYPE(BPF_MAP_TYPE_DEVMAP_HASH, check_devmap_hash);
	VERIFY_TYPE(BPF_MAP_TYPE_RINGBUF, check_ringbuf);

	return 1;
}

char _license[] SEC("license") = "GPL";
