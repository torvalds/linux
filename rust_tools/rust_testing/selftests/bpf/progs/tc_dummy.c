// SPDX-License-Identifier: GPL-2.0
#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include "bpf_legacy.h"

SEC("tc")
int entry(struct __sk_buff *skb)
{
	return 1;
}

char __license[] SEC("license") = "GPL";
