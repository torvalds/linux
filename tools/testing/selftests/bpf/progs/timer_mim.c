// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2021 Facebook */
#include <linux/bpf.h>
#include <time.h>
#include <errno.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

char _license[] SEC("license") = "GPL";
struct hmap_elem {
	int pad; /* unused */
	struct bpf_timer timer;
};

struct inner_map {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, 1024);
	__type(key, int);
	__type(value, struct hmap_elem);
} inner_htab SEC(".maps");

#define ARRAY_KEY 1
#define HASH_KEY 1234

struct outer_arr {
	__uint(type, BPF_MAP_TYPE_ARRAY_OF_MAPS);
	__uint(max_entries, 2);
	__uint(key_size, sizeof(int));
	__uint(value_size, sizeof(int));
	__array(values, struct inner_map);
} outer_arr SEC(".maps") = {
	.values = { [ARRAY_KEY] = &inner_htab },
};

__u64 err;
__u64 ok;
__u64 cnt;

static int timer_cb1(void *map, int *key, struct hmap_elem *val);

static int timer_cb2(void *map, int *key, struct hmap_elem *val)
{
	cnt++;
	bpf_timer_set_callback(&val->timer, timer_cb1);
	if (bpf_timer_start(&val->timer, 1000, 0))
		err |= 1;
	ok |= 1;
	return 0;
}

/* callback for inner hash map */
static int timer_cb1(void *map, int *key, struct hmap_elem *val)
{
	cnt++;
	bpf_timer_set_callback(&val->timer, timer_cb2);
	if (bpf_timer_start(&val->timer, 1000, 0))
		err |= 2;
	/* Do a lookup to make sure 'map' and 'key' pointers are correct */
	bpf_map_lookup_elem(map, key);
	ok |= 2;
	return 0;
}

SEC("fentry/bpf_fentry_test1")
int BPF_PROG(test1, int a)
{
	struct hmap_elem init = {};
	struct bpf_map *inner_map;
	struct hmap_elem *val;
	int array_key = ARRAY_KEY;
	int hash_key = HASH_KEY;

	inner_map = bpf_map_lookup_elem(&outer_arr, &array_key);
	if (!inner_map)
		return 0;

	bpf_map_update_elem(inner_map, &hash_key, &init, 0);
	val = bpf_map_lookup_elem(inner_map, &hash_key);
	if (!val)
		return 0;

	bpf_timer_init(&val->timer, inner_map, CLOCK_MONOTONIC);
	if (bpf_timer_set_callback(&val->timer, timer_cb1))
		err |= 4;
	if (bpf_timer_start(&val->timer, 0, 0))
		err |= 8;
	return 0;
}
