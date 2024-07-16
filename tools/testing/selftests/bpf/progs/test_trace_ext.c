// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2019 Facebook
#include <linux/bpf.h>
#include <stdbool.h>
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_endian.h>
#include <bpf/bpf_tracing.h>

__u64 ext_called = 0;

SEC("freplace/test_pkt_md_access")
int test_pkt_md_access_new(struct __sk_buff *skb)
{
	ext_called = skb->len;
	return 0;
}

char _license[] SEC("license") = "GPL";
