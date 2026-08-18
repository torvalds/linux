// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2026 Meta Platforms, Inc. and affiliates. */

#include <vmlinux.h>
#include <stdbool.h>
#include <bpf/bpf_helpers.h>
#include "bpf_kfuncs.h"

#define CLOCK_MONOTONIC 1

struct timer_elem {
	struct bpf_timer timer;
};

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, 1);
	__type(key, int);
	__type(value, struct timer_elem);
} timer_map SEC(".maps");

int timer_result;

#if (defined(__TARGET_ARCH_x86) || defined(__TARGET_ARCH_arm64)) && \
	defined(__BPF_FEATURE_STACK_ARGUMENT)

const volatile bool has_stack_arg = true;

__noinline static int static_func_many_args(int a, int b, int c, int d,
					    int e, int f, int g, int h,
					    int i, int j)
{
	return a + b + c + d + e + f + g + h + i + j;
}

__noinline int global_calls_many_args(int a, int b, int c)
{
	return static_func_many_args(a, b, c, a + 3, a + 4, a + 5, a + 6,
				     a + 7, a + 8, a + 9);
}

SEC("tc")
int test_global_many_args(void)
{
	return global_calls_many_args(1, 2, 3);
}

struct test_data {
	long x;
	long y;
};

/* 1+2+3+4+5+6+7+8+9+10+20 = 75 */
__noinline static long func_with_ptr_stack_arg(long a, long b, long c, long d,
					       long e, long f, long g, long h,
					       long i, struct test_data *p)
{
	return a + b + c + d + e + f + g + h + i + p->x + p->y;
}

__noinline long global_ptr_stack_arg(long a, long b, long c, long d, long e)
{
	struct test_data data = { .x = 10, .y = 20 };

	return func_with_ptr_stack_arg(a, b, c, d, e, a + 5, a + 6, a + 7,
				      a + 8, &data);
}

SEC("tc")
int test_bpf2bpf_ptr_stack_arg(void)
{
	return global_ptr_stack_arg(1, 2, 3, 4, 5);
}

/* 1+2+3+4+5+6+7+10+8+20 = 66 */
__noinline static long func_with_mix_stack_args(long a, long b, long c, long d,
						long e, long f, long g,
						struct test_data *p,
						long h, struct test_data *q)
{
	return a + b + c + d + e + f + g + p->x + h + q->y;
}

__noinline long global_mix_stack_args(long a, long b, long c, long d, long e)
{
	struct test_data p = { .x = 10 };
	struct test_data q = { .y = 20 };

	return func_with_mix_stack_args(a, b, c, d, e, e + 1, e + 2, &p,
					e + 3, &q);
}

SEC("tc")
int test_bpf2bpf_mix_stack_args(void)
{
	return global_mix_stack_args(1, 2, 3, 4, 5);
}

/*
 * Nesting test: func_outer calls func_inner, both with struct pointer
 * as stack arg.
 *
 * func_inner: (a+1)+...+(i+1) + p->x + p->y
 *           = 2+3+4+5+6+7+8+9+10+10+20 = 84
 */
__noinline static long func_inner_ptr(long a, long b, long c, long d,
				      long e, long f, long g, long h,
				      long i, struct test_data *p)
{
	return a + b + c + d + e + f + g + h + i + p->x + p->y;
}

__noinline static long func_outer_ptr(long a, long b, long c, long d,
				      long e, long f, long g, long h,
				      long i, struct test_data *p)
{
	return func_inner_ptr(a + 1, b + 1, c + 1, d + 1, e + 1,
			      f + 1, g + 1, h + 1, i + 1, p);
}

__noinline long global_nesting_ptr(long a, long b, long c, long d, long e)
{
	struct test_data data = { .x = 10, .y = 20 };

	return func_outer_ptr(a, b, c, d, e, a + 5, a + 6, a + 7, a + 8,
			      &data);
}

SEC("tc")
int test_bpf2bpf_nesting_stack_arg(void)
{
	return global_nesting_ptr(1, 2, 3, 4, 5);
}

