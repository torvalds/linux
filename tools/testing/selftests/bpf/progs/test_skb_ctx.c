// SPDX-License-Identifier: GPL-2.0

#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>

#include "bpf_compiler.h"

char _license[] SEC("license") = "GPL";

SEC("tc")
int process(struct __sk_buff *skb)
{
	__pragma_loop_unroll_full
	for (int i = 0; i < 5; i++) {
		if (skb->cb[i] != i + 1)
			return 1;
		skb->cb[i]++;
	}
	skb->priority++;
	skb->tstamp++;
	skb->mark++;

	if (skb->wire_len != 100)
		return 1;
	if (skb->gso_segs != 8)
		return 1;
	if (skb->gso_size != 10)
		return 1;
	if (skb->ingress_ifindex != 11)
		return 1;
	if (skb->ifindex != 1)
		return 1;
	if (skb->hwtstamp != 11)
		return 1;

	return 0;
}
