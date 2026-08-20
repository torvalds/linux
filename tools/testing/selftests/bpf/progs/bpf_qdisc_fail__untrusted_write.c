// SPDX-License-Identifier: GPL-2.0

#include <vmlinux.h>
#include "bpf_experimental.h"
#include "bpf_qdisc_common.h"
#include "bpf_misc.h"

char _license[] SEC("license") = "GPL";

SEC("struct_ops")
__failure __msg("only read is supported")
int BPF_PROG(untrusted_write, struct sk_buff *skb, struct Qdisc *sch,
	     struct bpf_sk_buff_ptr *to_free)
{
	struct Qdisc *next = sch->next_sched;

	/*
	 * sch is trusted, but the walk of next_sched yields a plain
	 * PTR_TO_BTF_ID which may fault on a dereference. A store through
	 * it does not get an exception table entry, there is no probed
	 * store to rewrite it into, hence it has to be rejected before
	 * bpf_qdisc_btf_struct_access() gets to allow the write to limit.
	 */
	next->limit = 1000;

	bpf_qdisc_skb_drop(skb, to_free);
	return NET_XMIT_DROP;
}

SEC("struct_ops")
__auxiliary
struct sk_buff *BPF_PROG(bpf_qdisc_test_dequeue, struct Qdisc *sch)
{
	return NULL;
}

SEC("struct_ops")
__auxiliary
int BPF_PROG(bpf_qdisc_test_init, struct Qdisc *sch, struct nlattr *opt,
	     struct netlink_ext_ack *extack)
{
	return 0;
}

SEC("struct_ops")
__auxiliary
void BPF_PROG(bpf_qdisc_test_reset, struct Qdisc *sch)
{
}

SEC("struct_ops")
__auxiliary
void BPF_PROG(bpf_qdisc_test_destroy, struct Qdisc *sch)
{
}

SEC(".struct_ops")
struct Qdisc_ops test = {
	.enqueue   = (void *)untrusted_write,
	.dequeue   = (void *)bpf_qdisc_test_dequeue,
	.init      = (void *)bpf_qdisc_test_init,
	.reset     = (void *)bpf_qdisc_test_reset,
	.destroy   = (void *)bpf_qdisc_test_destroy,
	.id        = "bpf_qdisc_test",
};