/* 1+2+3+4+5+6+7+8+9+sizeof(pkt_v4) = 45+54 = 99 */
__noinline static long func_with_dynptr(long a, long b, long c, long d,
					long e, long f, long g, long h,
					long i, struct bpf_dynptr *ptr)
{
	return a + b + c + d + e + f + g + h + i + bpf_dynptr_size(ptr);
}

__noinline long global_dynptr_stack_arg(void *ctx __arg_ctx, long a, long b,
					long c, long d)
{
	struct bpf_dynptr ptr;

	bpf_dynptr_from_skb(ctx, 0, &ptr);
	return func_with_dynptr(a, b, c, d, d + 1, d + 2, d + 3, d + 4,
				d + 5, &ptr);
}

SEC("tc")
int test_bpf2bpf_dynptr_stack_arg(struct __sk_buff *skb)
{
	return global_dynptr_stack_arg(skb, 1, 2, 3, 4);
}

/* foo1: a+b+c+d+e+f+g+h+i+j */
__noinline static int foo1(int a, int b, int c, int d, int e,
			   int f, int g, int h, int i, int j)
{
	return a + b + c + d + e + f + g + h + i + j;
}

/* foo2: a+b+c+d+e+f+g+h+i+j+k+l */
__noinline static int foo2(int a, int b, int c, int d, int e,
			   int f, int g, int h, int i, int j,
			   int k, int l)
{
	return a + b + c + d + e + f + g + h + i + j + k + l;
}

/* global_two_callees calls foo1 (5 stack args) and foo2 (7 stack args).
 * The outgoing stack arg area is sized for foo2 (the larger callee).
 * Stores for foo1 are a subset of the area used by foo2.
 * Result: foo1(1..10) + foo2(1..12) = 55 + 78 = 133
 *
 * Pass a-e through so the compiler can't constant-fold the stack args away.
 */
__noinline int global_two_callees(int a, int b, int c, int d, int e)
{
	int ret;

	ret = foo1(a, b, c, d, e, a + 5, a + 6, a + 7, a + 8, a + 9);
	ret += foo2(a, b, c, d, e, a + 5, a + 6, a + 7, a + 8, a + 9,
		    a + 10, a + 11);
	return ret;
}

SEC("tc")
int test_two_callees(void)
{
	return global_two_callees(1, 2, 3, 4, 5);
}

const volatile int timer_base = 10;

static int timer_cb_many_args(void *map, int *key, struct bpf_timer *timer)
{
	int v = timer_base;

	timer_result = static_func_many_args(v, v * 2, v * 3, v * 4, v * 5,
					     v * 6, v * 7, v * 8, v * 9,
					     v * 10);
	return 0;
}

SEC("tc")
int test_async_cb_many_args(void)
{
	struct timer_elem *elem;
	int key = 0;

	elem = bpf_map_lookup_elem(&timer_map, &key);
	if (!elem)
		return -1;

	bpf_timer_init(&elem->timer, &timer_map, CLOCK_MONOTONIC);
	bpf_timer_set_callback(&elem->timer, timer_cb_many_args);
	bpf_timer_start(&elem->timer, 1, 0);
	return 0;
}

#else

const volatile bool has_stack_arg = false;

SEC("tc")
int test_global_many_args(void)
{
	return 0;
}

SEC("tc")
int test_bpf2bpf_ptr_stack_arg(void)
{
	return 0;
}

SEC("tc")
int test_bpf2bpf_mix_stack_args(void)
{
	return 0;
}

SEC("tc")
int test_bpf2bpf_nesting_stack_arg(void)
{
	return 0;
}

SEC("tc")
int test_bpf2bpf_dynptr_stack_arg(struct __sk_buff *skb)
{
	return 0;
}

SEC("tc")
int test_two_callees(void)
{
	return 0;
}

SEC("tc")
int test_async_cb_many_args(void)
{
	return 0;
}

#endif

char _license[] SEC("license") = "GPL";
