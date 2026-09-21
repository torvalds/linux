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
#define ARRAY_KEY2 2
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

/* callback for inner hash map */
static int timer_cb(void *map, int *key, struct hmap_elem *val)
{
	return 0;
}

SEC("?fentry/bpf_fentry_test1")
int BPF_PROG(test1, int a)
{
	struct hmap_elem init = {};
	struct bpf_map *inner_map, *inner_map2;
	struct hmap_elem *val;
	int array_key = ARRAY_KEY;
	int array_key2 = ARRAY_KEY2;
	int hash_key = HASH_KEY;

	inner_map = bpf_map_lookup_elem(&outer_arr, &array_key);
	if (!inner_map)
		return 0;

	inner_map2 = bpf_map_lookup_elem(&outer_arr, &array_key2);
	if (!inner_map2)
		return 0;
	bpf_map_update_elem(inner_map, &hash_key, &init, 0);
	val = bpf_map_lookup_elem(inner_map, &hash_key);
	if (!val)
		return 0;

	bpf_timer_init(&val->timer, inner_map2, CLOCK_MONOTONIC);
	if (bpf_timer_set_callback(&val->timer, timer_cb))
		err |= 4;
	if (bpf_timer_start(&val->timer, 0, 0))
		err |= 8;
	return 0;
}

struct callback_ctx {
	void *map;
};

static int mismatch_iter_cb(void *map, int *key, struct hmap_elem *val, struct callback_ctx *ctx)
{
	bpf_timer_init(&val->timer, ctx->map, CLOCK_MONOTONIC);
	return 0;
}

static int timer_mismatch_cb(void *map, int *key, struct hmap_elem *val)
{
	struct callback_ctx ctx = { .map = map };
	struct bpf_map *inner_map2;
	int array_key2 = ARRAY_KEY2;

	inner_map2 = bpf_map_lookup_elem(&outer_arr, &array_key2);
	if (!inner_map2)
		return 0;
	bpf_for_each_map_elem(inner_map2, mismatch_iter_cb, &ctx, 0);
	return 0;
}

static int match_iter_cb(void *map, int *key, struct hmap_elem *val, struct callback_ctx *ctx)
{
	bpf_timer_init(&val->timer, map, CLOCK_MONOTONIC);
	return 0;
}

static int timer_match_cb(void *map, int *key, struct hmap_elem *val)
{
	struct callback_ctx ctx = {};
	struct bpf_map *inner_map2;
	int array_key2 = ARRAY_KEY2;

	inner_map2 = bpf_map_lookup_elem(&outer_arr, &array_key2);
	if (!inner_map2)
		return 0;
	bpf_for_each_map_elem(inner_map2, match_iter_cb, &ctx, 0);
	return 0;
}

SEC("?fentry/bpf_fentry_test1")
int BPF_PROG(callback_map_uid_mismatch, int a)
{
	struct hmap_elem *val;
	struct bpf_map *inner_map;
	int array_key = ARRAY_KEY;
	int hash_key = HASH_KEY;

	inner_map = bpf_map_lookup_elem(&outer_arr, &array_key);
	if (!inner_map)
		return 0;
	val = bpf_map_lookup_elem(inner_map, &hash_key);
	if (!val)
		return 0;

	bpf_timer_init(&val->timer, inner_map, CLOCK_MONOTONIC);
	bpf_timer_set_callback(&val->timer, timer_mismatch_cb);
	return 0;
}

SEC("?fentry/bpf_fentry_test1")
int BPF_PROG(callback_map_uid_match, int a)
{
	struct hmap_elem *val;
	struct bpf_map *inner_map;
	int array_key = ARRAY_KEY;
	int hash_key = HASH_KEY;

	inner_map = bpf_map_lookup_elem(&outer_arr, &array_key);
	if (!inner_map)
		return 0;
	val = bpf_map_lookup_elem(inner_map, &hash_key);
	if (!val)
		return 0;

	bpf_timer_init(&val->timer, inner_map, CLOCK_MONOTONIC);
	bpf_timer_set_callback(&val->timer, timer_match_cb);
	return 0;
}
