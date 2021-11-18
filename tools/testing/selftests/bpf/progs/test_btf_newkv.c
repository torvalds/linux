/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2018 Facebook */
#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include "bpf_legacy.h"

struct ipv_counts {
	unsigned int v4;
	unsigned int v6;
};

/* just to validate we can handle maps in multiple sections */
struct bpf_map_def SEC("maps") btf_map_legacy = {
	.type = BPF_MAP_TYPE_ARRAY,
	.key_size = sizeof(int),
	.value_size = sizeof(long long),
	.max_entries = 4,
};

BPF_ANNOTATE_KV_PAIR(btf_map_legacy, int, struct ipv_counts);

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, 4);
	__type(key, int);
	__type(value, struct ipv_counts);
} btf_map SEC(".maps");

__attribute__((noinline))
int test_long_fname_2(void)
{
	struct ipv_counts *counts;
	int key = 0;

	counts = bpf_map_lookup_elem(&btf_map, &key);
	if (!counts)
		return 0;

	counts->v6++;

	/* just verify we can reference both maps */
	counts = bpf_map_lookup_elem(&btf_map_legacy, &key);
	if (!counts)
		return 0;

	return 0;
}

__attribute__((noinline))
int test_long_fname_1(void)
{
	return test_long_fname_2();
}

SEC("dummy_tracepoint")
int _dummy_tracepoint(void *arg)
{
	return test_long_fname_1();
}

char _license[] SEC("license") = "GPL";
