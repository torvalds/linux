// SPDX-License-Identifier: GPL-2.0

#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>

int _version SEC("version") = 1;
char _license[] SEC("license") = "GPL";

SEC("skb_ctx")
int process(struct __sk_buff *skb)
{
	#pragma clang loop unroll(full)
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

	return 0;
}
