// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2026 Meta Platforms, Inc. and affiliates. */

#include <vmlinux.h>
#include <bpf/bpf_helpers.h>

char _license[] SEC("license") = "GPL";

/* rh is not at offset 0, so the callback's value recovery is exercised. */
struct elem {
	__u64 pad;
	struct bpf_rcu_head rh;
	__u64 val;
};

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, 2);
	__type(key, __u32);
	__type(value, struct elem);
} arr SEC(".maps");

__u32 cb_key;
__u64 cb_keys;
__u64 cb_val;
__u32 cb_max_entries;
int callbacks;
int arm_err;
int busy_err;
int chain;		/* set by userspace: number of times to re-arm from the callback */
int chain_err;

static int reclaim(struct bpf_map *map, void *key, void *value)
{
	struct elem *e = value;

	cb_key = *(__u32 *)key;
	__sync_fetch_and_or(&cb_keys, 1ULL << cb_key);
	cb_val = e->val;
	cb_max_entries = map->max_entries;
	e->val = 0;

	if (chain > 0) {
		chain--;
		chain_err = bpf_call_rcu(&e->rh, &arr, reclaim);
	}

	__sync_fetch_and_add(&callbacks, 1);
	return 0;
}

SEC("syscall")
int arm(void *ctx)
{
	__u32 key = 1;
	struct elem *e;

	e = bpf_map_lookup_elem(&arr, &key);
	if (!e)
		return 1;

	e->val = 0xdeadbeef;
	/* Keep a grace period from elapsing between the two arms. */
	bpf_rcu_read_lock();
	arm_err = bpf_call_rcu(&e->rh, &arr, reclaim);
	busy_err = bpf_call_rcu(&e->rh, &arr, reclaim);
	bpf_rcu_read_unlock();
	return 0;
}

SEC("syscall")
int arm_both(void *ctx)
{
	__u32 key0 = 0, key1 = 1;
	struct elem *e0, *e1;

	e0 = bpf_map_lookup_elem(&arr, &key0);
	e1 = bpf_map_lookup_elem(&arr, &key1);
	if (!e0 || !e1)
		return 1;

	e0->val = 0xdeadbeef;
	e1->val = 0xdeadbeef;
	arm_err = bpf_call_rcu(&e0->rh, &arr, reclaim);
	arm_err |= bpf_call_rcu(&e1->rh, &arr, reclaim);
	return 0;
}

SEC("syscall")
int arm_trace(void *ctx)
{
	__u32 key = 1;
	struct elem *e;

	e = bpf_map_lookup_elem(&arr, &key);
	if (!e)
		return 1;

	e->val = 0xdeadbeef;
	/* No lock needed: the enclosing rcu_read_lock_trace() already blocks the grace period. */
	arm_err = bpf_call_rcu_tasks_trace(&e->rh, &arr, reclaim);
	busy_err = bpf_call_rcu_tasks_trace(&e->rh, &arr, reclaim);
	return 0;
}

SEC("iter/bpf_map_elem")
int dump(struct bpf_iter__bpf_map_elem *ctx)
{
	return 0;
}
