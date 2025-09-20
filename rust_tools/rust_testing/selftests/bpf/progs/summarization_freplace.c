// SPDX-License-Identifier: GPL-2.0

#include <vmlinux.h>
#include <bpf/bpf_helpers.h>

SEC("?freplace")
long changes_pkt_data(struct __sk_buff *sk)
{
	return bpf_skb_pull_data(sk, 0);
}

SEC("?freplace")
long does_not_change_pkt_data(struct __sk_buff *sk)
{
	return 0;
}

SEC("?freplace")
long might_sleep(struct pt_regs *ctx)
{
	int i;

	bpf_copy_from_user(&i, sizeof(i), NULL);
	return i;
}

SEC("?freplace")
long does_not_sleep(struct pt_regs *ctx)
{
	return 0;
}

char _license[] SEC("license") = "GPL";
