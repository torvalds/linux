// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2020 Facebook */
#include <stddef.h>
#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>

__noinline int foo(struct __sk_buff *skb)
{
	return bpf_get_prandom_u32();
}

SEC("cgroup_skb/ingress")
int test_cls(struct __sk_buff *skb)
{
	if (!foo(skb))
		return 0;

	return 1;
}
