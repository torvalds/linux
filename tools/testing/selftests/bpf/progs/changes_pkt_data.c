// SPDX-License-Identifier: GPL-2.0

#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>

__noinline
long changes_pkt_data(struct __sk_buff *sk)
{
	return bpf_skb_pull_data(sk, 0);
}

__noinline __weak
long does_not_change_pkt_data(struct __sk_buff *sk)
{
	return 0;
}

SEC("?tc")
int main_with_subprogs(struct __sk_buff *sk)
{
	changes_pkt_data(sk);
	does_not_change_pkt_data(sk);
	return 0;
}

SEC("?tc")
int main_changes(struct __sk_buff *sk)
{
	bpf_skb_pull_data(sk, 0);
	return 0;
}

SEC("?tc")
int main_does_not_change(struct __sk_buff *sk)
{
	return 0;
}

char _license[] SEC("license") = "GPL";
