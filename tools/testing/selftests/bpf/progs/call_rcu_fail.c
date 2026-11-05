// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2026 Meta Platforms, Inc. and affiliates. */

#include <vmlinux.h>
#include <bpf/bpf_helpers.h>
#include "bpf_misc.h"

char _license[] SEC("license") = "GPL";

const void *user_ptr = NULL;

struct elem {
	__u64 pad;
	struct bpf_rcu_head rh;
	__u64 val;
};

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, 1);
	__type(key, __u32);
	__type(value, struct elem);
} arr SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, 1);
	__type(key, __u32);
	__type(value, struct elem);
} arr2 SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, 1);
	__type(key, __u32);
	__type(value, __u64);
} plain SEC(".maps");

__u32 key = 0;

static int reclaim(struct bpf_map *map, void *key, void *value)
{
	return 0;
}

static int sleepable_reclaim(struct bpf_map *map, void *key, void *value)
{
	struct elem *e = value;

	bpf_copy_from_user(&e->val, sizeof(e->val), user_ptr);
	return 0;
}

SEC("syscall")
__failure __msg("bpf_rcu_head pointer in R1") __msg("doesn't match map pointer in R2")
int mismatch_map(void *ctx)
{
	struct elem *e;

	e = bpf_map_lookup_elem(&arr, &key);
	if (!e)
		return 0;
	bpf_call_rcu(&e->rh, &arr2, reclaim);
	return 0;
}

SEC("syscall")
__failure __msg("map 'plain' has no valid bpf_rcu_head")
int no_rcu_head(void *ctx)
{
	__u64 *val;

	val = bpf_map_lookup_elem(&plain, &key);
	if (!val)
		return 0;
	bpf_call_rcu((struct bpf_rcu_head *)val, &plain, reclaim);
	return 0;
}

SEC("syscall")
__failure __msg("doesn't point to 'struct bpf_rcu_head' that is at 8")
int wrong_offset(void *ctx)
{
	struct elem *e;

	e = bpf_map_lookup_elem(&arr, &key);
	if (!e)
		return 0;
	bpf_call_rcu((struct bpf_rcu_head *)&e->pad, &arr, reclaim);
	return 0;
}

SEC("syscall")
__failure __msg("R1 type=fp expected=map_value")
int rcu_head_on_stack(void *ctx)
{
	struct bpf_rcu_head rh;

	bpf_call_rcu(&rh, &arr, reclaim);
	return 0;
}

SEC("syscall")
__failure __msg("sleepable helper bpf_copy_from_user") __msg("in non-sleepable prog")
int sleepable_callback(void *ctx)
{
	struct elem *e;

	e = bpf_map_lookup_elem(&arr, &key);
	if (!e)
		return 0;
	bpf_call_rcu(&e->rh, &arr, sleepable_reclaim);
	return 0;
}
