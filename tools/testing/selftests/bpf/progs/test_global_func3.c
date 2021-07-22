// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2020 Facebook */
#include <stddef.h>
#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>

__attribute__ ((noinline))
int f1(struct __sk_buff *skb)
{
	return skb->len;
}

__attribute__ ((noinline))
int f2(int val, struct __sk_buff *skb)
{
	return f1(skb) + val;
}

__attribute__ ((noinline))
int f3(int val, struct __sk_buff *skb, int var)
{
	return f2(var, skb) + val;
}

__attribute__ ((noinline))
int f4(struct __sk_buff *skb)
{
	return f3(1, skb, 2);
}

__attribute__ ((noinline))
int f5(struct __sk_buff *skb)
{
	return f4(skb);
}

__attribute__ ((noinline))
int f6(struct __sk_buff *skb)
{
	return f5(skb);
}

__attribute__ ((noinline))
int f7(struct __sk_buff *skb)
{
	return f6(skb);
}

#ifndef NO_FN8
__attribute__ ((noinline))
int f8(struct __sk_buff *skb)
{
	return f7(skb);
}
#endif

SEC("classifier/test")
int test_cls(struct __sk_buff *skb)
{
#ifndef NO_FN8
	return f8(skb);
#else
	return f7(skb);
#endif
}
