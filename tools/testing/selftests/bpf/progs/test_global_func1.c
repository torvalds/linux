// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2020 Facebook */
#include <stddef.h>
#include <linux/bpf.h>
#include <bpf/bpf_helpers.h>
#include "bpf_misc.h"

#define MAX_STACK 260

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
	volatile char buf[MAX_STACK] = {};

	__sink(buf[MAX_STACK - 1]);

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
__load_if_no_large_stack()
__failure __msg("combined stack size of 3 calls is")
int global_func1(struct __sk_buff *skb)
{
	return f0(1, skb) + f1(skb) + f2(2, skb) + f3(3, skb, 4);
}

/*
 * A chain of five frames that stay under 512 bytes each but add up to more
 * than the 2 KiB budget of JITs with large stacks; the chain also exceeds
 * 512 bytes after two frames, so it is rejected everywhere.
 */
#define MAX_STACK_LARGE 480

__attribute__ ((noinline))
int g0(struct __sk_buff *skb)
{
	volatile char buf[MAX_STACK_LARGE] = {};

	__sink(buf[MAX_STACK_LARGE - 1]);

	return skb->len;
}

__attribute__ ((noinline))
int g1(struct __sk_buff *skb)
{
	volatile char buf[MAX_STACK_LARGE] = {};

	__sink(buf[MAX_STACK_LARGE - 1]);

	return g0(skb) + skb->len;
}

__attribute__ ((noinline))
int g2(struct __sk_buff *skb)
{
	volatile char buf[MAX_STACK_LARGE] = {};

	__sink(buf[MAX_STACK_LARGE - 1]);

	return g1(skb) + skb->len;
}

__attribute__ ((noinline))
int g3(struct __sk_buff *skb)
{
	volatile char buf[MAX_STACK_LARGE] = {};

	__sink(buf[MAX_STACK_LARGE - 1]);

	return g2(skb) + skb->len;
}

__attribute__ ((noinline))
int g4(struct __sk_buff *skb)
{
	volatile char buf[MAX_STACK_LARGE] = {};

	__sink(buf[MAX_STACK_LARGE - 1]);

	return g3(skb) + skb->len;
}

SEC("tc")
__failure __msg("combined stack size of {{[0-9]+}} calls is")
int global_func1_deep(struct __sk_buff *skb)
{
	return g4(skb);
}
