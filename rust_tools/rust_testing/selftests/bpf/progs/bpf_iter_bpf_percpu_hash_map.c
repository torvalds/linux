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
	__uint(type, BPF_MAP_TYPE_PERCPU_HASH);
	__uint(max_entries, 3);
	__type(key, struct key_t);
	__type(value, __u32);
} hashmap1 SEC(".maps");

/* will set before prog run */
volatile const __s32 num_cpus = 0;

/* will collect results during prog run */
__u32 key_sum_a = 0, key_sum_b = 0, key_sum_c = 0;
__u32 val_sum = 0;

SEC("iter/bpf_map_elem")
int dump_bpf_percpu_hash_map(struct bpf_iter__bpf_map_elem *ctx)
{
	struct key_t *key = ctx->key;
	void *pptr = ctx->value;
	__u32 step;
	int i;

	if (key == (void *)0 || pptr == (void *)0)
		return 0;

	key_sum_a += key->a;
	key_sum_b += key->b;
	key_sum_c += key->c;

	step = 8;
	for (i = 0; i < num_cpus; i++) {
		val_sum += *(__u32 *)pptr;
		pptr += step;
	}
	return 0;
}
