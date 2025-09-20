// SPDX-License-Identifier: GPL-2.0

#include <vmlinux.h>
#include <bpf/bpf_helpers.h>
#include "bpf_misc.h"

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
int main_changes_with_subprogs(struct __sk_buff *sk)
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

__noinline
long might_sleep(struct pt_regs *ctx __arg_ctx)
{
	int i;

	bpf_copy_from_user(&i, sizeof(i), NULL);
	return i;
}

__noinline __weak
long does_not_sleep(struct pt_regs *ctx __arg_ctx)
{
	return 0;
}

SEC("?uprobe.s")
int main_might_sleep_with_subprogs(struct pt_regs *ctx)
{
	might_sleep(ctx);
	does_not_sleep(ctx);
	return 0;
}

SEC("?uprobe.s")
int main_might_sleep(struct pt_regs *ctx)
{
	int i;

	bpf_copy_from_user(&i, sizeof(i), NULL);
	return i;
}

SEC("?uprobe.s")
int main_does_not_sleep(struct pt_regs *ctx)
{
	return 0;
}

char _license[] SEC("license") = "GPL";
