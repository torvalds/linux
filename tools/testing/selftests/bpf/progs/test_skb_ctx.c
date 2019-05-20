// SPDX-License-Identifier: GPL-2.0

#include <linux/bpf.h>
#include "bpf_helpers.h"

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

	return 0;
}
