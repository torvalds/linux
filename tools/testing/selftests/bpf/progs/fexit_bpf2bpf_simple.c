// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2019 Facebook */
#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include "bpf_trace_helpers.h"

struct sk_buff {
	unsigned int len;
};

__u64 test_result = 0;

SEC("fexit/test_pkt_md_access")
int BPF_PROG(test_main2, struct sk_buff *skb, int ret)
{
	int len;

	__builtin_preserve_access_index(({
		len = skb->len;
	}));
	if (len != 74 || ret != 0)
		return 0;

	test_result = 1;
	return 0;
}
char _license[] SEC("license") = "GPL";
