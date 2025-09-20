// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2020 Facebook */
#include <stddef.h>
#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include "bpf_misc.h"

__attribute__ ((noinline))
int f1(struct __sk_buff *skb)
{
	return skb->len;
}

int f3(int, struct __sk_buff *skb);

__attribute__ ((noinline))
int f2(int val, struct __sk_buff *skb)
{
	return f1(skb) + f3(val, skb + 1); /* type mismatch */
}

__attribute__ ((noinline))
int f3(int val, struct __sk_buff *skb)
{
	return skb->ifindex * val;
}

SEC("tc")
__failure __msg("modified ctx ptr R2")
int global_func6(struct __sk_buff *skb)
{
	return f1(skb) + f2(2, skb) + f3(3, skb);
}
