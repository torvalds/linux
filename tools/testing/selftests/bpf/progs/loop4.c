// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2019 Facebook
#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>

char _license[] SEC("license") = "GPL";

SEC("socket")
int combinations(volatile struct __sk_buff* skb)
{
	int ret = 0, i;

#pragma nounroll
	for (i = 0; i < 20; i++)
		if (skb->len)
			ret |= 1 << i;
	return ret;
}
