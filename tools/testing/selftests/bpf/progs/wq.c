// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2024 Benjamin Tissoires
 */

#include "bpf_experimental.h"
#include <bpf/bpf_helpers.h>
#include "bpf_misc.h"
#include "../bpf_testmod/bpf_testmod_kfunc.h"

char _license[] SEC("license") = "GPL";

struct hmap_elem {
	int counter;
	struct bpf_timer timer; /* unused */
	struct bpf_spin_lock lock; /* unused */
	struct bpf_wq work;
};

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, 1000);
	__type(key, int);
	__type(value, struct hmap_elem);
} hmap SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(map_flags, BPF_F_NO_PREALLOC);
	__uint(max_entries, 1000);
	__type(key, int);
	__type(value, struct hmap_elem);
} hmap_malloc SEC(".maps");

struct elem {
	int ok_offset;
	struct bpf_wq w;
};

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, 2);
	__type(key, int);
	__type(value, struct elem);
} array SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_LRU_HASH);
	__uint(max_entries, 4);
	__type(key, int);
	__type(value, struct elem);
} lru SEC(".maps");

__u32 ok;
__u32 ok_sleepable;

static int test_elem_callback(void *map, int *key,
		int (callback_fn)(void *map, int *key, void *value))
{
	struct elem init = {}, *val;
	struct bpf_wq *wq;

	if ((ok & (1 << *key) ||
	    (ok_sleepable & (1 << *key))))
		return -22;

	if (map == &lru &&
	    bpf_map_update_elem(map, key, &init, 0))
		return -1;

	val = bpf_map_lookup_elem(map, key);
	if (!val)
		return -2;

	val->ok_offset = *key;

	wq = &val->w;
	if (bpf_wq_init(wq, map, 0) != 0)
		return -3;

	if (bpf_wq_set_callback(wq, callback_fn, 0))
		return -4;

	if (bpf_wq_start(wq, 0))
		return -5;

	return 0;
}

static int test_hmap_elem_callback(void *map, int *key,
		int (callback_fn)(void *map, int *key, void *value))
{
	struct hmap_elem init = {}, *val;
	struct bpf_wq *wq;

	if ((ok & (1 << *key) ||
	    (ok_sleepable & (1 << *key))))
		return -22;

	if (bpf_map_update_elem(map, key, &init, 0))
		return -1;

	val = bpf_map_lookup_elem(map, key);
	if (!val)
		return -2;

	wq = &val->work;
	if (bpf_wq_init(wq, map, 0) != 0)
		return -3;

	if (bpf_wq_set_callback(wq, callback_fn, 0))
		return -4;

	if (bpf_wq_start(wq, 0))
		return -5;

	return 0;
}

/* callback for non sleepable workqueue */
static int wq_callback(void *map, int *key, void *value)
{
	bpf_kfunc_common_test();
	ok |= (1 << *key);
	return 0;
}

/* callback for sleepable workqueue */
static int wq_cb_sleepable(void *map, int *key, void *value)
{
	struct elem *data = (struct elem *)value;
	int offset = data->ok_offset;

	if (*key != offset)
		return 0;

	bpf_kfunc_call_test_sleepable();
	ok_sleepable |= (1 << offset);
	return 0;
}

SEC("tc")
/* test that workqueues can be used from an array */
__retval(0)
long test_call_array_sleepable(void *ctx)
{
	int key = 0;

	return test_elem_callback(&array, &key, wq_cb_sleepable);
}

SEC("syscall")
/* Same test than above but from a sleepable context. */
__retval(0)
long test_syscall_array_sleepable(void *ctx)
{
	int key = 1;

	return test_elem_callback(&array, &key, wq_cb_sleepable);
}

SEC("tc")
/* test that workqueues can be used from a hashmap */
__retval(0)
long test_call_hash_sleepable(void *ctx)
{
	int key = 2;

	return test_hmap_elem_callback(&hmap, &key, wq_callback);
}

SEC("tc")
/* test that workqueues can be used from a hashmap with NO_PREALLOC. */
__retval(0)
long test_call_hash_malloc_sleepable(void *ctx)
{
	int key = 3;

	return test_hmap_elem_callback(&hmap_malloc, &key, wq_callback);
}

SEC("tc")
/* test that workqueues can be used from a LRU map */
__retval(0)
long test_call_lru_sleepable(void *ctx)
{
	int key = 4;

	return test_elem_callback(&lru, &key, wq_callback);
}
