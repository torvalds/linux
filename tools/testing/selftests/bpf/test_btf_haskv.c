/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2018 Facebook */
#include <linux/bpf.h>
#include "bpf_helpers.h"

int _version SEC("version") = 1;

struct ipv_counts {
	unsigned int v4;
	unsigned int v6;
};

typedef int btf_map_key;
typedef struct ipv_counts btf_map_value;
btf_map_key dumm_key;
btf_map_value dummy_value;

struct bpf_map_def SEC("maps") btf_map = {
	.type = BPF_MAP_TYPE_ARRAY,
	.key_size = sizeof(int),
	.value_size = sizeof(struct ipv_counts),
	.max_entries = 4,
};

struct dummy_tracepoint_args {
	unsigned long long pad;
	struct sock *sock;
};

SEC("dummy_tracepoint")
int _dummy_tracepoint(struct dummy_tracepoint_args *arg)
{
	struct ipv_counts *counts;
	int key = 0;

	if (!arg->sock)
		return 0;

	counts = bpf_map_lookup_elem(&btf_map, &key);
	if (!counts)
		return 0;

	counts->v6++;

	return 0;
}

char _license[] SEC("license") = "GPL";
