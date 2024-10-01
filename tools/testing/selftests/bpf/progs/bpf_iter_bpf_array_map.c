// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2020 Facebook */
#include "bpf_iter.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

char _license[] SEC("license") = "GPL";

struct key_t {
	int a;
	int b;
	int c;
};

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, 3);
	__type(key, __u32);
	__type(value, __u64);
} arraymap1 SEC(".maps");

__u32 key_sum = 0;
__u64 val_sum = 0;

SEC("iter/bpf_map_elem")
int dump_bpf_array_map(struct bpf_iter__bpf_map_elem *ctx)
{
	__u32 *key = ctx->key;
	__u64 *val = ctx->value;

	if (key == (void *)0 || val == (void *)0)
		return 0;

	bpf_seq_write(ctx->meta->seq, key, sizeof(__u32));
	bpf_seq_write(ctx->meta->seq, val, sizeof(__u64));
	key_sum += *key;
	val_sum += *val;
	*val = *key;
	return 0;
}
