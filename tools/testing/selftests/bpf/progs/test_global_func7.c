// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2020 Facebook */
#include <stddef.h>
#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>

__attribute__ ((noinline))
void foo(struct __sk_buff *skb)
{
	skb->tc_index = 0;
}

SEC("classifier/test")
int test_cls(struct __sk_buff *skb)
{
	foo(skb);
	return 0;
}
