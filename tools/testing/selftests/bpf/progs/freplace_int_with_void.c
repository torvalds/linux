// SPDX-License-Identifier: GPL-2.0
#include <linux/bpf.h>
#include <linux/pkt_cls.h>
#include <bpf/bpf_helpers.h>

SEC("freplace/global_func2")
void test_freplace_int_with_void(struct __sk_buff *skb)
{
}

char _license[] SEC("license") = "GPL";
