// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2020 Facebook */
#include <vmlinux.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

char _license[] SEC("license") = "GPL";

struct key_t {
	int a;
	int b;
	int c;
};

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, 3);
	__type(key, struct key_t);
	__type(value, __u64);
} hashmap1 SEC(".maps");

__u32 key_sum = 0;

SEC("iter/bpf_map_elem")
int dump_bpf_hash_map(struct bpf_iter__bpf_map_elem *ctx)
{
	void *key = ctx->key;

	if (key == (void *)0)
		return 0;

	/* out of bound access w.r.t. hashmap1 */
	key_sum += *(__u32 *)(key + sizeof(struct key_t));
	return 0;
}
