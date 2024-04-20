// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2024 Benjamin Tissoires
 */

#include "bpf_experimental.h"
#include <bpf/bpf_helpers.h>
#include "bpf_misc.h"
#include "../bpf_testmod/bpf_testmod_kfunc.h"

char _license[] SEC("license") = "GPL";

struct elem {
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

SEC("tc")
/* test that bpf_wq_init takes a map as a second argument
 */
__log_level(2)
__flag(BPF_F_TEST_STATE_FREQ)
__failure
__msg(": (85) call bpf_wq_init#") /* anchor message */
__msg("pointer in R2 isn't map pointer")
long test_wq_init_nomap(void *ctx)
{
	struct bpf_wq *wq;
	struct elem *val;
	int key = 0;

	val = bpf_map_lookup_elem(&array, &key);
	if (!val)
		return -1;

	wq = &val->w;
	if (bpf_wq_init(wq, &key, 0) != 0)
		return -3;

	return 0;
}

SEC("tc")
/* test that the workqueue is part of the map in bpf_wq_init
 */
__log_level(2)
__flag(BPF_F_TEST_STATE_FREQ)
__failure
__msg(": (85) call bpf_wq_init#") /* anchor message */
__msg("workqueue pointer in R1 map_uid=0 doesn't match map pointer in R2 map_uid=0")
long test_wq_init_wrong_map(void *ctx)
{
	struct bpf_wq *wq;
	struct elem *val;
	int key = 0;

	val = bpf_map_lookup_elem(&array, &key);
	if (!val)
		return -1;

	wq = &val->w;
	if (bpf_wq_init(wq, &lru, 0) != 0)
		return -3;

	return 0;
}
