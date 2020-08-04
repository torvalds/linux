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
	__uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
	__uint(max_entries, 3);
	__type(key, __u32);
	__type(value, __u32);
} arraymap1 SEC(".maps");

/* will set before prog run */
volatile const __u32 num_cpus = 0;

__u32 key_sum = 0, val_sum = 0;

SEC("iter/bpf_map_elem")
int dump_bpf_percpu_array_map(struct bpf_iter__bpf_map_elem *ctx)
{
	__u32 *key = ctx->key;
	void *pptr = ctx->value;
	__u32 step;
	int i;

	if (key == (void *)0 || pptr == (void *)0)
		return 0;

	key_sum += *key;

	step = 8;
	for (i = 0; i < num_cpus; i++) {
		val_sum += *(__u32 *)pptr;
		pptr += step;
	}
	return 0;
}
