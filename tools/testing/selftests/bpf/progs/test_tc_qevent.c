// SPDX-License-Identifier: GPL-2.0
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>

int redirect_ifindex = 1;
__u64 verdict_calls = 0;
__u64 helper_calls = 0;

SEC("tc")
int qevent_redirect_verdict(struct __sk_buff *skb)
{
	__sync_fetch_and_add(&verdict_calls, 1);
	return TCX_REDIRECT;
}

SEC("tc")
int qevent_redirect_helper(struct __sk_buff *skb)
{
	__sync_fetch_and_add(&helper_calls, 1);
	return bpf_redirect(redirect_ifindex, 0);
}

char _license[] SEC("license") = "GPL";
