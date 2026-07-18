// SPDX-License-Identifier: GPL-2.0

#include <vmlinux.h>
#include <bpf/bpf_helpers.h>

struct {
	__uint(type, BPF_MAP_TYPE_PROG_ARRAY);
	__uint(max_entries, 1);
	__uint(key_size, sizeof(__u32));
	__uint(value_size, sizeof(__u32));
} prog_array SEC(".maps");

SEC("cgroup_skb/egress")
int caller_prog(struct __sk_buff *skb)
{
	bpf_tail_call(skb, &prog_array, 0);
	return 1;
}

SEC("cgroup_skb/egress")
int leaf_prog(struct __sk_buff *skb)
{
	return 1;
}

char _license[] SEC("license") = "GPL";
