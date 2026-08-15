// SPDX-License-Identifier: GPL-2.0

#include <vmlinux.h>
#include "bpf_experimental.h"
#include "bpf_qdisc_common.h"
#include "bpf_misc.h"

char _license[] SEC("license") = "GPL";

int proto;

SEC("struct_ops")
__failure __msg("Expected an initialized dynptr as R1")
int BPF_PROG(invalid_dynptr, struct sk_buff *skb, struct Qdisc *sch,
	     struct bpf_sk_buff_ptr *to_free)
{
	struct bpf_dynptr ptr;
	struct ethhdr *hdr;

	bpf_dynptr_from_skb((struct __sk_buff *)skb, 0, &ptr);

	bpf_qdisc_skb_drop(skb, to_free);

	hdr = bpf_dynptr_slice(&ptr, 0, NULL, sizeof(*hdr));
	if (!hdr)
		return NET_XMIT_DROP;

	proto = hdr->h_proto;

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
	.enqueue   = (void *)invalid_dynptr,
	.dequeue   = (void *)bpf_qdisc_test_dequeue,
	.init      = (void *)bpf_qdisc_test_init,
	.reset     = (void *)bpf_qdisc_test_reset,
	.destroy   = (void *)bpf_qdisc_test_destroy,
	.id        = "bpf_qdisc_test",
};
