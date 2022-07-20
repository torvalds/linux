// SPDX-License-Identifier: GPL-2.0
#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>

__noinline
int test_ctx_global_func(struct __sk_buff *skb)
{
	volatile int retval = 1;
	return retval;
}

SEC("freplace/test_pkt_access")
int new_test_pkt_access(struct __sk_buff *skb)
{
	return test_ctx_global_func(skb);
}

char _license[] SEC("license") = "GPL";
