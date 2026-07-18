// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2026 Meta Platforms, Inc. and affiliates. */
#include <vmlinux.h>
#include <bpf/bpf_helpers.h>

char _license[] SEC("license") = "GPL";

struct {
	__uint(type, BPF_MAP_TYPE_RHASH);
	__uint(map_flags, BPF_F_NO_PREALLOC);
	__uint(max_entries, 64);
	__type(key, __u32);
	__type(value, __u64);
} rhashmap SEC(".maps");

__u32 key_sum = 0;
__u64 val_sum = 0;
__u32 elem_count = 0;
__u32 err = 0;

SEC("iter/bpf_map_elem")
int dump_bpf_rhash_map(struct bpf_iter__bpf_map_elem *ctx)
{
	__u32 *key = ctx->key;
	__u64 *val = ctx->value;

	if (!key || !val)
		return 0;

	key_sum += *key;
	val_sum += *val;
	elem_count++;
	return 0;
}
