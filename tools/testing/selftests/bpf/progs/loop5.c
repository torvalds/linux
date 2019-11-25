// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2019 Facebook
#include <linux/bpf.h>
#include "bpf_helpers.h"
#define barrier() __asm__ __volatile__("": : :"memory")

char _license[] SEC("license") = "GPL";

SEC("socket")
int while_true(volatile struct __sk_buff* skb)
{
	int i = 0;

	while (1) {
		if (skb->len)
			i += 3;
		else
			i += 7;
		if (i == 9)
			break;
		barrier();
		if (i == 10)
			break;
		barrier();
		if (i == 13)
			break;
		barrier();
		if (i == 14)
			break;
	}
	return i;
}
