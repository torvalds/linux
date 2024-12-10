// SPDX-License-Identifier: GPL-2.0

#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>

__noinline
long changes_pkt_data(struct __sk_buff *sk, __u32 len)
{
	return bpf_skb_pull_data(sk, len);
}

__noinline __weak
long does_not_change_pkt_data(struct __sk_buff *sk, __u32 len)
{
	return 0;
}

SEC("tc")
int dummy(struct __sk_buff *sk)
{
	changes_pkt_data(sk, 0);
	does_not_change_pkt_data(sk, 0);
	return 0;
}

char _license[] SEC("license") = "GPL";
