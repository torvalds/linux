/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2018 Facebook */
#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include "bpf_legacy.h"

int _version SEC("version") = 1;

struct ipv_counts {
	unsigned int v4;
	unsigned int v6;
};

struct bpf_map_def SEC("maps") btf_map = {
	.type = BPF_MAP_TYPE_ARRAY,
	.key_size = sizeof(int),
	.value_size = sizeof(struct ipv_counts),
	.max_entries = 4,
};

BPF_ANNOTATE_KV_PAIR(btf_map, int, struct ipv_counts);

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
