/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2018 Facebook */
#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>

struct ipv_counts {
	unsigned int v4;
	unsigned int v6;
};

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(key_size, sizeof(int));
	__uint(value_size, sizeof(struct ipv_counts));
	__uint(max_entries, 4);
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
