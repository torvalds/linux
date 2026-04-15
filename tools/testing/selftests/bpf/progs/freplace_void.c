// SPDX-License-Identifier: GPL-2.0
#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>

SEC("freplace/foo")
void test_freplace_void(struct __sk_buff *skb)
{
}

char _license[] SEC("license") = "GPL";
