// SPDX-License-Identifier: GPL-2.0

#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

__u64 fentry_called = 0;

SEC("fentry/test_pkt_md_access_new")
int BPF_PROG(fentry, struct sk_buff *skb)
{
	fentry_called = skb->len;
	return 0;
}

__u64 fexit_called = 0;

SEC("fexit/test_pkt_md_access_new")
int BPF_PROG(fexit, struct sk_buff *skb)
{
	fexit_called = skb->len;
	return 0;
}

char _license[] SEC("license") = "GPL";
