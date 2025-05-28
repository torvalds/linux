// SPDX-License-Identifier: GPL-2.0

#include <vmlinux.h>
#include "bpf_experimental.h"
#include "bpf_qdisc_common.h"

char _license[] SEC("license") = "GPL";

SEC("struct_ops")
int BPF_PROG(bpf_qdisc_test_enqueue, struct sk_buff *skb, struct Qdisc *sch,
	     struct bpf_sk_buff_ptr *to_free)
{
	bpf_qdisc_skb_drop(skb, to_free);
	return NET_XMIT_DROP;
}

SEC("struct_ops")
struct sk_buff *BPF_PROG(bpf_qdisc_test_dequeue, struct Qdisc *sch)
{
	return NULL;
}

SEC("struct_ops")
void BPF_PROG(bpf_qdisc_test_reset, struct Qdisc *sch)
{
}

SEC("struct_ops")
void BPF_PROG(bpf_qdisc_test_destroy, struct Qdisc *sch)
{
}

SEC(".struct_ops")
struct Qdisc_ops test = {
	.enqueue   = (void *)bpf_qdisc_test_enqueue,
	.dequeue   = (void *)bpf_qdisc_test_dequeue,
	.reset     = (void *)bpf_qdisc_test_reset,
	.destroy   = (void *)bpf_qdisc_test_destroy,
	.id        = "bpf_qdisc_test",
};

