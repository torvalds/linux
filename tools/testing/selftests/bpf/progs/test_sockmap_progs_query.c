// SPDX-License-Identifier: GPL-2.0
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>

struct {
	__uint(type, BPF_MAP_TYPE_SOCKMAP);
	__uint(max_entries, 1);
	__type(key, __u32);
	__type(value, __u64);
} sock_map SEC(".maps");

SEC("sk_skb")
int prog_skb_verdict(struct __sk_buff *skb)
{
	return SK_PASS;
}

SEC("sk_msg")
int prog_skmsg_verdict(struct sk_msg_md *msg)
{
	return SK_PASS;
}

char _license[] SEC("license") = "GPL";
