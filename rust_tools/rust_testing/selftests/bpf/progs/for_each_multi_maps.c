// SPDX-License-Identifier: GPL-2.0
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>

char _license[] SEC("license") = "GPL";

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, 3);
	__type(key, __u32);
	__type(value, __u64);
} arraymap SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, 5);
	__type(key, __u32);
	__type(value, __u64);
} hashmap SEC(".maps");

struct callback_ctx {
	int output;
};

u32 data_output = 0;
int use_array = 0;

static __u64
check_map_elem(struct bpf_map *map, __u32 *key, __u64 *val,
	       struct callback_ctx *data)
{
	data->output += *val;
	return 0;
}

SEC("tc")
int test_pkt_access(struct __sk_buff *skb)
{
	struct callback_ctx data;

	data.output = 0;
	if (use_array)
		bpf_for_each_map_elem(&arraymap, check_map_elem, &data, 0);
	else
		bpf_for_each_map_elem(&hashmap, check_map_elem, &data, 0);
	data_output = data.output;

	return 0;
}
