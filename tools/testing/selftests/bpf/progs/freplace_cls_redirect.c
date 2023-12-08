// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2020 Facebook

#include <linux/stddef.h>
#include <linux/bpf.h>
#include <linux/pkt_cls.h>
#include <bpf/bpf_endian.h>
#include <bpf/bpf_helpers.h>

struct {
	__uint(type, BPF_MAP_TYPE_SOCKMAP);
	__type(key, int);
	__type(value, int);
	__uint(max_entries, 2);
} sock_map SEC(".maps");

SEC("freplace/cls_redirect")
int freplace_cls_redirect_test(struct __sk_buff *skb)
{
	int ret = 0;
	const int zero = 0;
	struct bpf_sock *sk;

	sk = bpf_map_lookup_elem(&sock_map, &zero);
	if (!sk)
		return TC_ACT_SHOT;

	ret = bpf_map_update_elem(&sock_map, &zero, sk, 0);
	bpf_sk_release(sk);

	return ret == 0 ? TC_ACT_OK : TC_ACT_SHOT;
}

char _license[] SEC("license") = "GPL";
