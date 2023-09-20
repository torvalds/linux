// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2020 Facebook */
#include <stddef.h>
#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include "bpf_misc.h"

#define MAX_STACK (512 - 3 * 32 + 8)

static __attribute__ ((noinline))
int f0(int var, struct __sk_buff *skb)
{
	asm volatile ("");

	return skb->len;
}

__attribute__ ((noinline))
int f1(struct __sk_buff *skb)
{
	volatile char buf[MAX_STACK] = {};

	__sink(buf[MAX_STACK - 1]);

	return f0(0, skb) + skb->len;
}

int f3(int, struct __sk_buff *skb, int);

__attribute__ ((noinline))
int f2(int val, struct __sk_buff *skb)
{
	return f1(skb) + f3(val, skb, 1);
}

__attribute__ ((noinline))
int f3(int val, struct __sk_buff *skb, int var)
{
	volatile char buf[MAX_STACK] = {};

	__sink(buf[MAX_STACK - 1]);

	return skb->ifindex * val * var;
}

SEC("tc")
__failure __msg("combined stack size of 4 calls is 544")
int global_func1(struct __sk_buff *skb)
{
	return f0(1, skb) + f1(skb) + f2(2, skb) + f3(3, skb, 4);
}
